# About

This is a kernel-based IO benchmark.

KIO is similar (in spirit) to `fio` but runs entierly in the kernel.  This
removes any user space and queuing overhead, and measures just the device
driver + hardware.

## TL;DR

Quick run:
```
$ make reload
$ ./test.sh
$ sudo dmesg | tail -n1
[4930136.205920] kio: summary: completed=382413 lat=133.130(2.729+130.401) iops=73640 MB/s=301.633
```

# Usage

## Build

To build the system must have a compiler tools and kernel headers.

```
$ ./dependencies.sh
$ make
```

## Configure

The configuration allows for setting various attributes through `sysfs`.
See `test.sh` for an example.

You can have multiple threads, but you must configure each separately.

A few settings have restrictions.  See *Limitations* at the bottom.

## Results

A result (in `dmesg`) may look like this:

```
[4930136.204979] kio: thread[0]: completed=382413 lat=133.130(2.729+130.401) iops=73640 MB/s=301.633
[4930136.205920] kio: summary: completed=382413 lat=133.130(2.729+130.401) iops=73640 MB/s=301.633
```

Latencies, IOPS, and bandwidth are reported as averages.

Latency is reported in microseconds (*usec*), and is split into submission latency and completion latency.

# Limitations

* only tested on Ubuntu 18.04 kernel 4.15.0
* currently results only go to `dmesg`.
* `num_threads` can only be set once; must reload driver to change it.
* `block_size` only 4096 is currently supported.
