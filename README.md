# About

This is a kernel-based IO benchmark.

KIO is similar (in spirit) to `fio` but runs entierly in the kernel.  This
removes any user space and queuing overhead, and measures just the device
driver + hardware.

This code is provided under the GPLv2 license.

## TL;DR

Quick run:
```
$ make
$ kio.py --num-threads 1 \
        --runtime 5 \
        --block-size 4096 \
        --offset-stride 4096 \
        --queue-depth 10 \
        --offset-random 1 \
        --read-mix-percent 100 \
        --offset-high $((0xFFFFFFFF)) \
        --read-burst 100 \
        --read-mix-percent 100
...
-   bw_MBps: 299.211
    clat_usec: 129.876
    completed: 373186
    iops: 73049.0
    lat_usec: 133.03
    slat_usec: 3.154
...
```

# Usage

## Build

To build the system must have a compiler tools and kernel headers.

```
$ ./dependencies.sh
$ make
```

## Running kio.py

This is a helper script that manages configuration files and generates
reports.

```
$ ./kio.py --num-threads 2 \
        --runtime 5 \
        --block-size 4096 \
        --queue-depth 10 \
        --offset-random 1 \
        --read-mix-percent 100 \
        --offset-high $((0xFFFFFFFF)) \
        --read-burst 100 \
        --read-mix-percent 100
...
-   bw_MBps: 159.743
    clat_usec: 250.313
    completed: 398666
    iops: 19499.0
    lat_usec: 253.18
    slat_usec: 2.867
-   0:
        bw_MBps: 159.734
        clat_usec: 249.932
        completed: 199319
        iops: 38997.0
        lat_usec: 252.695
        slat_usec: 2.763
    1:
        bw_MBps: 159.752
        clat_usec: 250.695
        completed: 199347
        iops: 39002.0
        lat_usec: 253.666
        slat_usec: 2.971
```

It is also possible to generate a config, edit it, and then run from the config.

```
$ ./kio.py --num-threads 2 --runtime 5 --generate-config kio.conf
$ vim kio.conf
$ ./kio.py --config kio.conf
```

kio.py can generate reports in YAML format:
```
$ ./kio.py --config kio.conf --output-yaml report.yaml
```

Lastly, kio.py can be made to append to the same CVS file:
```
$ ./kio.py --config kio.conf --read-mix-percent 100 --output-csv report.yaml
$ ./kio.py --config kio.conf --read-mix-percent 50 --output-csv report.yaml
$ ./kio.py --config kio.conf --read-mix-percent 0 --output-csv report.yaml
```

## Running manually

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
