#pragma once
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/blk_types.h>

extern int kio_io_init(void);
extern void kio_io_exit(void);

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

static inline s64 kio_io_bio_get_start_time(struct bio *bio)
{
	struct bio_vec *vec = bio->bi_io_vec + 1;
	s64 *time = ((s64*)&vec->bv_page) + 1;
	if (unlikely (bio->bi_max_vecs < 2))
		return 0;
	return *time;
}

static inline s64 kio_bio_get_latency(struct bio *bio)
{
	u64 now = ktime_to_ns(ktime_get());
	u64 start = kio_io_bio_get_start_time(bio);
	s64 diff = (start && now>start) ? (now-start) : 0;
	return diff;
}
