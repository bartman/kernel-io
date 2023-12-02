#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/init.h>

#include "kio_config.h"

static struct kobject *kio_kobj;
struct kio_config kio_config = {};

// ------------------------------------------------------------------------

enum {
	KIO_BLOCK_SIZE,
	KIO_OFFSET_RANDOM,
	KIO_OFFSET_LOW,
	KIO_OFFSET_HIGH,
	KIO_READ_MIX_PERCENT,
	KIO_READ_BURST,
	KIO_WRITE_BURST,
};

static inline struct kio_thread_config *kio_thread_config_from_kobj(struct kobject *kobj)
{
	int i;
	for (i = 0; i < kio_config.num_threads; i++) {
		if (kio_config.threads[i].kobj == kobj)
			return &kio_config.threads[i];
	}
	return NULL;
}

static ssize_t kio_thread_var_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf, int var_index)
{
	struct kio_thread_config *ktc;
	long value;

	ktc = kio_thread_config_from_kobj(kobj);
	if (!ktc)
		return -ENODEV;

	switch (var_index) {
	case KIO_BLOCK_SIZE:       value = ktc->block_size;       break;
	case KIO_OFFSET_RANDOM:    value = ktc->offset_random;    break;
	case KIO_OFFSET_LOW:       value = ktc->offset_low;       break;
	case KIO_OFFSET_HIGH:      value = ktc->offset_high;      break;
	case KIO_READ_MIX_PERCENT: value = ktc->read_mix_percent; break;
	case KIO_READ_BURST:       value = ktc->read_burst;       break;
	case KIO_WRITE_BURST:      value = ktc->write_burst;      break;
	default: return -ENOENT;
	}
	return sprintf(buf, "%ld\n", value);
}

static ssize_t kio_thread_var_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count, int var_index)
{
	struct kio_thread_config *ktc;
	long value = -1;
	int rc;

	ktc = kio_thread_config_from_kobj(kobj);
	if (!ktc)
		return -ENODEV;

	rc = sscanf(buf, "%ld", &value);
	if (rc<0)
		return rc;
	if (!rc)
		return -EINVAL;

	switch (var_index) {
	case KIO_BLOCK_SIZE:       ktc->block_size       = value; break;
	case KIO_OFFSET_RANDOM:    ktc->offset_random    = value; break;
	case KIO_OFFSET_LOW:       ktc->offset_low       = value; break;
	case KIO_OFFSET_HIGH:      ktc->offset_high      = value; break;
	case KIO_READ_MIX_PERCENT: ktc->read_mix_percent = value; break;
	case KIO_READ_BURST:       ktc->read_burst       = value; break;
	case KIO_WRITE_BURST:      ktc->write_burst      = value; break;
	default: return -ENOENT;
	}

	return count;
}

