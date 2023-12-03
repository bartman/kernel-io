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

write runtime_seconds 5

write 0/block_size        4096
write 0/queue_depth       1
write 0/offset_low        0
write 0/offset_high       0xFFFFFFFF
write 0/offset_random     1
write 0/read_mix_percent  50
write 0/read_burst        1
write 0/write_burst       1

write run_workload 1
