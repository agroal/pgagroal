# pgagroal Test Suite Instructions

## Overview

This document explains how to run the pgagroal test suite, generate code coverage, and use containerized testing. The main entry point for all tests is the `testsuite.sh` script.

## Quick Start

1. **Build the Project** (with GCC for coverage):

    ```sh
    git clone https://github.com/pgagroal/pgagroal.git
    cd pgagroal
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Debug ..
    make
    ```

2. **Run the Test Suite**:

    ```sh
    ./testsuite.sh
    ```

    - This script sets up temporary PostgreSQL and pgagroal environments in the build directory, runs all tests, and produces logs and (if enabled) coverage data.

3. **Clean Up** (before re-running):

    ```sh
    ./testsuite.sh clean
    ```

4. **Run Configuration Tests** (multiple config scenarios):

    ```sh
    ./testsuite.sh run-configs
    ```

5. **Generate Coverage Reports** (if built with GCC and coverage enabled):

    ```sh
    # From inside the build directory, after running tests
    mkdir -p ./coverage
    gcovr -r ../src --object-directory . --html --html-details -o ./coverage/index.html
    gcovr -r ../src --object-directory . > ./coverage/summary.txt
    ```

6. **Containerized Testing** (Optional, requires Docker or Podman):

    - **Run all tests in a container:**
      ```sh
      ctest -V
      ```
    - **Or run and generate coverage in a container:**
      ```sh
      ./coverage.sh
      ```

## Artifacts

After running tests, you will find:

- Test logs: `build/log/`
- Coverage reports: `build/coverage/`
- CTest logs: `build/testing/`

## Adding New Test Cases

- Add new `.c` and `.h` files in `test/testcases/`.
- Register your test suite in `test/testcases/runner.c`.
- Add your test source to `test/CMakeLists.txt`:

    ```cmake
    set(SOURCES
      testcases/common.c
      testcases/your_new_test.c
      testcases/runner.c
    )
    ```

## Prerequisites

- PostgreSQL 17.x installed and available in your PATH
- The `initdb`, `pg_ctl`, and `psql` binaries in your PATH
- The `check` library installed for C unit tests
- **Docker or Podman** installed (for containerized tests)
- **gcov** and **gcovr** installed (for coverage reports)

## Notes

- Always clean up (`./testsuite.sh clean`) before re-running tests to avoid stale environments.
- If you want to test multiple configuration scenarios, add directories with `pgagroal.conf` and `pgagroal_hba.conf` under `test/conf/` and use `./testsuite.sh run-configs`.
- For more details, see the comments and logic in [`test/testsuite.sh`](../test/testsuite.sh).

---