#define VAR_ATTR_SHOW_STORE(_name_, _index_) \
	static ssize_t kio_config_##_name_##_show(struct kobject *kobj, \
		struct kobj_attribute *attr, char *buf) \
	{ \
		return kio_thread_var_show(kobj, attr, buf, _index_); \
	} \
	static ssize_t kio_config_##_name_##_store(struct kobject *kobj, \
		struct kobj_attribute *attr, const char *buf, size_t count) \
	{ \
		return kio_thread_var_store(kobj, attr, buf, count, _index_); \
	} \
	static struct kobj_attribute kio_config_##_name_##_attribute \
		= __ATTR(_name_, 0664, kio_config_##_name_##_show, \
			 kio_config_##_name_##_store)

VAR_ATTR_SHOW_STORE(block_size, KIO_BLOCK_SIZE);
VAR_ATTR_SHOW_STORE(offset_random, KIO_OFFSET_RANDOM);
VAR_ATTR_SHOW_STORE(offset_low, KIO_OFFSET_LOW);
VAR_ATTR_SHOW_STORE(offset_high, KIO_OFFSET_HIGH);
VAR_ATTR_SHOW_STORE(read_mix_percent, KIO_READ_MIX_PERCENT);
VAR_ATTR_SHOW_STORE(read_burst, KIO_READ_BURST);
VAR_ATTR_SHOW_STORE(write_burst, KIO_WRITE_BURST);

#undef VAR_ATTR_SHOW_STORE

// ------------------------------------------------------------------------

static int kio_config_create_thread(unsigned tid)
{
	struct kio_thread_config *ktc = &kio_config.threads[tid];
	char name[20];
	int retval;

	sprintf(name, "%d", tid);
	ktc->kobj = kobject_create_and_add(name, kio_kobj);
	if (!ktc->kobj)
		return -ENOMEM;

#define VAR_CREATE_FILE(_name_) \
	retval = sysfs_create_file(ktc->kobj, &kio_config_##_name_##_attribute.attr); \
	if (retval) \
		goto error; \

	VAR_CREATE_FILE(block_size);
	VAR_CREATE_FILE(offset_random);
	VAR_CREATE_FILE(offset_low);
	VAR_CREATE_FILE(offset_high);
	VAR_CREATE_FILE(read_mix_percent);
	VAR_CREATE_FILE(read_burst);
	VAR_CREATE_FILE(write_burst);

#undef VAR_CREATE_FILE

	return 0;

error:
	kobject_put(ktc->kobj);
	return retval;
}
static void kio_config_destroy_all_threads(void)
{
	int i;

	for (i = 0; i < kio_config.num_threads; i++) {
		if (kio_config.threads[i].kobj) {
			kobject_put(kio_config.threads[i].kobj);
			kio_config.threads[i].kobj = NULL;
		}
	}

	kfree(kio_config.threads);
	kio_config.threads = NULL;
}

// ------------------------------------------------------------------------

static ssize_t kio_num_threads_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", kio_config.num_threads);
}
static ssize_t kio_num_threads_store(struct kobject *kobj,
				 struct kobj_attribute *attr, const char *buf, size_t count)
{
	int result = -1, num_threads = -1, i;
	size_t threads_size;

	mutex_lock(&kio_config.mutex);

	if (kio_config.num_threads || kio_config.threads) {
		result = -EEXIST;
		goto unlock_and_return_result;
	}

	sscanf(buf, "%du", &num_threads);

	if (num_threads < 1 || num_threads > num_online_cpus()) {
		result = -EINVAL;
		goto unlock_and_return_result;
	}

	threads_size = num_threads * sizeof(*kio_config.threads);
	kio_config.threads = kzalloc(threads_size, GFP_KERNEL);
	if (!kio_config.threads) {
		result = -ENOMEM;
		goto unlock_and_return_result;
	}
	kio_config.num_threads = num_threads;

	for (i=0; i<num_threads; i++) {
		result = kio_config_create_thread(i);
		if (result) {
			kio_config_destroy_all_threads();
			goto unlock_and_return_result;
		}
	}

	result = count;

unlock_and_return_result:
	mutex_unlock(&kio_config.mutex);

	return result;
}

// ------------------------------------------------------------------------

static struct kobj_attribute num_threads_attribute
	= __ATTR(num_threads, 0664, kio_num_threads_show, kio_num_threads_store);

int __init kio_config_init(void)
{
	int retval;

	memset(&kio_config, 0, sizeof(kio_config));
	mutex_init(&kio_config.mutex);

	// Create a kobject for kio
	kio_kobj = kobject_create_and_add("kio", kernel_kobj);
	if (!kio_kobj)
		return -ENOMEM;

	// Create the num_threads file
	retval = sysfs_create_file(kio_kobj,
				   &num_threads_attribute.attr);
	if (retval)
		kobject_put(kio_kobj);

	return retval;
}
void __exit kio_config_exit(void)
{
	kio_config_destroy_all_threads();

	kobject_put(kio_kobj);
}
