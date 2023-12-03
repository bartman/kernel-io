#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/log2.h>

#include "kio_config.h"
#include "kio_run.h"

static struct kobject *kio_kobj;
static struct kio_config kio_config = {};

// ------------------------------------------------------------------------

#define CHECK_VAR(_name,_fmt,_min,_max) \
({ \
	typeof(kio_config._name) \
		val = kio_config._name, \
		min = _min, max = _max; \
	if (val < min || val > max) { \
		pr_warn("kio: %s value " _fmt \
			" is out of range [" _fmt "," _fmt "]\n", \
			__stringify(_name), val, min, max); \
		return false; \
	} \
})

#define CHECK_THRD_VAR(_index,_name,_fmt,_min,_max) \
({ \
	typeof(kio_config.threads[_index]._name) \
		val = kio_config.threads[_index]._name, \
		min = (_min), max = (_max); \
	if (val < min || val > max) { \
		pr_warn("kio: thread %u %s value " _fmt \
			" is out of range [" _fmt "," _fmt "]\n", \
			_index, __stringify(_name), val, min, max); \
		return false; \
	} \
})

static bool kio_config_is_valid(void)
{
	int i;

	CHECK_VAR(num_threads, "%d", 1, num_online_cpus());
	CHECK_VAR(runtime_seconds, "%d", 1, KIO_MAX_RUNTIME_SECONDS);

	for (i=0; i<kio_config.num_threads; i++) {

		/* range checks */

		CHECK_THRD_VAR(i, block_size, "%d", 512, 1<<20);
		CHECK_THRD_VAR(i, queue_depth, "%d", 1, 1024);
		CHECK_THRD_VAR(i, offset_low, "%ld", 0, LONG_MAX);
		CHECK_THRD_VAR(i, offset_high, "%ld", 0, LONG_MAX);
		CHECK_THRD_VAR(i, read_mix_percent, "%d", 0, 100);
		CHECK_THRD_VAR(i, read_burst, "%d", 0, 1024);
		CHECK_THRD_VAR(i, write_burst, "%d", 0, 1024);
		CHECK_THRD_VAR(i, read_sleep_usec, "%d", 0, 100000);
		CHECK_THRD_VAR(i, write_sleep_usec, "%d", 0, 100000);

		/* block size is a power of 2 */

		if (!is_power_of_2(kio_config.threads[i].block_size)) {
			pr_warn("kio: thread %u block_size value %u "
				"must be a power of 2\n",
				i, kio_config.threads[i].block_size);
			return false;
		}

		/* offset_low is less than offset_high */

		if (kio_config.threads[i].offset_low >=
		    kio_config.threads[i].offset_high) {
			pr_warn("kio: thread %u offset_low %lu must be "
				"smaller than offset_high %lu\n",
				i, kio_config.threads[i].offset_low,
				kio_config.threads[i].offset_high);
			return false;
		}

		/* either read_burst or write_burst are set */

		if (!kio_config.threads[i].read_burst
		    && !kio_config.threads[i].write_burst) {
			pr_warn("kio: thread %u has neither read_burst nor "
				"write_burst set\n", i);
			return false;
		}

		/* for read-only workload read_burst must be set */

		if (kio_config.threads[i].read_mix_percent == 100
		    && !kio_config.threads[i].read_burst) {
			pr_warn("kio: thread %u has is read-only but does "
				"not set read_burst\n", i);
			return false;
		}

		/* for write-only workload write_burst must be set */

		if (kio_config.threads[i].read_mix_percent == 0
		    && !kio_config.threads[i].write_burst) {
			pr_warn("kio: thread %u has is write-only but does "
				"not set write_burst\n", i);
			return false;
		}
	}

	return true;
}

#undef CHECK_VAR
#undef CHECK_THRD_VAR

// ------------------------------------------------------------------------

enum {
	KIO_BLOCK_SIZE,
	KIO_QUEUE_DEPTH,
	KIO_OFFSET_RANDOM,
	KIO_OFFSET_LOW,
	KIO_OFFSET_HIGH,
	KIO_READ_MIX_PERCENT,
	KIO_READ_BURST,
	KIO_WRITE_BURST,
	KIO_READ_SLEEP_USEC,
	KIO_WRITE_SLEEP_USEC,
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
	case KIO_QUEUE_DEPTH:      value = ktc->queue_depth;      break;
	case KIO_OFFSET_RANDOM:    value = ktc->offset_random;    break;
	case KIO_OFFSET_LOW:       value = ktc->offset_low;       break;
	case KIO_OFFSET_HIGH:      value = ktc->offset_high;      break;
	case KIO_READ_MIX_PERCENT: value = ktc->read_mix_percent; break;
	case KIO_READ_BURST:       value = ktc->read_burst;       break;
	case KIO_WRITE_BURST:      value = ktc->write_burst;      break;
	case KIO_READ_SLEEP_USEC:  value = ktc->read_sleep_usec;  break;
	case KIO_WRITE_SLEEP_USEC: value = ktc->write_sleep_usec; break;
	default: return -ENOENT;
	}
	return sprintf(buf, "%ld\n", value);
}

