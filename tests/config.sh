#!/bin/bash # --- Edit below configuration --- General ConfigurationLOGFILE=
PROJECT_ROOT=$(env | grep -q "GITHUB_RUN_ID" && echo "/home/runner/work/pgagroal/pgagroal" || echo "$(dirname "$(pwd)")")

# 
# benchmark.sh Configuration
#

# Machine 1: pgbench client machine
PG_BENCH_IP=
PG_BENCH_SSH_PORT=

# Machine 2: pgagroal machine (local)
PGAGROAL_IP=localhost
PGAGROAL_PORT=2345

# Machine 3: Postgres server machine
POSTGRES_IP=
POSTGRES_PORT=
POSTGRES_SSH_PORT=


# ---------- Do NOT edit below this line ----------

LOCAL_MACHINE="controller"
if [[ -z LOGFILE ]]; then
    logfile=$(mktemp --suff .pgagroal.log)
fi
EVENT_BACKENDS=("io_uring" "epoll" "kqueue")

declare -A machines
machines=(
        ["client"]="$PG_BENCH_IP::$PG_BENCH_SSH_PORT"
        ["controller"]="$PGAGROAL_IP:$PGAGROAL_PORT:"
        ["server"]="$POSTGRES_IP:$POSTGRES_PORT:$POSTGRES_SSH_PORT" 
)

SCRIPTS_DIR=scripts
CONFIGS_DIR=configs
BUILD_DIR=../build
PGAGROAL_BIN=../build/src/pgagroal
PGAGROAL_CLI_BIN=../build/src/pgagroal-cli

