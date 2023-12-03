#pragma once
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/kobject.h>

extern bool kio_is_running(void);

struct kio_config;
extern int kio_run(const struct kio_config *kc);
