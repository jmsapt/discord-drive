#!/bin/bash

PORT=10809

nbd-client -d /dev/nbd0 > /dev/null

export TOKEN=$(cat SECRET)

nbdkit -p $PORT -fvv ./bazel-bin/src/libdrive.so channels=1
# sudo nbd-client 127.0.0.1 $PORT -b 4096 /dev/nbd0
# wait
