\newpage

## Code Coverage

### Overview

Code coverage helps you understand which parts of the codebase are exercised by your tests. This project supports both local and containerized coverage workflows.

### When is Coverage Available?

- Coverage is only available if you **build the project with GCC** and have both `gcov` and `gcovr` installed.
- If you use Clang, coverage reports will **not** be generated.
- Coverage is enabled automatically during the build if the requirements are met.

### How to Generate Coverage Reports

**1. Run the Test Suite***

First, run the test suite to generate coverage data. From your `build` directory:

```sh
./testsuite.sh
```

- This script sets up temporary PostgreSQL and pgagroal environments, runs all tests, and produces coverage data if enabled.

**2. Generate Coverage Reports (Local)**

After running the tests, generate the coverage reports:

```sh
# Run these commands from inside the build directory
mkdir -p ./coverage

gcovr -r ../src --object-directory . --html --html-details -o ./coverage/index.html
gcovr -r ../src --object-directory . > ./coverage/summary.txt
```

- The HTML report will be available at `build/coverage/index.html`
- A summary text report will be available at `build/coverage/summary.txt`

> **Note:** If the `coverage` directory does not exist, create it first using `mkdir -p ./coverage`.
>  
> **Important:** `gcovr` only works with GCC builds.

**3. Containerized Coverage (Optional)**

If you have **Docker** or **Podman** installed, you can run tests and generate coverage in a container for a clean, isolated environment.

You have two options:

a. Using CTest

```sh
ctest -V
```

This will run all tests in a container if configured.

b. Using the Coverage Script

```sh
./coverage.sh
```

This script will:
- Build and run the tests in a container
- Generate coverage reports automatically in `build/coverage/`
- Copy logs and coverage data back to your host

### Summary Table

| Task                        | Command(s)                        | Prerequisites                |
|-----------------------------|-----------------------------------|------------------------------|
| Run tests locally           | `./testsuite.sh`                  | Built with GCC, PostgreSQL   |
| Generate coverage locally   | See commands above                | `gcov`, `gcovr` installed    |
| Run containerized tests     | `ctest -V` or `./coverage.sh`     | Docker or Podman installed   |

### Notes

- Always run the coverage commands from the `build` directory.
- If coverage tools are not found, or the compiler is not GCC, coverage generation will be skipped and a message will be shown.
- You can always re-run the coverage commands manually if needed.

---
