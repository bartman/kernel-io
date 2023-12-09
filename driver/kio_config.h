#pragma once
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/kobject.h>

struct kio_thread_config;

#define KIO_MAX_RUNTIME_SECONDS 3600

struct kio_config {
	struct mutex mutex;

	uint32_t runtime_seconds;

	uint32_t num_threads;
	struct kio_thread_config *threads;
};

struct kio_thread_config {
	struct kobject *kobj;

	off_t offset_low;
	off_t offset_high;

	uint32_t block_size;
	uint32_t queue_depth;
	uint32_t offset_stride;         // offset increment, if non-zero

	uint32_t offset_random:1;       // if set random offsets
	uint32_t burst_delay:1;         // delay applied on burst, not IOs
	uint32_t burst_finish:1;        // finish burst before starting another

	uint8_t read_mix_percent;       // mix of bursts, not IOs

	uint32_t read_burst;            // keep reading for this many IOs
	uint32_t write_burst;           // keep writing for this many IOs

	uint32_t read_sleep_usec;       // delay after each read
	uint32_t write_sleep_usec;      // delay after each write
};

extern int kio_config_init(void);
extern void kio_config_exit(void);
