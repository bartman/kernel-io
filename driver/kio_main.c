#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#include <linux/writeback.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/signal.h>
#include <linux/proc_fs.h>
#include <asm/tlbflush.h>
#include <asm/mman.h>
#include <linux/moduleparam.h>
#include <linux/mempolicy.h>
#include <linux/userfaultfd_k.h>
#include <linux/delay.h>
#include <linux/mmu_notifier.h>
#include <linux/kprobes.h>
#include <linux/wait.h>
#include <linux/interrupt.h>

#include "kio_compat.h"
#include "kio_version.h"


static int __init kio_init(void)
{
	int rc = 0;

	pr_info("kio: init, version %s\n", KIO_GIT_REVISION);


	return rc;
}

static void __exit kio_exit(void)
{
	pr_info("kio: exit\n");
}

module_init(kio_init);
module_exit(kio_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION(KIO_GIT_REVISION);
