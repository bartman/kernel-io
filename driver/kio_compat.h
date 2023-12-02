#pragma once

#include <linux/version.h>
#include <linux/types.h>
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/bvec.h>
#include <uapi/linux/eventpoll.h>
#include <asm/paravirt.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,16,0)
#define __poll_t unsigned
#else
#include <uapi/linux/types.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
#define vm_fault_t int
#else
#include <linux/mm_types.h>
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(5,6,0)
#include <linux/time.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
#define VM_FAULT_TAKES_VMA_VMF 1
#define VM_MKWRITE_TAKES_VMA_VMF 1
#else
#define VM_FAULT_TAKES_VMA_VMF 0
#define VM_MKWRITE_TAKES_VMA_VMF 0
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
#define VM_FAULT_HAS_PTL 0
#define VM_FAULT_HAS_PTE 0
#define VM_FAULT_HAS_ORIG_PTE 0
#define HAVE_VM_FAULT_ENV 1
#else
#define VM_FAULT_HAS_PTL 1
#define VM_FAULT_HAS_PTE 1
#define VM_FAULT_HAS_ORIG_PTE 1
#define HAVE_VM_FAULT_ENV 0
#endif

#if VM_FAULT_HAS_PTL
/* the first time the fault occurs in a vma, we will get the fault
 * under a ptl lock -- which we have to release before we exit */
