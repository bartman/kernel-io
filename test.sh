#!/bin/bash
set -e

read() {
        local val=$(cat /sys/kernel/kio/"$1")
        echo >&2 "# $1 -> $val"
        echo -n $val
}
write() {
        echo >&2 "# $2 -> $1"
        sudo tee /sys/kernel/kio/"$1" <<<"$2" >/dev/null
}

sync

if [ $(read num_threads) = 0 ] ; then
        write num_threads 1
fi

echo ------------------------------------------------------------------------

write runtime_seconds 5

write 0/offset_low        0
write 0/offset_high       0xFFFFFFFF

write 0/block_size        4096
write 0/offset_stride     4096          # advance offset by this amount

write 0/queue_depth       16

write 0/offset_random     0
write 0/read_mix_percent  100

# how often to switch directions / apply burst delay / burst finish
write 0/read_burst        128
write 0/write_burst       128

# 
write 0/burst_delay       0
write 0/burst_finish      0

# sleep after each IO or burst (burst_delay)
write 0/read_sleep_usec   0
write 0/write_sleep_usec  0

write run_workload 1

echo ------------------------------------------------------------------------

grep . /sys/module/kio/parameters/* $(find /sys/kernel/kio/ -type f | sort) | column -t -s:
