# CLI Test Suite

The pgagroal CLI test suite provides comprehensive testing for all `pgagroal-cli` commands and functionality.

## Overview

The CLI test suite (`test_cli_comprehensive.sh`) supports two testing modes:

### Comprehensive Mode
**Recommended for CI/CD environments**

Creates a complete isolated testing environment including PostgreSQL cluster and pgagroal instance. This mode supports two sub-modes:
- **Standard Mode**: Tests with regular connections
- **TLS Mode**: Tests with TLS-enabled connections and certificates

### Skip-Setup Mode  
**Recommended for local development**

Tests against an existing pgagroal instance without creating any infrastructure. This mode is safer for local development as it does not interfere with existing PostgreSQL instances, master keys, or configuration files.

**Caution**: While comprehensive mode can be used locally, it may interfere with existing PostgreSQL instances, master keys, and configuration files. Use comprehensive mode locally only if you understand the implications.

## Usage

### Comprehensive Mode

```bash
# Standard comprehensive testing
./test_cli_comprehensive.sh --debug

# TLS comprehensive testing  
./test_cli_comprehensive.sh --debug --tls

# Without cleanup (for debugging)
./test_cli_comprehensive.sh --debug --no-cleanup
```

**Comprehensive Mode Options:**
- `--debug` - Enable verbose debug logging
- `--tls` - Enable TLS testing mode
- `--no-cleanup` - Skip cleanup after test completion
- `-h, --help` - Show help message

### Skip-Setup Mode

```bash
# Basic skip-setup testing
./test_cli_comprehensive.sh --skip-setup -p 2347 -U admin -P password

# With custom parameters
./test_cli_comprehensive.sh --skip-setup -p 2347 -U admin -P password -d mydb -s primary --debug

# Output to file
./test_cli_comprehensive.sh --skip-setup -p 2347 -U admin -P password -F results.log
```

**Skip-Setup Mode Options:**
- `--skip-setup` - Enable skip-setup mode (required)
- `-p, --port PORT` - Management port (required)
- `-H, --host HOST` - pgagroal host (default: localhost)
- `-U, --user USER` - Admin username (default: admin)
- `-P, --password PASS` - Admin password (default: password)
- `-c, --config FILE` - Config file path (default: /etc/pgagroal/pgagroal.conf)
- `-d, --database NAME` - Database name for testing (default: postgres)
- `-s, --server NAME` - Server name for testing (default: primary)
- `--conf-param NAME` - Config parameter for testing (default: log_level)
- `--conf-value VALUE` - Config value for testing (default: info)
- `-F, --file FILE` - Output results to file
- `--debug` - Enable verbose debug logging
- `-h, --help` - Show help message

## Test Categories

The test suite covers all major CLI functionality:

- **Basic Commands**: `ping`, `status`, `status details`
- **Configuration Management**: `conf ls`, `conf get`, `conf set`, `conf reload`, `conf alias`
- **Database Management**: `enable`, `disable`
- **Connection Management**: `flush` (gracefully, idle, all)
- **Server Management**: `clear`, `switch-to`
- **Shutdown Operations**: `shutdown immediate` with cancel testing
- **Error Scenarios**: Invalid commands and parameters

## Adding New CLI Commands to Test Suite

Adding new CLI command tests follows a standard format:

### 1. Add Test Function

```bash
test_new_command() {
    log_info "=== Testing NEW Command ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        # Skip-setup mode: test against existing instance
        execute_cli_test "new command" "new-command arg1" "true" "remote"
        execute_cli_test "new command invalid" "new-command invalid" "false" "remote"
    else
        # Comprehensive mode: test both local and remote
        execute_cli_test "new command local" "new-command arg1" "true" "local"
        execute_cli_test "new command remote" "new-command arg1" "true" "remote"
        execute_cli_test "new command invalid" "new-command invalid" "false" "local"
    fi
}
```

### 2. Add to Test Execution

Add your test function to the `run_all_tests()` function:

```bash
run_all_tests() {
    # ... existing tests ...
    test_new_command
    # ... remaining tests ...
}
```

### 3. Test Function Parameters

The `execute_cli_test` function accepts:
- `test_name`: Descriptive name for the test
- `cli_command`: The CLI command to execute (without `pgagroal-cli`)
- `expected_success`: `"true"` for success, `"false"` for expected failure
- `connection_type`: `"local"` or `"remote"`

### 4. Parameter Substitution

Use test parameters for dynamic values:
- `$TEST_DATABASE` - Database name for testing
- `$TEST_SERVER` - Server name for testing  
- `$TEST_CONF_PARAM` - Configuration parameter name
- `$TEST_CONF_VALUE` - Configuration parameter value

## Logging System

The test suite provides structured logging with multiple levels:

### Log Levels

```bash
log_info "Informational message"      # Blue [INFO]
log_success "Success message"         # Green [SUCCESS]  
log_warning "Warning message"         # Yellow [WARNING]
log_error "Error message"             # Red [ERROR]
log_debug "Debug message"             # Yellow [DEBUG] (only with --debug)
log_verbose "Verbose message"         # Yellow [VERBOSE] (only with --debug)
```

### Log Output

- **Console Output**: All log messages appear on console by default
- **File Output**: When using `-F` option, logs go to file but summary still appears on console
- **Debug Mode**: Enable with `--debug` for detailed command execution and validation steps

### Log Files (Comprehensive Mode Only)

- `log-cli/logfile` - PostgreSQL server logs
- `log-cli/pgagroal_cli.log` - pgagroal server logs
- `log-cli/pgbench.log` - pgbench initialization logs

## Test Validation

The test suite performs robust validation:

### JSON Validation
- Validates JSON structure for successful commands
- Checks required fields (`Header.Command`, `Outcome.Status`)
- Handles non-JSON responses for expected failures (e.g., connection refused)

### Success Validation
- Compares `Outcome.Status` with expected results
- Validates exit code consistency
- Accepts connection errors for expected failures

### Error Handling
- Expected failures don't require JSON validation
- Connection refused messages are acceptable for shutdown tests
- Graceful degradation for different response formats

## Test Results

### Summary Display
Test summary is always displayed, regardless of output mode:

```
=== TEST SUMMARY ===
Total tests: 30
Passed: 28
Failed: 2

Failed tests:
  - invalid command (remote)
  - conf set invalid (remote)
```

### Exit Codes
- `0` - All tests passed
- `1` - One or more tests failed

## Platform Support

The test suite supports multiple platforms with automatic adaptation:
- **Linux**: Full support with all features
- **macOS**: Full support with Homebrew dependencies  
- **FreeBSD**: Full support with `su - postgres` execution

## Environment Variables

- `DEBUG=true` - Enable debug mode (can be overridden by `--debug` flag)

## Examples

### CI/CD Usage
```bash
# Standard CI testing
./test_cli_comprehensive.sh --debug

# TLS CI testing
./test_cli_comprehensive.sh --debug --tls
```

### Local Development Usage
```bash
# Quick local testing
./test_cli_comprehensive.sh --skip-setup -p 2347 -U admin -P password --debug

# Test with custom database and server
./test_cli_comprehensive.sh --skip-setup -p 2347 -U admin -P password -d testdb -s replica

# Save results to file
./test_cli_comprehensive.sh --skip-setup -p 2347 -U admin -P password -F test_results.log

./test_cli_comprehensive.sh --skip-setup -p 2347 -U admin -P admin1234 -d mydb -s primary --conf-param log_level --conf-value INFO --debug
```

The CLI test suite provides reliable validation of pgagroal CLI functionality across different environments and use cases, with clear separation between comprehensive CI testing and quick local development validation.