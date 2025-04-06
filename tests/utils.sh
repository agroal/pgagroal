#!/bin/bash
#
# Copyright (C) 2025 the pgagroal community
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list
# of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may
# be used to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

log() {
    local log_type="$1"
    local text="$2"
    local date_time
    date_time=$(date "+%Y-%m-%d %H:%M:%S")
    local filename
    filename=$(basename "$0")

    # Color definitions
    local color_info=""
    local color_good="\033[0;32m"
    local color_warn="\033[0;33m"
    local color_error="\033[0;31m"
    local color_filename="\x1b[38;5;59m"
    local color_reset="\033[0m"

    case "$log_type" in
        INFO)
            echo -en "${color_info}[*] ${text}${color_reset}" ;;
        GOOD)
            echo -e "${color_good}${text}${color_reset}" ;;
        BAD)
            echo -e "${color_error}${text}${color_reset}" ;;
        WARN)
            echo -e "${color_warn}[!] ${text}${color_reset}" ;;
        ERROR)
            echo -e "${color_error}[E] ${text}${color_reset}" ;;
        FATAL)
            echo -e "${color_error}[F] ${text}${color_reset}" ;;
        *)
            echo -e "${date_time} ${color_filename}${filename}${color_reset}: ${text}" ;;
    esac
}

info() { 
    if [[ -z "${2:-}" ]]; then
        log INFO "$1\n"
    else
        log INFO "$1"
    fi
}
good()   { log GOOD "OK"; }
bad()    { log BAD "ERROR"; }
warn()   { log WARN "$1"; }
error()  { log ERROR "$1"; }
die()    { log FATAL "$1"; cleanup; exit 1; }

cleanup() {
    rm -rf /tmp/pgagroal.${PGAGROAL_PORT}.pid
}

# Retrieve machine info from an associative array 'machines'
get_machine_info() {
    local machine="$1"
    local value="${machines[$machine]}"
    if [ -z "$value" ]; then
        die "Machine $machine not found"
    fi
    IFS=":" read -r ip port ssh_port <<< "$value"
    echo "$ip $port $ssh_port"
}

check_ping() {
    local src="$1"
    local dest="$2"
    read -r src_ip _ <<< "$(get_machine_info "$src")"
    read -r dest_ip _ <<< "$(get_machine_info "$dest")"

    local cmd="ping -c 4 $dest_ip"
    
    info "Checking ping from $src to $dest ($dest_ip) "
    
    if [ "$LOCAL_MACHINE" == "$src" ]; then
        info "Local ping from $src:"
        eval "$cmd"
    else
        info "Remote ping from $src via SSH:"
        ssh_cmd "$src" "$cmd"
    fi
}

check_ssh() {
    local ip="$1"
    local port="$2"
    local desc="$3"

    run_test "ssh -p $port -o BatchMode=yes -o ConnectTimeout=5 $ip \"echo SSH_OK\" 2>/dev/null | grep -q SSH_OK" \
             "Testing SSH connectivity to $desc ($ip) on port $port " \
             "SSH connection to $desc ($ip) on port $port failed. Exiting."
}

ssh_cmd() {
    local machine="$1"
    local cmd="$2"
    read -r ip port ssh_port <<< "$(get_machine_info "$machine")"
    
    if [ "$LOCAL_MACHINE" == "$machine" ]; then
        info "Running command locally on $machine: $cmd"
        eval "$cmd"
    else
        info "Running command on $machine ($ip:$port) via SSH: $cmd"
        ssh -p "$port" "$ip" "$cmd"
    fi
}

