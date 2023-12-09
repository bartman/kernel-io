#pragma once
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/blk_types.h>
#include "kio_compat.h"

extern int kio_io_init(void);
extern void kio_io_exit(void);

// preallocate this many BIOs
#define KIO_USE_BIO_SET_MIN_COUNT 1024

struct kio_io {
	char *dev_name;
	struct block_device *bdev;
	size_t dev_byte_size;
	unsigned dev_block_size;
#if KIO_USE_BIO_SET_MIN_COUNT
#ifdef USE_BIOSET_INIT
	struct bio_set bio_set;
#define KIO_IO_BIO_SET(io) &(io)->bio_set
#else // USE_BIOSET_CREATE
	struct bio_set *bio_set;
#define KIO_IO_BIO_SET(io) (io)->bio_set
#endif
#endif
};
extern struct kio_io kio_io;

static inline u64 kio_io_dev_byte_size(void)
{
	return kio_io.dev_byte_size;
}

static inline unsigned kio_io_dev_block_size(void)
{
	return kio_io.dev_block_size;
}

static inline bool kio_io_offset_is_valid(off_t off, size_t size)
{
	return (off+size) <= kio_io.dev_byte_size
		&& !(off % kio_io.dev_block_size);
}

static inline const char * kio_io_dev_name(void)
{
	return kio_io.dev_name;
}

extern int kio_io_submit(off_t off, struct page *page, bool is_write,
			     bio_end_io_t fn, void *bi_private);

static inline int kio_io_submit_write(struct page *page, off_t off,
			     bio_end_io_t fn, void *bi_private)
{
	return kio_io_submit(off, page, true, fn, bi_private);
}

static inline int kio_io_submit_read(off_t off, struct page *page,
			   bio_end_io_t fn, void *bi_private)
{
	return kio_io_submit(off, page, false, fn, bi_private);
}

static inline s64* kio_io_bio_start_time_ptr(struct bio *bio)
{
#if KIO_USE_BIO_SET_MIN_COUNT
	return ((s64*)bio) - 1;
#else
	struct bio_vec *vec = bio->bi_io_vec + 1;
	s64 *time = ((s64*)&vec->bv_page) + 1;
	if (unlikely (bio->bi_max_vecs < 2))
		return NULL;
	return time;
#endif
}

static inline void kio_io_bio_set_start_time(struct bio *bio)
{
	s64 *time = kio_io_bio_start_time_ptr(bio);
	if (time)
		*time = ktime_to_ns(ktime_get());
}

static inline s64 kio_io_bio_get_start_time(struct bio *bio)
{
	s64 *time = kio_io_bio_start_time_ptr(bio);
	return time ? *time : 0;
}

static inline s64 kio_bio_get_latency(struct bio *bio)
{
	u64 now = ktime_to_ns(ktime_get());
	u64 start = kio_io_bio_get_start_time(bio);
	s64 diff = (start && now>start) ? (now-start) : 0;
	return diff;
}
