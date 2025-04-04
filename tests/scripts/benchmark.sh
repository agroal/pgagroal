#!/bin/bash
#
# benchmark.sh
#
# Script to set up a 3-machine test environment for pgagroal
#   Machine 1: client machine (pgbench)
#   Machine 2: pgagroal machine
#   Machine 3: server machine (Postgres)
#
# Everything is controlled from Machine 2
#

set -euo pipefail

# Check SSH connectivity
check_ssh client
check_ssh server

# Check connectivity between the machines
check_ping controller client 
check_ping controller server
check_ping client server

# Bandwidth Tests

echo "=== Bandwidth Tests ==="

#
## TODO ensure installed iperf3 then die if not
#

test_bandwidth() {
    local src="$1"
    local dest="$2"
    read src_ip src_port src_ssh_port <<< "$(get_machine_info "$src")"
    read dest_ip dest_port _ <<< "$(get_machine_info "$dest")"

    cmd="iperf3 -c $dest"

    info "Running bandwidth test between $src and $dest..."
    if [ "$LOCAL_MACHINE" == "$src" ]; then
        eval "$cmd"
    if 

    if command -v iperf3 &> /dev/null; then
        info "Running bandwidth test between ..."
        if ! iperf3 -c "$POSTGRES_IP"; then
            die "Bandwidth test from Proxy to Postgres failed."
        fi
        echo ""
    fi



# Test bandwidth from Proxy (Machine 2) to Postgres (Computer 3)
if command -v iperf3 &> /dev/null; then
    info "Running bandwidth test from Proxy (Machine 2) to Postgres (Computer 3)..."
    if ! iperf3 -c "$POSTGRES_IP"; then
        die "Bandwidth test from Proxy to Postgres failed."
    fi
    echo ""
fi

# Test bandwidth from Proxy (Machine 2) to pgbench (Computer 1)
if command -v iperf3 &> /dev/null; then
    echo "Running bandwidth test from Proxy (Machine 2) to pgbench (Computer 1)..."
    if ! iperf3 -c "$PG_BENCH_IP"; then
        echo "Error: Bandwidth test from Proxy to pgbench failed. Exiting."
        exit 1
    fi
    echo ""
else
    echo "iperf3 is not installed on Proxy; skipping test to pgbench."
    echo ""
fi

# Test bandwidth from pgbench (Machine 1) to Postgres (Computer 3) via SSH.
if ssh -p "$PG_BENCH_SSH_PORT" "$PG_BENCH_IP" "command -v iperf3" &> /dev/null; then
    echo "Running bandwidth test from pgbench (Machine 1) to Postgres (Computer 3)..."
    if ! ssh -p "$PG_BENCH_SSH_PORT" "$PG_BENCH_IP" "iperf3 -c $POSTGRES_IP"; then
        echo "Error: Bandwidth test from pgbench to Postgres failed. Exiting."
        exit 1
    fi
    echo ""
else
    echo "iperf3 is not installed on pgbench machine; skipping test from pgbench to Postgres."
    echo ""
fi

# --- pgbench Baseline Tests ---

echo "=== Running pgbench Baseline Tests ==="
echo "All tests will be executed on the pgbench machine via SSH."
echo ""

# Define multiple pgbench configurations.
# Format for each configuration: "label:options"
# The pgbench command will be executed with the Postgres host (-h) and port (-p) specified.
pgbench_configs=(
  "baseline:-S -c 10 -t 1000"
  "high_clients:-S -c 50 -t 1000"
  "high_transactions:-S -c 10 -t 5000"
  "combined:-S -c 50 -t 5000"
)

# Loop through each configuration and run it via SSH on Machine 1.
for config in "${pgbench_configs[@]}"; do
    label="${config%%:*}"
    options="${config#*:}"
    
    echo "Running pgbench configuration [$label] with options: $options"
    
    # Build the pgbench command; specify the Postgres host and port.
    pgbench_cmd="pgbench -h $PG_BENCH_HOST -p $POSTGRES_PORT $options"
    echo "Command: $pgbench_cmd"
    
    # Execute the command on the pgbench machine via SSH.
    if ! ssh -p "$PG_BENCH_SSH_PORT" "$PG_BENCH_IP" "$pgbench_cmd"; then
        echo "Error: pgbench configuration [$label] failed. Exiting."
        exit 1
    fi
    
    echo "Completed configuration [$label]"
    echo "-----------------------------"
done

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

echo "All benchmarks completed successfully."

