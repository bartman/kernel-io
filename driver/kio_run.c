#include <linux/atomic.h>

#include "kio_run.h"

static atomic_t kio_running = {0};
bool kio_is_running(void)
{
	return atomic_read(&kio_running);
}

void kio_run(struct kio_config *kc)
{
	pr_info("running\n");
	atomic_set(&kio_running, 1);




	atomic_set(&kio_running, 0);
	pr_info("complete\n");
}
