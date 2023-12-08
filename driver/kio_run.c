/* Copyright 2023 Bart Trojanowski <bart@jukie.net> */
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/atomic.h>
#include <linux/blk_types.h>
#include <linux/bio.h>
#include <linux/delay.h>

#include "kio_run.h"
#include "kio_config.h"
#include "kio_compat.h"
#include "kio_io.h"

static atomic_t kio_running = {0};
bool kio_is_running(void)
{
	return atomic_read(&kio_running);
}

struct kio_thread {
	unsigned index;
	struct task_struct *thread;
	const struct kio_thread_config *config;

	wait_queue_head_t *run_wqh;
	bool *emergency_stop;

	wait_queue_head_t *bio_wqh;

	atomic_t dispatched;
	atomic_t completed;

	u64 runtime;
	u64 slat_total;
	atomic64_t clat_total;

	uint32_t read_burst;
	uint32_t write_burst;
	off_t next_offset;
};

static inline struct page *kio_new_page(void)
{
	int nid = NUMA_NO_NODE;
	return alloc_pages_node(nid, GFP_KERNEL|__GFP_ZERO|__GFP_THISNODE, 0);
}

void kio_bio_completion (struct bio *bio)
{
	struct kio_thread *th = bio->bi_private;
	struct page *page = bio->bi_io_vec[0].bv_page;
	s64 clat_nsec;

	clat_nsec = kio_bio_get_latency(bio);
	atomic64_add(clat_nsec, &th->clat_total);

	atomic_dec(&th->dispatched);
	atomic_inc(&th->completed);

	if (th->bio_wqh)
		wake_up_interruptible(th->bio_wqh);

	__free_page(page);
	bio_put(bio);
}

static inline int kio_thread_too_busy(struct kio_thread *th)
{
	const struct kio_thread_config *ktc = th->config;
	return ktc->queue_depth
		&& atomic_read(&th->dispatched) >= ktc->queue_depth;
}

static inline bool kio_thread_is_write_next(struct kio_thread *th)
{
	const struct kio_thread_config *ktc = th->config;
	u32 rnd;

	if (th->read_burst) {
		th->read_burst --;
		return false;
	}

	if (th->write_burst) {
		th->write_burst --;
		return true;
	}

	if (ktc->read_mix_percent >= 100)
		return false;

	if (ktc->read_mix_percent < 1)
		return true;

	rnd = prandom_u32() % 100;
	if (rnd < ktc->read_mix_percent) {
		th->read_burst = ktc->read_burst - 1;
		return false;
	} else {
		th->write_burst = ktc->write_burst - 1;
		return true;
	}
}

static inline off_t kio_thread_next_offset(struct kio_thread *th)
{
	const struct kio_thread_config *ktc = th->config;
	off_t result;

	u64 range = ktc->offset_high - ktc->offset_low;
	if (unlikely (!range))
	    return ktc->offset_low;

	if (ktc->offset_random) {
		u32 lo = prandom_u32();
		u32 hi = prandom_u32();
		u64 rnd = (u64)hi<<32 | lo;
		return ktc->offset_low + (rnd % range);
	}

	result = th->next_offset;

	th->next_offset += ktc->block_size;
	if (th->next_offset > ktc->offset_high)
		th->next_offset = ktc->offset_low;

	return result;
}

static int kio_thread_fn(void *data)
{
	DECLARE_WAIT_QUEUE_HEAD(wqh);
	struct kio_thread *th = data;
	const struct kio_thread_config *ktc = th->config;
	int result = 0, rc;
	s64 thread_start;

	pr_info("kio: thread[%u]: start\n",
		th->index);

	th->bio_wqh = &wqh;
	th->runtime = 0;
	thread_start = ktime_to_ns(ktime_get());

	while (!kthread_should_stop()) {
		off_t offset;
		struct page *page;
		s64 io_start, slat_nsec;
		bool is_write;
		u32 sleep_usec;

		if (unlikely(kio_thread_too_busy(th))) {
			rc = wait_event_interruptible(wqh,
					      !kio_thread_too_busy(th));
			(void)rc;
		}

		page = kio_new_page();
		if (unlikely(!page)) {
			pr_warn("kio: thread[%u]: failed allocate page\n",
				th->index);
			result = -ENOMEM;
			break;
		}

		is_write = kio_thread_is_write_next(th);
		offset = kio_thread_next_offset(th);

		io_start = ktime_to_ns(ktime_get());

		atomic_inc(&th->dispatched);

		result = kio_io_submit(offset, page, is_write,
				       kio_bio_completion, th);
		if (unlikely(result<0)) {
			pr_warn("kio: thread[%u]: failed read dispatch at %ld, with %d\n",
				th->index, offset, result);
			break;
		}

		slat_nsec = ktime_to_ns(ktime_get()) - io_start;

		th->slat_total += slat_nsec;

		sleep_usec = is_write ? ktc->write_sleep_usec : ktc->read_sleep_usec;
		if (unlikely(sleep_usec)) {
			unsigned long hz = usecs_to_jiffies(sleep_usec);
			if (hz)
				schedule_timeout(hz);
			else
				udelay(ktc->read_sleep_usec);
		}

		if (unlikely(signal_pending(current))) {
			result = -EINTR;
			break;
		}
	}

	rc = wait_event_interruptible(wqh, !atomic_read(&th->dispatched));

	th->runtime = ktime_to_ns(ktime_get()) - thread_start;

	if (atomic_read(&th->dispatched)) {
		pr_warn("kio: thread[%u]: %u pending requests, completed=%d, result=%d\n",
			th->index, atomic_read(&th->dispatched),
			atomic_read(&th->completed), result);
	} else {
		pr_info("kio: thread[%u]: done, completed=%d, result=%d\n",
			th->index, atomic_read(&th->completed), result);
	}

	th->bio_wqh = NULL;

	if (result<0) {
		mb();
		*(th->emergency_stop) = true;
		mb();
		wake_up_interruptible(th->run_wqh);
	}

	return result;
}

