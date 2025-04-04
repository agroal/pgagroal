#!/bin/bash
#
# stress.sh
#
# Stress tests to evaluate the pooler's performance under different connection
# and transaction loads. These tests aim to stress the network pooler rather than
# the backend database.
#

source utils.sh

run_test "PGPASSWORD=postgres pgbench -c 50 -j 4 -h localhost -p 2345 -U postgres -d postgres" \
         "Stress Test 1: 50 clients" \
         "Test failed"
         
# run_test "PGPASSWORD=postgres pgbench -c 100 -j 4 -h localhost -p 2345 -U postgres -d postgres" \
#         "Stress Test 2: 100 clients (high concurrency)" \
#         "Test failed"
# #
# run_test "PGPASSWORD=postgres pgbench -c 200 -j $(nproc) -t 10 -h localhost -p 2345 -U postgres -d postgres" \
#          "Stress Test 2: 200 clients, 10 transactions each (high concurrency)" \
#          "Test failed"
# sleep 5
# 
# run_test "PGPASSWORD=postgres pgbench -c 100 -j $(nproc) -t 100 -h localhost -p 2345 -U postgres -d postgres" \
#          "Stress Test 3: 100 clients, 100 transactions each (moderate per-client load)" \
#          "Test failed"
# sleep 5
# 
# run_test "PGPASSWORD=postgres pgbench -c 200 -j $(nproc) -t 100 -h localhost -p 2345 -U postgres -d postgres" \
#          "Stress Test 4: 200 clients, 100 transactions each (increased concurrency and load)" \
#          "Test failed"
# sleep 5
# 
# # Test 6: Extreme connection churn test with 200 clients executing 1000 transactions each.
# # The -C flag forces pgbench to reconnect for each transaction, stressing the pooler's connection management.
# run_test "PGPASSWORD=postgres pgbench -c 200 -j $(nproc) -t 10 -C -h localhost -p 2345 -U postgres -d postgres" \
#          "Stress Test 5: 200 clients, 10 transactions each with -C (connection churn stress)" \
#          "Test failed"
