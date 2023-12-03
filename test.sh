#!/bin/bash
set -e

write() {
        echo >&2 "# $2 -> $1"
        sudo tee /sys/kernel/kio/"$1" <<<"$2" >/dev/null
}

sync

write num_threads 1
write runtime_seconds 5

write 0/block_size        4096
write 0/offset_low        0
write 0/offset_high       -1
write 0/offset_random     1
write 0/read_mix_percent  50
write 0/read_burst        1
write 0/write_burst       1

write run_workload 1
