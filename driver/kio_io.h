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
