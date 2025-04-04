# pgagroal Test Suite

The tests are designed to validate the functionality and performance of pgagroal under different configurations. It automates building pgagroal, running tests, and managing the server lifecycle.

*DISCLAIMER*: for now, the tests are designed to work on CI only -- Linux only. Also, it may or may not work on your Linux system.

## Structure

`run`: The main script to execute tests with one or more configurations.

`config.sh`: Contains general configuration (ip, ports, etc) that should be filled before executing the tests.

`configs/`: Contains configuration directories and each directory holds configuration files that defines a specific test environment.

`scripts/`: Contains test scripts that run various tests against pgagroal. For example, "simple.sh" tests query execution.

`utils.sh`: Utility functions for logging, building, and managing pgagroal.

## Available Configurations

Each configuration contains the files: 

```txt
pgagroal.conf
pgagroal_databases.conf
pgagroal_hba.conf
pgagroal_users.conf
```

The test suite currently includes the following configurations under the `configs/` directory:

#### 01

Event Backend: io_uring

Pipeline: performance

#### 02

Event Backend: epoll

Pipeline: performance

### 03

Event Backend: io_uring

Pipeline: session

### 04

Event Backend: epoll

Pipeline: session

### 05 (Work in progress)

Event Backend: io_uring

Pipeline: transaction

### 06 (Work in progress)

Event Backend: epoll

Pipeline: transaction

## Test Scripts

The test scripts are located in the `scripts/` directory. Currently, the following test script is available:

```txt
simple.sh
benchmark.sh  # work in progress
```

To add a test script just add it to the script directory.

## Running the Tests

Use `run` script to run tests with the desired configurations and test scripts.

Run `run -h` for usage.

This command will: (a) Compile pgagroal (if not already built); (b) Start pgagroal using each specified configuration; (c) Run the a test script; and (d) Shut down pgagroal after each test run.

The startup and shutdown of pgagroal are also tested to check that pgagroal is ready before the test if shutdown was successful after the test.

## Environment Setup

Before running tests, ensure that:

The `config.sh` file is configured with the necessary environment variables (e.g., `PROJECT_ROOT`, `PG_BENCH_IP`, `PGAGROAL_IP`, `PGAGROAL_PORT`, etc.).

Required tools and dependencies (like psql, pg_isready, and others) are installed and available on your system.


## TODO

- Implement configs 05 and 06
- Finish the `benchmark.sh` script and add metrics
- Add possibility to test on FreeBSD
