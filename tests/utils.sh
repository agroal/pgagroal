#!/bin/bash

LOCAL_RUN=$(env | grep "GITHUB_RUN_ID"; echo $?) # 0 if running in github actions
if [[ $LOCAL_RUN == 1 ]]; then
        PROJECT_ROOT=$(dirname `pwd`)
else
        PROJECT_ROOT=/home/runner/work/pgagroal/pgagroal
fi
EVENT_BACKENDS=(io_uring epoll kqueue)
SIGNALS=(
        SIGTERM "pgagroal: shutdown"
        # SIGHUP ""
        SIGINT  "pgagroal: shutdown"
        SIGTRAP "pgagroal: shutdown"
        SIGABRT "pgagroal: core dump requested" # NOTE: this is failing to delete pid file
        SIGALRM "pgagroal: shutdown"
        # SIGCHLD ""
)
LOGFILE=$(mktemp --suff .pgagroal.log)

function log() {
    local log_type=$1
    local text=$2
    local flags=$3
    local color_info=""
    local color_good="\033[0;32m"
    local color_error="\033[0;31m"
    local color_filename="\x1b[38;5;59m"
    local color_reset="\033[0m"
    local date_time=$(date "+%Y-%m-%d %H:%M:%S")
    local filename=$(basename "$0")

    if [ "$log_type" = "INFO" ]; then
        echo -e "$flags ${color_info}${text}${color_reset}"
    elif [ "$log_type" = "ERROR" ]; then
        echo -e "$flags ${color_error}${text}${color_reset}"
    elif [ "$log_type" = "GOOD" ]; then
        echo -e "$flags ${color_good}${text}${color_reset}"
    else
        echo -e "$flags ${date_time} ${color_filename}${filename}${color_reset}: $text"
    fi
}

function info {
        log "INFO" "${msg}"
}

function bad {
        msg=$1
        log "ERROR" "${msg}"
}

function good {
        msg=$1
        log "GOOD" "${msg}"
}

function die {
        msg=$1
        log "ERROR" "${msg}"
        exit 1
}

function vm_setup {
        info "Setting up huge pages, nmi_watchdog and perf_event_paranoid."
        sudo bash -c "echo 1024 > /proc/sys/vm/nr_hugepages"
        sudo bash -c "echo 0 > /proc/sys/kernel/nmi_watchdog"
        sudo bash -c "echo -1 > /proc/sys/kernel/perf_event_paranoid"
}

function postgres_setup {
        info "Not implemented yet."
}

function pgagroal_setup {
        ev_backend=$1
        sudo mkdir -p /etc/pgagroal
        crudini --set ../../doc/etc/pgagroal.conf pgagroal log_type file
        crudini --set ../../doc/etc/pgagroal.conf pgagroal log_path $LOGFILE
        crudini --set ../../doc/etc/pgagroal.conf pgagroal ev_backend "$ev_backend"
        echo "host all all all trust" > ../../doc/etc/pgagroal_hba.conf
        sudo cp "$PROJECT_ROOT/doc/etc/*.conf" /etc/pgagroal
}

function build_pgagroal {
        system=$(uname)

        build_type=$1
        compiler_id=$2

        if [[ -z $build_type ]]; then
          type=Debug
        fi

        if [[ -z $compiler_id ]]; then
          type=GNU
        fi

        build_dir="$PROJECT_ROOT/build"
        mkdir -p $build_dir
        cd $build_dir
        rm -rf *
        cmake .. -DCMAKE_BUILD_TYPE=$type -DCMAKE_C_COMPILER_ID=$cid
        make -j$(nproc)
}

function run_pgagroal {
        ev_backend=$1
        pgagroal_setup $ev_backend
        sudo sysctl kernel.io_uring_disabled=0
        pgagroal -d
        verify_running
}

function verify_running {
          info "Verifying pgagroal is running and listening on port 2345"
          netstat -tuln | grep "127.0.0.1:2345" || die "Nothing is listening on 127.0.0.1:2345"
          netstat -tuln | grep "::1:2345" || die "Nothing is listening on ::1:2345"
          info "Running pgagroal-cli ping"
          ./pgagroal-cli ping

}

function verify_shutdown {
          info "Running pgagroal-cli shutdown immediate"
          ./pgagroal-cli shutdown immediate
          sleep 5
          info "Confirming there are no dangling pgagroal processes"
          pgrep pgagroal > /dev/null && die "Dangling pgagroal child processes: $(wc -l < <(pgrep pgagroal))"
          info "Checking for dangling PID file"
          rm -f /tmp/pgagroal.2345.pid && die "Dangling PID file"
}

function simple_queries_test {
          info "Testing simple queries with psql..." "-n"
          PGPASSWORD="postgres" psql -h 127.0.0.1 -p 2345 -U postgres -d postgres -c "SELECT * FROM pgbench_accounts LIMIT 50;" > /dev/null || die "psql error"
          PGPASSWORD="postgres" psql -h ::1 -p 2345 -U postgres -d postgres -c "SELECT * FROM pgbench_accounts LIMIT 50;" > /dev/null || die "psql error"
          good "Passed!"
}

function signals_test {
        info "Running signals test..."
        for ev_backend in ${EVENT_BACKENDS[@]}; do
                run_pgagroal $ev_backend debug
                for ((i=0; i<${#SIGNALS[@]}; i+=2)); do
                        signal="${SIGNALS[i]}"
                        expected_result="${SIGNALS[i+1]}"
                        info "Testing $signal... " "-n"
                        pid=$(pgrep -o pgagroal)
                        echo "kill -$signal $pid"
                        kill -$signal $pid
                        tail -n10 $LOGFILE | grep result || exit 1
                        verify_shutdown
                        if [[ $? == 0 ]]; then
                                good "Passed!"
                        else
                                die "Expected result '$expected_result' not found in logs"
                        fi
                done
        done
}

function cli_commands_test {
        info "Not implemented yet!"
}

performance_test() {
    info "Running performance test"

    for nr in 5 20 50 90; do
        pgbench -c "$nr" \
                -j "$(nproc)" \
                -h localhost \
                -U pgagroal_user \
                -T 60 \
                -p 2345 \
                pgagroal_database
    done

    info "Done."
}


