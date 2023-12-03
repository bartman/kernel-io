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

	uint32_t block_size;
	uint32_t queue_depth;

	uint8_t offset_random:1;

	off_t offset_low;
	off_t offset_high;

	uint8_t read_mix_percent;

	uint32_t read_burst;
	uint32_t write_burst;

	uint32_t read_sleep_usec;
	uint32_t write_sleep_usec;
};

extern int kio_config_init(void);
extern void kio_config_exit(void);
