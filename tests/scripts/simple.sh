#!/bin/bash
#
# simple.sh
#
# Simple tests to verify pgagroal functionality
#

source utils.sh

run_test "PGPASSWORD=\"postgres\" psql -h 127.0.0.1 -p 2345 -U postgres -d postgres -c \"SELECT * FROM pgbench_accounts LIMIT 50;\"" "Running simple query with psql on IPv4 " "Test failed"

run_test "PGPASSWORD=\"postgres\" psql -h ::1 -p 2345 -U postgres -d postgres -c \"SELECT * FROM pgbench_accounts LIMIT 50;\"" "Running simple query with psql on IPv6 " "Test failed"

run_test "PGPASSWORD=postgres pgbench -c 1 -h localhost -p 2345 -U postgres -d postgres" "Running pgbench with 1 client " "Test failed"