# Unused
#
# setup_machine() {
#     info "Setting up huge pages, nmi_watchdog, and perf_event_paranoid."
#     sudo bash -c "echo 1024 > /proc/sys/vm/nr_hugepages"
#     sudo bash -c "echo 0 > /proc/sys/kernel/nmi_watchdog"
#     sudo bash -c "echo -1 > /proc/sys/kernel/perf_event_paranoid"
# }
#
setup_postgres() {
    info "Setting up Postgres"

    sudo systemctl start postgresql || exit 1
    pg_isready -h 127.0.0.1 | grep '5432' > /dev/null || (echo "Nothing is listening on 127.0.0.1:5432"; exit 1)
    pg_isready -h ::1 | grep '5432' > /dev/null || (echo "Nothing is listening on ::1:5432"; exit 1)
    sudo -u postgres psql -c "ALTER USER postgres WITH PASSWORD 'postgres';" > /dev/null

    export PGPASSWORD='postgres'
    psql -U postgres -h 127.0.0.1 -c "ALTER SYSTEM SET max_connections = '200';" > /dev/null
    sudo systemctl restart postgresql

    scale=1
    pgbench -i -s $scale -n -h 127.0.0.1 -p 5432 -U postgres -d postgres > /dev/null 2>/dev/null

}

build_pgagroal() {
    local build_type="$1"
    if [[ -z "$build_type" ]]; then
        build_type="Debug"
    fi

    if [[ -z "${compiler_id:-}" ]]; then
        compiler_id="GNU"
    fi

    info "Building pgagroal with build type: $build_type and compiler: $compiler_id"
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR" > /dev/null
    rm -rf ./*
    cmake .. -DCMAKE_BUILD_TYPE="$build_type" -DCMAKE_C_COMPILER_ID="$compiler_id"
    make -j"$(nproc)"
    popd > /dev/null
}

run_test() {
    cmd=$1
    descr=$2
    err=$3

    info "$descr" "-n"
    if ! eval "$cmd" > /dev/null 2>/tmp/err.log; then
        bad
        cat /tmp/err.log
        die "$err"
    fi
    good
}

start_pgagroal() {
    local config_dir="$1"

    # Enable io_uring if supported
    sudo sysctl kernel.io_uring_disabled=0 > /dev/null

    # debug "Executing: $PGAGROAL_BIN -c $config_dir/pgagroal.conf -a $config_dir/pgagroal_hba.conf -l $config_dir/pgagroal_databases.conf -u $config_dir/pgagroal_users.conf -d"

    run_test "$PGAGROAL_BIN -c $config_dir/pgagroal.conf -a $config_dir/pgagroal_hba.conf -d" \
             "Starting pgagroal with configuration from: $config_dir " \
             "Startup failed"

    # Allow time for pgagroal to start up
    sleep 2
}

shutdown_pgagroal() {
    config_dir=$1
    run_test "$PGAGROAL_CLI_BIN -c ${config_dir}/pgagroal.conf shutdown immediate" \
             "Shutting down pgagroal with pgagroal-cli " \
             "Shutdown failed"
}

test_startup() {
    config_dir=$1

    run_test "netstat -tuln | grep '127.0.0.1:$PGAGROAL_PORT'" \
             "Confirming pgagroal is listening on port $PGAGROAL_PORT (IPv4) " \
             "Nothing is listening on 127.0.0.1:$PGAGROAL_PORT"

    run_test "$PGAGROAL_CLI_BIN -c ${config_dir}/pgagroal.conf ping" \
             "Running pgagroal-cli ping " \
             "pgagroal-cli ping failed"

    run_test "PGPASSWORD='postgres' pg_isready -h 127.0.0.1 -p $PGAGROAL_PORT -U postgres -d postgres" \
             "Confirming pgagroal backend readiness (IPv4) " \
             "Backend not ready on 127.0.0.1:$PGAGROAL_PORT"

    run_test "PGPASSWORD='postgres' pg_isready -h ::1 -p $PGAGROAL_PORT -U postgres -d postgres" \
             "Confirming pgagroal backend readiness (IPv6) " \
             "Backend not ready on ::1:$PGAGROAL_PORT"
}

test_shutdown() {
     run_test "! pgrep pgagroal" \
              "Confirming there are no dangling pgagroal processes " \
              "Dangling child processes: $(pgrep -c pgagroal)\n$(pgrep -a pgagroal)"

     run_test "[ ! -f /tmp/pgagroal.$PGAGROAL_PORT.pid ]" \
              "Checking if PID file was correctly removed " \
              "PID file still exists"
}

