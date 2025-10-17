\newpage

## Test Suite

### Overview

This document explains how to run the pgagroal test suite, generate code coverage, and use containerized testing. All testing is now performed using the `check.sh` script with containerized PostgreSQL (recommended and default for all development and CI).

**Running Specific Test Cases or Suites**

You can run a specific test case or suite using the following environment variables:

- `CK_RUN_CASE=<test_case_name> ./check.sh` — runs a single test case
- `CK_RUN_SUITE=<test_suite_name> ./check.sh` — runs a single test suite

Alternatively, you can export the environment variable before running the script:

```sh
export CK_RUN_CASE=<test_case_name>
./check.sh
```

The environment variables will be automatically unset when the test is finished or aborted.

### Containerized

The `check.sh` script is the main and recommended way to run the pgagroal test suite. It works on any system with Docker or Podman (Linux, macOS, FreeBSD, Windows/WSL2). It automatically builds a PostgreSQL 17 container, sets up the test environment, runs all tests, and generates coverage reports and logs. No local PostgreSQL installation is required.

**Key Features**

- **No local PostgreSQL required**: Uses Docker/Podman containers
- **Consistent environment**: Same PostgreSQL version (17) across all systems
- **Automatic cleanup**: Containers are removed after tests
- **Integrated coverage**: Coverage reports generated automatically
- **Isolated testing**: No interference with local PostgreSQL installations
- **Multiple configurations**: Supports running tests on multiple pgagroal configurations
- **Easy setup**: `./check.sh setup` installs all dependencies and builds the PostgreSQL image
- **Flexible CI support**: Used in CI for Linux, and will be used for all platforms after migration

**Usage**

```sh
./check.sh [sub-command]
```

**Subcommands:**

- `setup`                  Install dependencies and build PostgreSQL image (one-time setup)
- `clean`                  Clean up test suite environment and remove PostgreSQL image
- `run-configs`            Run the testsuite on multiple pgagroal configurations (containerized)
- `ci`                     Run in CI mode (local PostgreSQL, no container)
- `run-configs-ci`         Run multiple configuration tests using local PostgreSQL (like ci + run-configs)
- `ci-nonbuild`            Run in CI mode (local PostgreSQL, skip build step)
- `run-configs-ci-nonbuild` Run multiple configuration tests using local PostgreSQL, skip build step
- (no sub-command)         Default: run all tests in containerized mode

> **For local development, use only the `run-configs` and default (no sub-command) modes. Other modes (`ci`, `run-configs-ci`, etc.) are intended for CI and may interfere with your local PostgreSQL setup if used locally.**

**Artifacts and Logs**

After running containerized tests, you will find:

- Test logs: `/tmp/pgagroal-test/log/`
- PostgreSQL logs: `/tmp/pgagroal-test/pg_log/`
- Coverage reports: `/tmp/pgagroal-test/coverage/`

**Adding New Test Cases**

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

**Prerequisites**

- **Docker or Podman** installed and running
- The `check` library installed for C unit tests
- **LLVM/clang** and **llvm-cov**/**llvm-profdata** installed (for coverage reports)

> **Note:** The `check.sh` script always builds the project with Clang in Debug mode for coverage and testability.

**Notes**

- The containerized approach automatically handles cleanup on exit.
- Use `./check.sh clean` to manually remove containers and test data.
- PostgreSQL container logs are available with debug5 level for troubleshooting.
- The script automatically detects and uses either Docker or Podman.
- It is recommended to **ALWAYS** run tests before raising a PR.
- Coverage reports are generated using LLVM tooling (clang, llvm-cov, llvm-profdata).
- For local development, use only the `run-configs` and default (no sub-command) modes. Other modes (`ci`, `run-configs-ci`, etc.) are intended for CI and may interfere with your local PostgreSQL setup if used locally.
