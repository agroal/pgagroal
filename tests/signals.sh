#!/bin/bash

# COPYRIGHT NOTICE
#
#

source utils.sh

for ev_backend in ${EVENT_BACKENDS[@]}; do
        run_pgagroal $ev_backend
        for signal in ${SIGNALS[@]}; do
                pid=$(pgrep -o pgagroal)
                echo "kill -$signal $pid"
        done
done
