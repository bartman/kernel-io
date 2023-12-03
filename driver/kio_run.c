#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

#include "kio_run.h"
#include "kio_config.h"

static atomic_t kio_running = {0};
bool kio_is_running(void)
{
	return atomic_read(&kio_running);
}

struct kio_thread {
	unsigned index;
	struct task_struct *thread;

	wait_queue_head_t *wqh;
	bool *emergency_stop;
};

static int kio_thread_fn(void *data)
{
	struct kio_thread *th = data;
	int result = 0;

	pr_info("thread[%u]: start\n",
		th->index);

	while (!kthread_should_stop()) {
		schedule_timeout(1);

		if (signal_pending(current)) {
			result = -EINTR;
			break;
		}
	}

	pr_info("thread[%u]: done, result=%d\n",
		th->index, result);

	if (result<0) {
		mb();
		*(th->emergency_stop) = true;
		mb();
		wake_up_interruptible(th->wqh);
	}

	return result;
}

int kio_run(struct kio_config *kc)
{
	int result = 0, i;
	size_t ths_size;
	struct kio_thread *ths;
	DECLARE_WAIT_QUEUE_HEAD(wqh);
	bool emergency_stop = false;

	pr_info("kio: setup for %u threads, %u seconds\n",
		kc->num_threads, kc->runtime_seconds);
	atomic_set(&kio_running, 1);

	ths_size = kc->num_threads * sizeof(*ths);
	ths = kzalloc(ths_size, GFP_KERNEL);
	if (!ths)
		return -ENOMEM;

	for (i=0; i<kc->num_threads; i++) {
		ths[i].index = i;
		ths[i].wqh = &wqh;
		ths[i].emergency_stop = &emergency_stop;

		ths[i].thread = kthread_run(kio_thread_fn, &ths[i], "kio[%d]", i);


		if (IS_ERR(ths[i].thread)) {
			result = PTR_ERR(ths[i].thread);
			if (i==0)
				return result;
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

	return result;
}
