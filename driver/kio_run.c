#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/kthread.h>

#include "kio_run.h"
#include "kio_config.h"

static atomic_t kio_running = {0};
bool kio_is_running(void)
{
	return atomic_read(&kio_running);
}

struct kio_thread {
	struct task_struct *thread;
};

static int kio_thread_fn(void *data)
{
	struct kio_thread *th = data;
	int result;

	(void)th;

	result = 0;

	return result;
}

int kio_run(struct kio_config *kc)
{
	int result = 0, rc, i;
	size_t ths_size;
	struct kio_thread *ths;

	pr_info("running\n");
	atomic_set(&kio_running, 1);

	ths_size = kc->num_threads * sizeof(*ths);
	ths = kzalloc(ths_size, GFP_KERNEL);
	if (!ths)
		return -ENOMEM;

	for (i=0; i<kc->num_threads; i++) {
		ths[i].thread = kthread_run(kio_thread_fn, &ths[i], "kio[%d]", i);
		if (IS_ERR(ths[i].thread)) {
			result = PTR_ERR(ths[i].thread);
			if (i==0)
				return result;
			break;
		}
	}

	for (i=0; i<kc->num_threads; i++) {
		if (!ths[i].thread)
			continue;
		rc = kthread_stop(ths[i].thread);
		if (!result)
			result = rc;
	}

	atomic_set(&kio_running, 0);
	pr_info("complete\n");

	return result;
}