#define vm_fault_release_vmf_ptl(vmf) ({                                     \
	if ((vmf)->ptl && spin_is_locked((vmf)->ptl)) {                      \
		spin_unlock((vmf)->ptl);                                     \
		(vmf)->ptl = NULL;                                           \
	}                                                                    \
})
#else
#define vm_fault_release_vmf_ptl(vmf) ({ (void)vmf; })
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,10,0)
#define vmf_address(vmf) ((unsigned long)(vmf)->virtual_address)
#else
#define vmf_address(vmf) (vmf)->address
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
#define USE_P4D_T 1
#else
#define USE_P4D_T 0
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(4,11,0)
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(4,14,0)
#define compat_release_pages(pages,nr_pages) release_pages(pages, nr_pages)
#else
#define compat_release_pages(pages,nr_pages) release_pages(pages, nr_pages, 0)
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(4,14,0)
#define compat_cpumask_last(cpumask) cpumask_last(cpumask)
#else
/* find_last_bit was added in 4.0 */
#define compat_cpumask_last(cpumask) find_last_bit(cpumask_bits(cpumask), nr_cpumask_bits)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
static inline vm_fault_t vmf_insert_pfn(struct vm_area_struct *vma,
			unsigned long addr, unsigned long pfn)
{
	int err = vm_insert_pfn(vma, addr, pfn);

	if (err == -ENOMEM)
		return VM_FAULT_OOM;
	if (err < 0 && err != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
#define __flush_tlb_one_user(addr) __flush_tlb_single(addr)
#define __flush_tlb_one_kernel(addr) __flush_tlb_single(addr)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
#define MMU_NOTIFIER_START_RETURN_TYPE void
#define MMU_NOTIFIER_START_RETURN_ON_SUCCESS
#define MMU_NOTIFIER_START_RETURN_ON_FAILURE
#define MMU_NOTIFIER_MM_ARG , struct mm_struct *mm
#define MMU_NOTIFIER_RANGE_ARG , unsigned long start, unsigned long end
#define MMU_NOTIFIER_START_BLOCKABLE_ARG
#define MMU_NOTIFIER_RANGE_VAR
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,21,0)
#define MMU_NOTIFIER_START_RETURN_TYPE int
#define MMU_NOTIFIER_START_RETURN_ON_SUCCESS 0
#define MMU_NOTIFIER_START_RETURN_ON_FAILURE -EAGAIN
#define MMU_NOTIFIER_MM_ARG , struct mm_struct *mm
#define MMU_NOTIFIER_RANGE_ARG , unsigned long start, unsigned long end
#define MMU_NOTIFIER_START_BLOCKABLE_ARG ,bool blockable
#define MMU_NOTIFIER_RANGE_VAR
#else /* > 4.21 */
#define MMU_NOTIFIER_START_RETURN_TYPE int
#define MMU_NOTIFIER_START_RETURN_ON_SUCCESS 0
#define MMU_NOTIFIER_START_RETURN_ON_FAILURE -EAGAIN
#define MMU_NOTIFIER_MM_ARG
#define MMU_NOTIFIER_RANGE_ARG , const struct mmu_notifier_range *range
#define MMU_NOTIFIER_START_BLOCKABLE_ARG
#define MMU_NOTIFIER_RANGE_VAR unsigned long start = range->start, end = range->end;
#endif

/* 96d4f267e40f9 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,21,0)
#define compat_access_ok(type, addr, size) \
	access_ok(type, addr, size)
#else
#define compat_access_ok(type, addr, size) \
	access_ok(addr, size)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
static inline int down_read_killable(struct rw_semaphore *sem)
{
	/* this compat function does not try very hard */
	if (fatal_signal_pending(current))
		return -EINTR;
	down_read(sem);
	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
#define BVEC_ITER_HAS_BI_DONE 1
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
#define BIO_HAS_BI_STATUS 1
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0)
#define BIO_HAS_BI_ISSUE
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0)
#define bio_has_disk(bio) ( (bio)->bi_bdev && (bio)->bi_bdev->bd_disk )
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
#define BIO_HAS_BI_DISK 1
#if !defined( bio_set_dev)
#define bio_set_dev(bio, bdev) ({ \
	(bio)->bi_disk = (bdev)->bd_disk; \
	(bio)->bi_partno = (bdev)->bd_partno; \
})
#endif
#define bio_has_disk(bio) ( (bio)->bi_disk != NULL )
#else /* pre 4.13 */
#define bio_set_dev(bio, bdev) ({ \
	(bio)->bi_bdev = (bdev); \
})
#define bio_has_disk(bio) ( (bio)->bi_bdev != NULL )
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0)
#define USE_BIOSET_INIT 1
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
#define USE_BIOSET_CREATE 1
#define BIOSET_CREATE_HAS_FLAGS 1
#else
#define USE_BIOSET_CREATE 1
#undef BIOSET_CREATE_HAS_FLAGS
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,16,0)
#define TASK_STRUCT_HAS_RECENT_USED_CPU 1
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,3,0)
#define TASK_STRUCT_HAS_CPUS_ALLOWED
#endif
static inline const struct cpumask *compat_task_cpus_allowed(struct task_struct *task)
{
#ifdef TASK_STRUCT_HAS_CPUS_ALLOWED
	return &task->cpus_allowed;
#else
	return task->cpus_ptr ?: &task->cpus_mask;
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0)
#define USE_MMU_NOTIFIER_SUBSCRIPTIONS
#define USE_MM_TRACE_RSS_STAT
extern void (*kio_mm_trace_rss_stat)(struct mm_struct *mm, int member, long count);
extern void mm_trace_rss_stat(struct mm_struct *mm, int member, long count);
#else
#define USE_MMU_NOTIFIER_MM
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,3,0)
#define HAVE_MMU_NOTIFIER_UNREGISTER_NO_RELEASE
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
#define HAVE_MMU_NOTIFIER_RANGE
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
#define mmap_read_lock_killable(mm) \
	down_read_killable(&(mm)->mmap_sem)
#define mmap_read_unlock(mm) \
	up_read(&(mm)->mmap_sem)
#endif

#if defined(_UAPI_ASM_GENERIC_INT_LL64_H)
#define MODULE_PARAM_USE_LL64 1
#elif defined(_UAPI_ASM_GENERIC_INT_L64_H)
#define MODULE_PARAM_USE_L64 1
#elif defined(CONFIG_64BIT)
#define MODULE_PARAM_USE_L64 1
#elif defined(_ASM_GENERIC_ATOMIC64_H)
#define MODULE_PARAM_USE_LL64 1
#else
#error neither _UAPI_ASM_GENERIC_INT_L64_H nor _UAPI_ASM_GENERIC_INT_LL64_H defined
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,20,0)
#undef MODULE_PARAM_USE_LL64
#define MODULE_PARAM_USE_L64 1
#endif

#ifdef MODULE_PARAM_USE_LL64
#define atomic64_module_param_named(name, var, mode) \
	module_param_named(name,  var.counter,  ullong,  mode)
#elif MODULE_PARAM_USE_L64
#define atomic64_module_param_named(name, var, mode) \
	module_param_named(name,  var.counter,  ulong,  mode)
#endif