struct kio_run_stats {
	u32 num_threads;
	u64 dispatched;
	u64 completed;
	u64 slat_total;
	u64 clat_total;
	u64 bps_total;
	u64 runtime_total;
};

static void kio_run_stats_thread(const struct kio_thread *th,
				 struct kio_run_stats *st)
{
	const struct kio_thread_config *ktc = th->config;
	u32 cnt=0, iops=0;
	u64 slat=0, clat=0, lat=0, bps=0;

	cnt = atomic_read(&th->completed);
	if (cnt) {
		slat = th->slat_total / cnt;
		clat = atomic64_read(&th->clat_total) / cnt;
		lat = slat + clat;
	}

	if (th->runtime) {
		iops = ((u64)cnt * NSEC_PER_SEC) / th->runtime;
		bps = ((u64)cnt * ktc->block_size * NSEC_PER_SEC) / th->runtime;
	}


	pr_warn("kio: thread[%u]: completed=%u "
		"lat=%llu.%03llu(%llu.%03llu+%llu.%03llu) iops=%u MB/s=%llu.%03llu\n",
		th->index, cnt,
		lat/1000, lat%1000,
		slat/1000, slat%1000,
		clat/1000, clat%1000,
		iops,
		bps/1000000, (bps/1000)%1000);

	st->num_threads ++;
	st->dispatched += atomic_read(&th->dispatched);
	st->completed += cnt;
	st->slat_total += th->slat_total;
	st->clat_total += atomic64_read(&th->clat_total);
	st->bps_total += bps;
	st->runtime_total += th->runtime;
}

static void kio_run_stats_total(const struct kio_run_stats *st)
{
	u32 cnt=0, iops=0;
	u64 slat=0, clat=0, lat=0, bps=0;

	cnt = st->completed;
	if (cnt) {
		slat = st->slat_total / cnt;
		clat = st->clat_total / cnt;
		lat = slat + clat;
	}

	if (st->runtime_total && st->num_threads) {
		iops = ((u64)cnt * NSEC_PER_SEC)
			/ (st->runtime_total * st->num_threads);
		bps = st->bps_total / st->num_threads;
	}

	pr_warn("kio: summary: completed=%u "
		"lat=%llu.%03llu(%llu.%03llu+%llu.%03llu) iops=%u MB/s=%llu.%03llu\n",
		cnt,
		lat/1000, lat%1000,
		slat/1000, slat%1000,
		clat/1000, clat%1000,
		iops,
		bps/1000000, (bps/1000)%1000);
}

int kio_run(const struct kio_config *kc)
{
	int result = 0, i;
	size_t ths_size;
	struct kio_thread *ths;
	DECLARE_WAIT_QUEUE_HEAD(wqh);
	bool emergency_stop = false;

#if 1
	for (i=0; i<kc->num_threads; i++) {
		if (kc->threads[i].block_size != 4096) {
			pr_warn("kio: current implementation only "
				"supports block_size of 4096\n");
			return -ERANGE;
		}
	}
#endif

	pr_info("kio: setup for %u threads, %u seconds\n",
		kc->num_threads, kc->runtime_seconds);
	atomic_set(&kio_running, 1);

	ths_size = kc->num_threads * sizeof(*ths);
	ths = kzalloc(ths_size, GFP_KERNEL);
	if (!ths)
		return -ENOMEM;

	for (i=0; i<kc->num_threads; i++) {
		ths[i].index = i;
		ths[i].config = &kc->threads[i];
		ths[i].run_wqh = &wqh;
		ths[i].emergency_stop = &emergency_stop;

		ths[i].thread = kthread_run(kio_thread_fn, &ths[i], "kio[%d]", i);


		if (IS_ERR(ths[i].thread)) {
			result = PTR_ERR(ths[i].thread);
			if (i==0) {
				kfree(ths);
				return result;
			}
			break;
		}
	}

	if (!result) {
		long jiffies = HZ * kc->runtime_seconds;
		wait_event_interruptible_timeout(wqh, emergency_stop,
						 jiffies);
		if (emergency_stop)
			result = -EINTR;
	}

	for (i=0; i<kc->num_threads; i++) {
		if (!ths[i].thread)
			continue;
		kthread_stop(ths[i].thread);
	}

	atomic_set(&kio_running, 0);
	pr_info("kio: stopped threads, result=%d\n", result);

	if (!result) {
		struct kio_run_stats st = {};
		for (i=0; i<kc->num_threads; i++)
			kio_run_stats_thread(&ths[i], &st);
		kio_run_stats_total(&st);
	}

	kfree(ths);
	return result;
}