static ssize_t kio_thread_var_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count, int var_index)
{
	struct kio_thread_config *ktc;
	long value = -1;
	int rc;

	if (kio_is_running())
		return -EBUSY;

	ktc = kio_thread_config_from_kobj(kobj);
	if (!ktc)
		return -ENODEV;

	rc = kstrtol(buf, 0, &value);
	if (rc<0)
		return rc;

	switch (var_index) {
	case KIO_BLOCK_SIZE:       ktc->block_size       = value; break;
	case KIO_QUEUE_DEPTH:      ktc->queue_depth      = value; break;
	case KIO_OFFSET_RANDOM:    ktc->offset_random    = value; break;
	case KIO_OFFSET_LOW:       ktc->offset_low       = value; break;
	case KIO_OFFSET_HIGH:      ktc->offset_high      = value; break;
	case KIO_READ_MIX_PERCENT: ktc->read_mix_percent = value; break;
	case KIO_READ_BURST:       ktc->read_burst       = value; break;
	case KIO_WRITE_BURST:      ktc->write_burst      = value; break;
	case KIO_READ_SLEEP_USEC:  ktc->read_sleep_usec  = value; break;
	case KIO_WRITE_SLEEP_USEC: ktc->write_sleep_usec = value; break;
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
VAR_ATTR_SHOW_STORE(queue_depth, KIO_QUEUE_DEPTH);
VAR_ATTR_SHOW_STORE(offset_random, KIO_OFFSET_RANDOM);
VAR_ATTR_SHOW_STORE(offset_low, KIO_OFFSET_LOW);
VAR_ATTR_SHOW_STORE(offset_high, KIO_OFFSET_HIGH);
VAR_ATTR_SHOW_STORE(read_mix_percent, KIO_READ_MIX_PERCENT);
VAR_ATTR_SHOW_STORE(read_burst, KIO_READ_BURST);
VAR_ATTR_SHOW_STORE(write_burst, KIO_WRITE_BURST);
VAR_ATTR_SHOW_STORE(read_sleep_usec, KIO_READ_SLEEP_USEC);
VAR_ATTR_SHOW_STORE(write_sleep_usec, KIO_WRITE_SLEEP_USEC);

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
	VAR_CREATE_FILE(queue_depth);
	VAR_CREATE_FILE(offset_random);
	VAR_CREATE_FILE(offset_low);
	VAR_CREATE_FILE(offset_high);
	VAR_CREATE_FILE(read_mix_percent);
	VAR_CREATE_FILE(read_burst);
	VAR_CREATE_FILE(write_burst);
	VAR_CREATE_FILE(read_sleep_usec);
	VAR_CREATE_FILE(write_sleep_usec);

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

	if (kio_is_running()) {
		result = -EBUSY;
		goto unlock_and_return_result;
	}

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

static struct kobj_attribute num_threads_attribute
	= __ATTR(num_threads, 0664, kio_num_threads_show, kio_num_threads_store);

// ------------------------------------------------------------------------

static ssize_t kio_runtime_seconds_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", kio_config.runtime_seconds);
}
static ssize_t kio_runtime_seconds_store(struct kobject *kobj,
				 struct kobj_attribute *attr, const char *buf, size_t count)
{
	int result = -1;
	int seconds = -1;

	mutex_lock(&kio_config.mutex);

	if (kio_is_running()) {
		result = -EBUSY;
		goto unlock_and_return_result;
	}

	sscanf(buf, "%du", &seconds);

	if (seconds<1 || seconds>KIO_MAX_RUNTIME_SECONDS) {
		result = -EOVERFLOW;
		goto unlock_and_return_result;
	}

	kio_config.runtime_seconds = seconds;
	result = count;

unlock_and_return_result:
	mutex_unlock(&kio_config.mutex);

	return result;
}

static struct kobj_attribute runtime_seconds_attribute
	= __ATTR(runtime_seconds, 0664, kio_runtime_seconds_show, kio_runtime_seconds_store);

// ------------------------------------------------------------------------

static ssize_t kio_run_workload_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", kio_is_running());
}
static ssize_t kio_run_workload_store(struct kobject *kobj,
				 struct kobj_attribute *attr, const char *buf, size_t count)
{
	int result = -1;

	mutex_lock(&kio_config.mutex);

	if (kio_is_running()) {
		result = -EBUSY;
		goto unlock_and_return_result;
	}

	if (!kio_config.num_threads || !kio_config.threads) {
		result = -ECHILD;
		goto unlock_and_return_result;
	}

	if (!kio_config_is_valid()) {
		result = -ENOEXEC;
		goto unlock_and_return_result;
	}

	kio_run(&kio_config);

	result = count;

unlock_and_return_result:
	mutex_unlock(&kio_config.mutex);

	return result;
}

static struct kobj_attribute run_workload_attribute
	= __ATTR(run_workload, 0664, kio_run_workload_show, kio_run_workload_store);

// ------------------------------------------------------------------------

int kio_config_init(void)
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
		goto err_num_threads;

	// Create the runtime_seconds file
	retval = sysfs_create_file(kio_kobj,
				   &runtime_seconds_attribute.attr);
	if (retval)
		goto err_runtime_seconds;

	// Create the run_workload file
	retval = sysfs_create_file(kio_kobj,
				   &run_workload_attribute.attr);
	if (retval)
		goto err_run_workload;

	return 0;

err_run_workload:
err_runtime_seconds:
err_num_threads:
	kobject_put(kio_kobj);
	return retval;
}
void kio_config_exit(void)
{
	kio_config_destroy_all_threads();

	kobject_put(kio_kobj);
}
