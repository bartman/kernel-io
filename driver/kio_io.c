/* Copyright 2023 Bart Trojanowski <bart@jukie.net> */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/circ_buf.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#include "kio_io.h"
#include "kio_compat.h"

static char *kio_block_device;
module_param_named(block_device, kio_block_device, charp, S_IRUGO);
MODULE_PARM_DESC(block_device, "target for IO");

static unsigned kio_io_submit_mode = 0;
module_param_named(io_submit_mode, kio_io_submit_mode, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(io_submit_mode, "0 submit_bio, 1 generic_make_request, 2 q->make_request_fn");

struct kio_io kio_io = {};

int kio_io_init(void)
{
	char *dev_name = NULL;
	struct block_device *bdev;
	struct request_queue *q;
	size_t dev_byte_size;
	unsigned block_size = 0;
	int rc;

	/* get the device */

	if (!kio_block_device) {
		pr_warn("kio: no block_device provided for IO\n");
		return -EINVAL;
	}

	pr_debug("%s: block_device: %s\n", __func__, kio_block_device);

	dev_name = strim(kstrdup(kio_block_device, GFP_KERNEL));
	if (!dev_name) {
		pr_warn("kio: could not to allocate space for devname %s\n",
			kio_block_device);
		return -ENOMEM;
	}

	bdev = blkdev_get_by_path(dev_name, FMODE_READ|FMODE_WRITE|FMODE_EXCL,
				  THIS_MODULE);
	if (!bdev || IS_ERR(bdev)) {
		pr_warn("kio: failed to get block device %s\n",
			kio_block_device);
		rc = PTR_ERR(bdev) ?: -ENODEV;
		goto err_release_dev_name;
	}

	pr_debug("%s: bdev: %px\n", __func__, bdev);

	q = bdev->bd_disk ? bdev->bd_disk->queue : NULL;
	if (!q) {
		pr_warn("kio: %s doeas not have a disk queue\n",
			kio_block_device);
		rc = -ENODEV;
		goto err_no_queue;
	}
	block_size = queue_logical_block_size(q);

	rc = set_blocksize(bdev, PAGE_SIZE);
	pr_debug("%s: set_blocksize(4k): %d\n", __func__, rc);
	if (rc) {
		pr_warn("kio: failed to set blocksize of %s to %lu\n",
			kio_block_device, PAGE_SIZE);
		goto err_put_back_bd;
	}

	dev_byte_size = i_size_read(bdev->bd_inode);
	pr_debug("%s: byte_size: %zu (%zu GiB)\n", __func__, dev_byte_size, dev_byte_size>>30);
	if (dev_byte_size < (1<<30)) {
		pr_warn("kio: %s is too small (%zu)\n",
			kio_block_device, dev_byte_size);
		rc = -ETOOSMALL;
		goto err_put_back_bd;
	}

	pr_debug("%s: : %zu\n", __func__, dev_byte_size);

	/* allocate the bio set */

#if KIO_USE_BIO_SET_MIN_COUNT
#ifdef USE_BIOSET_INIT
	rc = bioset_init(KIO_IO_BIO_SET(&kio_io),
			 KIO_USE_BIO_SET_MIN_COUNT, sizeof(s64), BIOSET_NEED_BVECS);
	if (unlikely (rc)) {
		pr_warn("kio: could not create bio set\n");
		goto err_bio_set_init;
	}
#else // USE_BIOSET_CREATE
	KIO_IO_BIO_SET(&kio_io) = bioset_create(
			KIO_USE_BIO_SET_MIN_COUNT, sizeof(s64)
#ifdef BIOSET_CREATE_HAS_FLAGS
			, BIOSET_NEED_BVECS
#endif
			);
	if (unlikely (!KIO_IO_BIO_SET(&kio_io))) {
		pr_warn("kio: could not create bio set\n");
		rc = -ENOMEM;
		goto err_bio_set_init;
	}
#endif
#endif

	/* finalize */

	kio_io.dev_name = dev_name;
	kio_io.bdev = bdev;
	kio_io.dev_block_size = block_size;
	kio_io.dev_byte_size = dev_byte_size;

	pr_info("kio: using %s with %zu bytes available, with %u block size\n",
		kio_io.dev_name, kio_io.dev_byte_size, block_size);

	return 0;

err_put_back_bd:
#if KIO_USE_BIO_SET_MIN_COUNT
#ifdef USE_BIOSET_INIT
	bioset_exit(KIO_IO_BIO_SET(&kio_io));
#else
	bioset_free(KIO_IO_BIO_SET(&kio_io));
#endif
err_bio_set_init:
#endif
err_no_queue:
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
err_release_dev_name:
	kfree(dev_name);
	return rc;
}

void kio_io_exit(void)
{
#if KIO_USE_BIO_SET_MIN_COUNT
#ifdef USE_BIOSET_INIT
	bioset_exit(KIO_IO_BIO_SET(&kio_io));
#else
	bioset_free(KIO_IO_BIO_SET(&kio_io));
#endif
#endif

	kfree(kio_io.dev_name);
	blkdev_put(kio_io.bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
	memset(&kio_io, 0, sizeof(kio_io));
}

#ifdef BDEV_HAS_BD_PART
/* this function was not exported, but it's used to skip directly into
 * make_request_fn */
static struct hd_struct *kio_disk_get_part(struct gendisk *disk, int partno)
{
	struct disk_part_tbl *ptbl = rcu_dereference(disk->part_tbl);
	if (unlikely(partno < 0 || partno >= ptbl->len))
		return NULL;
	return rcu_dereference(ptbl->part[partno]);
}

#endif

static inline void blk_partition_remap(struct bio *bio)
{
	struct block_device *bdev = kio_io.bdev;
        struct block_device *whole = bdev_whole(bdev);
	if (unlikely (bdev != whole)) {
#ifdef BDEV_HAS_BD_PART
		struct hd_struct *p;
		if (bio->bi_partno)
			return;

		rcu_read_lock();
		p = kio_disk_get_part(bio->bi_disk, bio->bi_partno);
		if (p) {
			bio->bi_iter.bi_sector += p->start_sect;
			bio->bi_partno = 0;
		}
		rcu_read_unlock();
#else
		/* TODO: this is probably incomplete */
                bio->bi_iter.bi_sector += bdev->bd_start_sect;
#endif
	}
}

int kio_io_submit(off_t off, struct page *page, bool is_write,
			     bio_end_io_t fn, void *bi_private)
{
	struct bio *bio;
	int rc;
	blk_qc_t qc;

	if (unlikely (!page)) {
		pr_warn("%s: page==NULL, cannot queue %s bio\n",
			__func__, is_write ? "write" : "read");
		return -EFAULT;
	}

	if (unlikely (!kio_io_offset_is_valid(off, PAGE_SIZE))) {
		pr_warn("%s: invalid off=%lx max=%lx, cannot queue %s bio\n",
			__func__, off, kio_io.dev_byte_size,
			is_write ? "write" : "read");
		return -EINVAL;
	}

	/* NOTE: we use only 1 bvec, but allocate +1 */
#if KIO_USE_BIO_SET_MIN_COUNT
	bio = bio_alloc_bioset(GFP_ATOMIC, 1, KIO_IO_BIO_SET(&kio_io));
#else
	bio = bio_alloc(GFP_ATOMIC, 2);
#endif
	if (unlikely (!bio))
		return -ENOMEM;

	/* the second bvec also holds the start time, see kio_io_bio_get_start_time() */
	kio_io_bio_set_start_time(bio);

	bio->bi_iter.bi_sector = off >> SECTOR_SHIFT;
	bio_set_dev(bio, kio_io.bdev);

	rc = bio_add_page(bio, page, PAGE_SIZE, 0);
	/* bio_add_page() returns length added on success */
	if (unlikely (!rc)) {
		bio_put(bio);
		return -EIO;
	}

	if (is_write)
		bio->bi_opf = REQ_OP_WRITE | REQ_SYNC;
	else
		bio->bi_opf = REQ_OP_READ;

	bio->bi_end_io = fn;
	bio->bi_private = bi_private;

	if (unlikely(!bio_has_disk(bio))) {
		pr_warn("%s: bio->...disk in NULL, cannot queue %s bio\n",
			__func__, is_write ? "write" : "read");
		bio_put(bio);
		return -EIO;
	}

	if (unlikely (!bio->bi_vcnt || !bio->bi_io_vec[0].bv_page)) {
		pr_warn("%s: bi_vcnt=%u, bi_io_vec[0].bv_page=%px, cannot queue %s bio\n",
			__func__, bio->bi_vcnt, bio->bi_io_vec[0].bv_page,
			is_write ? "write" : "read");
		bio_put(bio);
		return -EIO;
	}

	switch (kio_io_submit_mode) {
	default:
	case 0:
		qc = submit_bio(bio);
		break;
	case 1:
		qc = submit_bio_noacct(bio);
		break;
	case 2: {
#ifdef GENDISK_HAS_SUBMIT_BIO
                struct gendisk *disk = bio->bi_bdev->bd_disk;
		blk_partition_remap(bio);
                qc = disk->fops->submit_bio(bio);
#else
		struct request_queue *q = kio_io.bdev->bd_disk->queue;
		blk_partition_remap(bio);
		qc = q->make_request_fn(q, bio);
#endif
		break;
		}
	}
	(void)qc; // not sure what to do with this now

	return 0;
}
