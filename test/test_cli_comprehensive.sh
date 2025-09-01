#!/bin/bash

# pgagroal CLI Comprehensive Test Script
# Complete standalone test script with its own PostgreSQL and pgagroal setup
# Tests all CLI commands with both local and remote connections

set -e

# Platform detection
OS=$(uname)

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="$PROJECT_DIR/test"

# Test environment configuration
PGAGROAL_PORT=2345
MANAGEMENT_PORT=2346
POSTGRES_PORT=5432
ADMIN_USER="admin"
ADMIN_PASSWORD="adminpass"
TEST_TIMEOUT=30
WAIT_TIMEOUT=10

# TLS configuration
TLS_ENABLED=false

# Skip setup configuration
SKIP_SETUP=false
REMOTE_HOST="localhost"
REMOTE_PORT=""
REMOTE_USER="admin"
REMOTE_PASSWORD="password"
REMOTE_CONFIG="/etc/pgagroal/pgagroal.conf"
TEST_DATABASE="postgres"
TEST_SERVER="primary"
TEST_CONF_PARAM="log_level"
TEST_CONF_VALUE="info"


OUTPUT_FILE=""

# User configuration
PSQL_USER=$(whoami)
if [ "$OS" = "FreeBSD" ]; then
    PSQL_USER=postgres
fi
PGPASSWORD="password"

# Directories for CLI test environment
LOG_DIR="$PROJECT_DIR/log-cli"
PGCTL_LOG_FILE="$LOG_DIR/logfile"
PGAGROAL_LOG_FILE="$LOG_DIR/pgagroal_cli.log"
PGBENCH_LOG_FILE="$LOG_DIR/pgbench.log"

POSTGRES_OPERATION_DIR="$PROJECT_DIR/pgagroal-postgresql-cli"
DATA_DIR="$POSTGRES_OPERATION_DIR/data"

PGAGROAL_OPERATION_DIR="$PROJECT_DIR/pgagroal-testsuite-cli"
CONFIG_DIR="$PGAGROAL_OPERATION_DIR/conf"

# Test results
TESTS_PASSED=0
TESTS_FAILED=0
FAILED_TESTS=()
PASSED_TESTS=()

# Colors for output (will be set based on output mode)
RED=''
GREEN=''
YELLOW=''
BLUE=''
NC=''

########################### UTILS ############################

# Set colors based on output mode
setup_colors() {
    if [[ -n "$OUTPUT_FILE" ]]; then
        # File output - no colors
        RED=''
        GREEN=''
        YELLOW=''
        BLUE=''
        NC=''
    else
        # Terminal output - use colors
        RED='\033[0;31m'
        GREEN='\033[0;32m'
        YELLOW='\033[1;33m'
        BLUE='\033[0;34m'
        NC='\033[0m'
    fi
}

# Strip color codes from text (for file output)
strip_colors() {
    echo "$1" | sed 's/\x1b\[[0-9;]*m//g'
}

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_debug() {
    if [[ "${DEBUG:-false}" == "true" ]]; then
        echo -e "${YELLOW}[DEBUG]${NC} $1"
    fi
}

log_verbose() {
    if [[ "${DEBUG:-false}" == "true" ]]; then
        echo -e "${YELLOW}[VERBOSE]${NC} $1"
    fi
}

# Port and process utilities
is_port_in_use() {
    local port=$1
    if [[ "$OS" == "Linux" ]]; then
        ss -tuln | grep $port >/dev/null 2>&1
    elif [[ "$OS" == "Darwin" ]]; then
        lsof -i:$port >/dev/null 2>&1
    elif [[ "$OS" == "FreeBSD" ]]; then
        sockstat -4 -l | grep $port >/dev/null 2>&1
    fi
    return $?
}

next_available_port() {
    local port=$1
    while true; do
        is_port_in_use $port
        if [ $? -ne 0 ]; then
            echo "$port"
            return 0
        else
            port=$((port + 1))
        fi
    done
}

wait_for_server_ready() {
    local start_time=$SECONDS
    local port=$1
    local server_name=${2:-"server"}
    
    log_info "Waiting for $server_name to be ready on port $port..."
    
    while true; do
        # For PostgreSQL, use pg_isready
        if [[ "$server_name" == "PostgreSQL" ]]; then
            pg_isready -h localhost -p $port >/dev/null 2>&1
            if [ $? -eq 0 ]; then
                log_success "$server_name is ready on port $port"
                return 0
            fi
        else
            # For pgagroal, just check if the port is listening
            if [[ "$OS" == "Linux" ]]; then
                ss -tuln | grep ":$port " >/dev/null 2>&1
            elif [[ "$OS" == "Darwin" ]]; then
                lsof -i:$port >/dev/null 2>&1
            elif [[ "$OS" == "FreeBSD" ]]; then
                sockstat -4 -l | grep ":$port " >/dev/null 2>&1
            fi
            
            if [ $? -eq 0 ]; then
                log_success "$server_name is ready on port $port"
                return 0
            fi
        fi
        
        if [ $(($SECONDS - $start_time)) -gt $WAIT_TIMEOUT ]; then
            log_error "Timeout waiting for $server_name on port $port"
            return 1
        fi
        sleep 1
    done
}

function sed_i() {
   if [[ "$OS" == "Darwin" || "$OS" == "FreeBSD" ]]; then
      sed -i '' -E "$@"
   else
      sed -i -E "$@"
   fi
}

run_as_postgres() {
  if [[ "$OS" == "FreeBSD" ]]; then
    su - postgres -c "$*"
  else
    eval "$@"
  fi
}

stop_processes() {
    log_info "Stopping any running pgagroal and PostgreSQL processes..."
    pkill -f pgagroal || true
    
    if [[ -f "$PGCTL_LOG_FILE" ]]; then
        if [[ "$OS" == "FreeBSD" ]]; then
            su - postgres -c "pg_ctl -D $DATA_DIR -l $PGCTL_LOG_FILE stop" || true
        else
            pg_ctl -D $DATA_DIR -l $PGCTL_LOG_FILE stop || true
        fi
    fi
    sleep 2
}

########################### SETUP FUNCTIONS ############################

find_executable_dir() {
    local possible_dirs=(
        "$PROJECT_DIR/src"           # Default location
        "$PROJECT_DIR/build/src"     # CMake build directory
        "$PROJECT_DIR/build"         # Alternative build location
        "/usr/local/bin"             # System installation
        "/usr/bin"                   # System installation
    )
    
    for dir in "${possible_dirs[@]}"; do
        if [[ -f "$dir/pgagroal" ]] && [[ -f "$dir/pgagroal-cli" ]] && [[ -f "$dir/pgagroal-admin" ]]; then
            echo "$dir"
            return 0
        fi
    done
    
    return 1
}

check_system_requirements() {
    log_info "Checking system requirements..."
    
    # Check required binaries
    local required_bins=("initdb" "pg_ctl" "pgbench" "psql" "jq")
    for bin in "${required_bins[@]}"; do
        if ! which $bin > /dev/null 2>&1; then
            log_error "$bin not found in PATH"
            return 1
        fi
    done
    
    # Check ports
    POSTGRES_PORT=$(next_available_port $POSTGRES_PORT)
    if is_port_in_use $PGAGROAL_PORT; then
        PGAGROAL_PORT=$(next_available_port $PGAGROAL_PORT)
    fi
    if is_port_in_use $MANAGEMENT_PORT; then
        MANAGEMENT_PORT=$(next_available_port $MANAGEMENT_PORT)
    fi
    
    log_success "System requirements check passed"
    log_info "Using PostgreSQL port: $POSTGRES_PORT"
    log_info "Using pgagroal port: $PGAGROAL_PORT"
    log_info "Using management port: $MANAGEMENT_PORT"
    return 0
}

initialize_log_files() {
    log_info "Initializing log files..."
    mkdir -p $LOG_DIR
    touch $PGAGROAL_LOG_FILE $PGCTL_LOG_FILE $PGBENCH_LOG_FILE
    log_success "Log files initialized in $LOG_DIR"
}

create_postgresql_cluster() {
    log_info "Creating PostgreSQL cluster..."
    
    if [ "$OS" = "FreeBSD" ]; then
        mkdir -p "$POSTGRES_OPERATION_DIR"
        mkdir -p "$DATA_DIR"
        mkdir -p "$CONFIG_DIR"
        if ! pw user show postgres >/dev/null 2>&1; then
            pw groupadd -n postgres -g 770
            pw useradd -n postgres -u 770 -g postgres -d /var/db/postgres -s /bin/sh
        fi
        chown postgres:postgres $PGCTL_LOG_FILE
        chown postgres:postgres $PGBENCH_LOG_FILE
        chown -R postgres:postgres "$DATA_DIR"
        chown -R postgres:postgres "$CONFIG_DIR"
    fi

    # Initialize database
    INITDB_PATH=$(command -v initdb)
    if [ -z "$INITDB_PATH" ]; then
        log_error "initdb not found!"
        return 1
    fi

    run_as_postgres "$INITDB_PATH -k -D $DATA_DIR"
    
    # Configure PostgreSQL
    LOG_ABS_PATH=$(realpath "$LOG_DIR")
    sed_i "s/^#*logging_collector.*/logging_collector = on/" "$DATA_DIR/postgresql.conf"
    sed_i "s/^#*log_destination.*/log_destination = 'stderr'/" "$DATA_DIR/postgresql.conf"
    sed_i "s|^#*log_directory.*|log_directory = '$LOG_ABS_PATH'|" "$DATA_DIR/postgresql.conf"
    sed_i "s/^#*log_filename.*/log_filename = 'logfile'/" "$DATA_DIR/postgresql.conf"
    sed_i "s|#unix_socket_directories = '/var/run/postgresql'|unix_socket_directories = '/tmp'|" "$DATA_DIR/postgresql.conf"
    sed_i "s/#port = 5432/port = $POSTGRES_PORT/" "$DATA_DIR/postgresql.conf"
    sed_i "s/#max_connections = 100/max_connections = 200/" "$DATA_DIR/postgresql.conf"

    # Configure HBA
    cat > "$DATA_DIR/pg_hba.conf" << EOF
local   all             all                                     trust
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
host    mydb            myuser          127.0.0.1/32            scram-sha-256
host    mydb            myuser          ::1/128                 scram-sha-256
EOF

    log_success "PostgreSQL cluster created"
}

start_postgresql() {
    log_info "Starting PostgreSQL..."
    
    PGCTL_PATH=$(command -v pg_ctl)
    if [ -z "$PGCTL_PATH" ]; then
        log_error "pg_ctl not found!"
        return 1
    fi

    run_as_postgres "$PGCTL_PATH -D $DATA_DIR -l $PGCTL_LOG_FILE start"
    
    if ! wait_for_server_ready $POSTGRES_PORT "PostgreSQL"; then
        return 1
    fi

    # Create test user and database
    psql -h localhost -p $POSTGRES_PORT -U $PSQL_USER -d postgres -c "CREATE ROLE myuser WITH LOGIN PASSWORD '$PGPASSWORD';" || true
    psql -h localhost -p $POSTGRES_PORT -U $PSQL_USER -d postgres -c "CREATE DATABASE mydb WITH OWNER myuser;" || true
    
    # Initialize pgbench
    pgbench -i -s 1 -n -h localhost -p $POSTGRES_PORT -U $PSQL_USER -d postgres
    
    log_success "PostgreSQL started and configured"
}

create_master_key() {
    log_info "Creating master key..."
    
    if [[ "$OS" == "FreeBSD" ]]; then
        if run_as_postgres "test -f ~/.pgagroal/master.key"; then
            log_info "Master key already exists"
            return 0
        fi
        run_as_postgres "mkdir -p ~/.pgagroal"
        run_as_postgres "chmod 700 ~/.pgagroal"
    else
        if [ -f "$HOME/.pgagroal/master.key" ]; then
            log_info "Master key already exists"
            return 0
        fi
        mkdir -p ~/.pgagroal
        chmod 700 ~/.pgagroal
    fi
    
    run_as_postgres "$EXECUTABLE_DIR/pgagroal-admin master-key -P $PGPASSWORD"
    log_success "Master key created"
}

create_tls_certificates() {
    if [[ "$TLS_ENABLED" != "true" ]]; then
        return 0
    fi
    
    log_info "Creating TLS certificates..."
    
    local cert_dir="$CONFIG_DIR/certs"
    mkdir -p "$cert_dir"
    
    # Generate server certificate
    openssl req -new -x509 -days 365 -nodes -text \
        -out "$cert_dir/server.crt" \
        -keyout "$cert_dir/server.key" \
        -subj "/CN=localhost" >/dev/null 2>&1
    
    # Set proper permissions
    chmod 644 "$cert_dir/server.crt"
    chmod 600 "$cert_dir/server.key"
    
    # Ensure certificates are readable by the user running pgagroal
    if [[ "$OS" == "FreeBSD" ]]; then
        chown postgres:postgres "$cert_dir/server.crt" "$cert_dir/server.key"
    fi
    
    # Setup client certificates
    if [[ "$OS" == "FreeBSD" ]]; then
        run_as_postgres "mkdir -p ~/.pgagroal"
        run_as_postgres "chmod 700 ~/.pgagroal"
        run_as_postgres "cp $cert_dir/server.crt ~/.pgagroal/pgagroal.crt"
        run_as_postgres "cp $cert_dir/server.key ~/.pgagroal/pgagroal.key"
        run_as_postgres "cp $cert_dir/server.crt ~/.pgagroal/root.crt"
        run_as_postgres "chmod 600 ~/.pgagroal/pgagroal.key"
        run_as_postgres "chmod 644 ~/.pgagroal/pgagroal.crt ~/.pgagroal/root.crt"
    else
        mkdir -p ~/.pgagroal
        chmod 700 ~/.pgagroal
        cp "$cert_dir/server.crt" ~/.pgagroal/pgagroal.crt
        cp "$cert_dir/server.key" ~/.pgagroal/pgagroal.key
        cp "$cert_dir/server.crt" ~/.pgagroal/root.crt
        chmod 600 ~/.pgagroal/pgagroal.key
        chmod 644 ~/.pgagroal/pgagroal.crt ~/.pgagroal/root.crt
    fi
    
    log_success "TLS certificates created"
}

create_pgagroal_configuration() {
    log_info "Creating pgagroal configuration..."
    
    mkdir -p "$CONFIG_DIR"
    
    # Main configuration
    cat > "$CONFIG_DIR/pgagroal.conf" << EOF
[pgagroal]
host = localhost
port = $PGAGROAL_PORT
management = $MANAGEMENT_PORT

log_type = file
log_level = info
log_path = $PGAGROAL_LOG_FILE

max_connections = 8
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/
pipeline = session
EOF

    # Add TLS configuration if enabled
    if [[ "$TLS_ENABLED" == "true" ]]; then
        cat >> "$CONFIG_DIR/pgagroal.conf" << EOF

# TLS Configuration
tls = on
tls_cert_file = $CONFIG_DIR/certs/server.crt
tls_key_file = $CONFIG_DIR/certs/server.key
tls_ca_file = $CONFIG_DIR/certs/server.crt
EOF
    fi

    cat >> "$CONFIG_DIR/pgagroal.conf" << EOF

[primary]
host = localhost
port = $POSTGRES_PORT
EOF

    # HBA configuration - different for TLS vs non-TLS
    if [[ "$TLS_ENABLED" == "true" ]]; then
        # TLS-enabled HBA configuration for CLI testing
        cat > "$CONFIG_DIR/pgagroal_hba.conf" << EOF
# TYPE  DATABASE USER  ADDRESS  METHOD
host    all      all   all     trust
hostssl all      all   all     trust
EOF
    else
        # Standard HBA configuration (original simple format)
        cat > "$CONFIG_DIR/pgagroal_hba.conf" << EOF
host    all all all all
EOF
    fi

    # Database limits configuration
    cat > "$CONFIG_DIR/pgagroal_databases.conf" << EOF
#
# DATABASE=ALIAS1,ALIAS2 USER MAX_SIZE INITIAL_SIZE MIN_SIZE
#
postgres=pgalias1,pgalias2 $PSQL_USER 8 0 0
EOF

    # Create users configuration
    if [ -f "$CONFIG_DIR/pgagroal_users.conf" ]; then
        rm -f "$CONFIG_DIR/pgagroal_users.conf"
    fi
    
    run_as_postgres "$EXECUTABLE_DIR/pgagroal-admin -f $CONFIG_DIR/pgagroal_users.conf -U $PSQL_USER -P $PGPASSWORD user add"
    
    # Create admin configuration
    if [ -f "$CONFIG_DIR/pgagroal_admins.conf" ]; then
        rm -f "$CONFIG_DIR/pgagroal_admins.conf"
    fi
    
    run_as_postgres "$EXECUTABLE_DIR/pgagroal-admin -f $CONFIG_DIR/pgagroal_admins.conf -U $ADMIN_USER -P $ADMIN_PASSWORD user add"
    
    # Debug: Show created configuration files
    if [[ "${DEBUG:-false}" == "true" ]]; then
        log_debug "Admin configuration file:"
        cat "$CONFIG_DIR/pgagroal_admins.conf" || true
        log_debug "Users configuration file:"
        cat "$CONFIG_DIR/pgagroal_users.conf" || true
    fi
    
    if [[ "$OS" == "FreeBSD" ]]; then
        chown -R postgres:postgres "$CONFIG_DIR"
        chown postgres:postgres "$PGAGROAL_LOG_FILE"
    fi
    
    log_success "pgagroal configuration created"
}

start_pgagroal() {
    log_info "Starting pgagroal..."
    
    # Debug: Show configuration files if debug is enabled
    if [[ "${DEBUG:-false}" == "true" ]]; then
        log_debug "pgagroal configuration:"
        cat "$CONFIG_DIR/pgagroal.conf"
        log_debug "HBA configuration:"
        cat "$CONFIG_DIR/pgagroal_hba.conf"
        
        if [[ "$TLS_ENABLED" == "true" ]]; then
            log_debug "TLS certificate files:"
            ls -la "$CONFIG_DIR/certs/" || true
        fi
    fi
    
    run_as_postgres "$EXECUTABLE_DIR/pgagroal -c $CONFIG_DIR/pgagroal.conf -a $CONFIG_DIR/pgagroal_hba.conf -u $CONFIG_DIR/pgagroal_users.conf -l $CONFIG_DIR/pgagroal_databases.conf -A $CONFIG_DIR/pgagroal_admins.conf -d"
    
    # Give pgagroal a moment to start and check logs if it fails
    sleep 2
    
    if ! wait_for_server_ready $PGAGROAL_PORT "pgagroal"; then
        log_error "pgagroal failed to start. Checking logs..."
        if [[ -f "$PGAGROAL_LOG_FILE" ]]; then
            log_error "pgagroal log contents:"
            tail -20 "$PGAGROAL_LOG_FILE" || true
        fi
        return 1
    fi
    
    # Also check if management port is ready
    if ! wait_for_server_ready $MANAGEMENT_PORT "pgagroal-management"; then
        log_error "pgagroal management port failed to start"
        return 1
    fi
    
    # Test admin credentials work
    log_info "Testing admin credentials..."
    local test_cmd
    if [[ "$OS" == "FreeBSD" ]]; then
        test_cmd="su - postgres -c '$EXECUTABLE_DIR/pgagroal-cli -h localhost -p $MANAGEMENT_PORT -U $ADMIN_USER -P $ADMIN_PASSWORD ping --format json'"
    else
        test_cmd="$EXECUTABLE_DIR/pgagroal-cli -h localhost -p $MANAGEMENT_PORT -U $ADMIN_USER -P $ADMIN_PASSWORD ping --format json"
    fi
    
    log_debug "Testing admin credentials with command: $test_cmd"
    local admin_output
    if admin_output=$(timeout 10 bash -c "$test_cmd" 2>&1); then
        log_success "Admin credentials verified"
        log_debug "Admin test output: $admin_output"
    else
        local admin_exit_code=$?
        log_warning "Admin credentials test failed - remote tests may fail (exit code: $admin_exit_code)"
        log_error "Admin test command output: $admin_output"
        
        # Additional TLS debugging if enabled
        if [[ "$TLS_ENABLED" == "true" ]]; then
            log_error "TLS mode enabled - checking certificate setup:"
            log_error "Client certificate files:"
            if [[ "$OS" == "FreeBSD" ]]; then
                su - postgres -c "ls -la ~/.pgagroal/" || true
            else
                ls -la ~/.pgagroal/ || true
            fi
        fi
    fi
    
    # Final verification that pgagroal is working
    if [[ "$TLS_ENABLED" == "true" ]]; then
        log_info "TLS mode: Performing additional connectivity checks..."
        
        # Check if pgagroal process is still running
        if pgrep -f "pgagroal" >/dev/null; then
            log_info "pgagroal process is running"
        else
            log_warning "pgagroal process check failed - but this may not indicate a real problem"
            # Don't return 1 here as the process might be running but not match our pattern
        fi
        
        # Check recent log entries for any TLS errors
        if [[ -f "$PGAGROAL_LOG_FILE" ]]; then
            log_debug "Recent pgagroal log entries:"
            tail -10 "$PGAGROAL_LOG_FILE" || true
        fi
    fi
    
    log_success "pgagroal started successfully"
}

# JSON validation functions
validate_json_structure() {
    local json_output="$1"
    local test_name="$2"
    
    # Check if we have any output at all
    if [[ -z "$json_output" ]]; then
        log_error "$test_name: No output received - CLI/server may be down"
        return 1
    fi
    
    # Check if output is valid JSON
    if ! echo "$json_output" | jq . >/dev/null 2>&1; then
        log_error "$test_name: Invalid JSON output"
        log_error "$test_name: Raw output: $json_output"
        return 1
    fi
    
    # Check for required JSON structure (actual pgagroal CLI format)
    local command_name=$(echo "$json_output" | jq -r '.Header.Command // "null"')
    
    # Check if we have the expected structure
    if ! echo "$json_output" | jq -e '.Outcome | has("Status")' >/dev/null 2>&1; then
        log_error "$test_name: Missing Outcome.Status in JSON structure"
        return 1
    fi

    if [[ "$command_name" == "null" || -z "$command_name" ]]; then
        log_error "$test_name: Missing Header.Command in JSON structure"
        return 1
    fi
    
    return 0
}

validate_command_success() {
    local json_output="$1"
    local expected_success="$2"  # true/false
    local test_name="$3"
    
    # Extract the Outcome.Status value (this should be the primary status indicator)
    local actual_status=$(echo "$json_output" | jq -r '.Outcome.Status // "null"' 2>/dev/null)
    
    # If we couldn't find Outcome.Status, try fallback methods
    if [[ "$actual_status" == "null" || -z "$actual_status" ]]; then
        # Try to find Status at any level using jq's recursive descent
        local status_values=$(echo "$json_output" | jq -r '.. | objects | select(has("Status")) | .Status' 2>/dev/null)
        
        if [[ -n "$status_values" ]]; then
            actual_status=$(echo "$status_values" | head -n1)
        else
            # Try legacy formats
            local legacy_status=$(echo "$json_output" | jq -r '.command.status // empty' 2>/dev/null)
            if [[ -n "$legacy_status" && "$legacy_status" != "null" ]]; then
                # Convert legacy status to boolean-like
                if [[ "$legacy_status" == "OK" ]]; then
                    actual_status="true"
                else
                    actual_status="false"
                fi
            else
                # Check for error field as another indicator
                local error_field=$(echo "$json_output" | jq -r '.command.error // .Outcome.Error // empty' 2>/dev/null)
                if [[ -n "$error_field" && "$error_field" != "null" ]]; then
                    # If error is 0 or false, it's success; otherwise failure
                    if [[ "$error_field" == "0" || "$error_field" == "false" ]]; then
                        actual_status="true"
                    else
                        actual_status="false"
                    fi
                fi
            fi
        fi
    fi
    
    # If we still couldn't find any status indicator, treat as failure
    if [[ "$actual_status" == "null" || -z "$actual_status" ]]; then
        log_error "$test_name: No status indicator found in JSON response"
        log_error "$test_name: Expected: $expected_success, Received: <no status found>"
        return 1
    fi
    
    # Debug output
    log_info "$test_name: Status validation - Expected: $expected_success, Received: $actual_status"
    
    # Compare the found status with expected result
    if [[ "$expected_success" == "true" ]]; then
        # We expect success
        if [[ "$actual_status" == "true" ]]; then
            return 0  # Success as expected
        else
            log_error "$test_name: Expected success but got failure"
            log_error "$test_name: Expected: true, Received: $actual_status"
            return 1
        fi
    else
        # We expect failure
        if [[ "$actual_status" == "true" ]]; then
            log_error "$test_name: Expected failure but got success"
            log_error "$test_name: Expected: false, Received: $actual_status"
            return 1
        else
            return 0  # Failure as expected
        fi
    fi
}






execute_cli_test() {
    local test_name="$1"
    local cli_command="$2"
    local expected_success="$3"  # true/false
    local connection_type="$4"   # local/remote
    
    log_info "Testing: $test_name ($connection_type)"
    
    local full_command
    local output
    local exit_code
    
    # Build command based on connection type and skip-setup mode
    if [[ "$SKIP_SETUP" == "true" ]]; then
        # Skip-setup mode: always use remote connection with provided parameters
        if [[ "$OS" == "FreeBSD" ]]; then
            full_command="su - postgres -c '$EXECUTABLE_DIR/pgagroal-cli -h $REMOTE_HOST -p $REMOTE_PORT -U $REMOTE_USER -P $REMOTE_PASSWORD $cli_command --format json'"
        else
            full_command="$EXECUTABLE_DIR/pgagroal-cli -h $REMOTE_HOST -p $REMOTE_PORT -U $REMOTE_USER -P $REMOTE_PASSWORD $cli_command --format json"
        fi
    elif [[ "$connection_type" == "local" ]]; then
        if [[ "$OS" == "FreeBSD" ]]; then
            full_command="su - postgres -c '$EXECUTABLE_DIR/pgagroal-cli -c $CONFIG_DIR/pgagroal.conf $cli_command --format json'"
        else
            full_command="$EXECUTABLE_DIR/pgagroal-cli -c $CONFIG_DIR/pgagroal.conf $cli_command --format json"
        fi
    else
        if [[ "$OS" == "FreeBSD" ]]; then
            full_command="su - postgres -c '$EXECUTABLE_DIR/pgagroal-cli -h localhost -p $MANAGEMENT_PORT -U $ADMIN_USER -P $ADMIN_PASSWORD $cli_command --format json'"
        else
            full_command="$EXECUTABLE_DIR/pgagroal-cli -h localhost -p $MANAGEMENT_PORT -U $ADMIN_USER -P $ADMIN_PASSWORD $cli_command --format json"
        fi
    fi
    
    # Execute command with timeout and capture all output
    log_debug "Executing command: $full_command"
    if output=$(timeout $TEST_TIMEOUT bash -c "$full_command" 2>&1); then
        exit_code=0
    else
        exit_code=$?
    fi
    
    # Always log the raw output for debugging, regardless of debug mode
    if [[ -n "$output" ]]; then
        log_debug "Command output: $output"
    else
        log_debug "Command produced no output (exit code: $exit_code)"
    fi
    
    # Convert expected_success to expected exit code for comparison
    local expected_exit_code
    if [[ "$expected_success" == "true" ]]; then
        expected_exit_code=0
    else
        expected_exit_code=1
    fi
    
    # Check if exit code matches expectation
    local exit_code_matches=false
    if [[ $exit_code -eq $expected_exit_code ]]; then
        exit_code_matches=true
    fi
    
    # Handle different scenarios
    if [[ $exit_code -eq 0 ]]; then
        # Command executed successfully, validate JSON structure and content
        if ! validate_json_structure "$output" "$test_name ($connection_type)"; then
            log_error "$test_name ($connection_type): JSON structure validation failed"
            echo "Command output: $output"
            FAILED_TESTS+=("$test_name ($connection_type)")
            ((TESTS_FAILED++))
            return 1
        fi
        
        if ! validate_command_success "$output" "$expected_success" "$test_name ($connection_type)"; then
            log_error "$test_name ($connection_type): Command success validation failed"
            echo "Command output: $output"
            FAILED_TESTS+=("$test_name ($connection_type)")
            ((TESTS_FAILED++))
            return 1
        fi
        
        # Additional check: exit code should match JSON status
        local json_status=$(echo "$output" | jq -r '.Outcome.Status // "null"' 2>/dev/null)
        if [[ "$json_status" == "true" && $exit_code -ne 0 ]] || [[ "$json_status" == "false" && $exit_code -eq 0 ]]; then
            log_warning "$test_name ($connection_type): Exit code ($exit_code) doesn't match JSON status ($json_status)"
        fi
        
    else
        # Command failed (non-zero exit code)
        if [[ "$expected_success" == "true" ]]; then
            # We expected success but got failure
            log_error "$test_name ($connection_type): Command failed unexpectedly"
            log_error "$test_name ($connection_type): Expected: success (exit code 0), Received: failure (exit code $exit_code)"
            
            # Check if we have any output to analyze
            if [[ -n "$output" ]]; then
                echo "Command output: $output"
                # Try to parse JSON even on failure to get more info
                if echo "$output" | jq . >/dev/null 2>&1; then
                    local json_status=$(echo "$output" | jq -r '.Outcome.Status // "unknown"' 2>/dev/null)
                    log_error "$test_name ($connection_type): JSON Status: $json_status"
                fi
            else
                log_error "$test_name ($connection_type): No output received - CLI/server may be down"
            fi
            
            FAILED_TESTS+=("$test_name ($connection_type)")
            ((TESTS_FAILED++))
            return 1
        else
            # We expected failure and got failure - this is correct
            log_verbose "$test_name ($connection_type): Command failed as expected (exit code: $exit_code)"
            
            # For expected failures, don't require JSON validation
            # This handles cases like connection refused when server is down
            if [[ -n "$output" ]]; then
                log_verbose "Command output: $output"
                # Only try JSON validation if output looks like JSON
                if echo "$output" | jq . >/dev/null 2>&1; then
                    if validate_json_structure "$output" "$test_name ($connection_type)" 2>/dev/null; then
                        validate_command_success "$output" "$expected_success" "$test_name ($connection_type)" || true
                    fi
                else
                    # Non-JSON output is acceptable for expected failures (e.g., connection refused)
                    log_verbose "$test_name ($connection_type): Non-JSON output received (acceptable for expected failure)"
                fi
            else
                log_verbose "Command produced no output"
            fi
        fi
    fi
    
    log_success "$test_name ($connection_type): PASSED"
    PASSED_TESTS+=("$test_name ($connection_type)")
    ((TESTS_PASSED++))
    return 0
}

# Individual test functions
test_ping_command() {
    log_info "=== Testing PING Command ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "ping" "ping" "true" "local"
        execute_cli_test "ping remote" "ping" "true" "remote"
    else
        execute_cli_test "ping" "ping" "true" "local"
        execute_cli_test "ping remote" "ping" "true" "remote"
    fi
}

test_status_commands() {
    log_info "=== Testing STATUS Commands ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "status" "status" "true" "local"
        execute_cli_test "status details" "status details" "true" "local"
        execute_cli_test "status details" "status details" "true" "remote"
        execute_cli_test "status remote" "status" "true" "remote"
    else
        execute_cli_test "status" "status" "true" "local"
        execute_cli_test "status details" "status details" "true" "local"
        execute_cli_test "status details" "status details" "true" "remote"
        execute_cli_test "status remote" "status" "true" "remote"
    fi
}

test_conf_commands() {
    log_info "=== Testing CONF Commands ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "conf ls" "conf ls" "true" "remote"
        execute_cli_test "conf alias" "conf alias" "true" "remote"
        execute_cli_test "conf get $TEST_CONF_PARAM" "conf get $TEST_CONF_PARAM" "true" "remote"
        execute_cli_test "conf get nonexistent" "conf get nonexistent_param" "false" "remote"
        
        # Save current value first for restoration
        local current_value
        if current_value=$($EXECUTABLE_DIR/pgagroal-cli -h $REMOTE_HOST -p $REMOTE_PORT -U $REMOTE_USER -P $REMOTE_PASSWORD conf get $TEST_CONF_PARAM --format text 2>/dev/null | tail -1); then
            execute_cli_test "conf set $TEST_CONF_PARAM" "conf set $TEST_CONF_PARAM $TEST_CONF_VALUE" "true" "remote"
            # Restore original value
            execute_cli_test "conf set $TEST_CONF_PARAM restore" "conf set $TEST_CONF_PARAM $current_value" "true" "remote"
        else
            log_warning "Could not get current value of $TEST_CONF_PARAM, skipping conf set test"
        fi
        execute_cli_test "conf reload" "conf reload" "true" "remote"
    else
        execute_cli_test "conf ls" "conf ls" "true" "local"
        execute_cli_test "conf get max_connections" "conf get max_connections" "true" "local"
        execute_cli_test "conf get nonexistent" "conf get nonexistent_param" "false" "local"
        execute_cli_test "conf alias" "conf alias" "true" "local"
        execute_cli_test "conf ls remote" "conf ls" "true" "remote"
        execute_cli_test "conf get remote" "conf get" "true" "remote"
        
        # execute_cli_test "conf set log_level" "conf set log_level debug" "true" "local"
        # execute_cli_test "conf set port" "conf set port 2000" "true" "remote"
        # execute_cli_test "conf reload" "conf reload" "true" "local"
        # execute_cli_test "conf reload" "conf reload" "true" "remote"
    fi
}


test_enable_disable_commands() {
    log_info "=== Testing ENABLE/DISABLE Commands ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "disable $TEST_DATABASE" "disable $TEST_DATABASE" "true" "remote"
        execute_cli_test "enable $TEST_DATABASE" "enable $TEST_DATABASE" "true" "remote"
        execute_cli_test "disable all" "disable" "true" "remote"
        execute_cli_test "enable all" "enable" "true" "remote"
    else
        execute_cli_test "disable postgres" "disable postgres" "true" "local"
        execute_cli_test "enable postgres" "enable postgres" "true" "local"
        execute_cli_test "disable all" "disable" "true" "remote"
        execute_cli_test "enable all" "enable" "true" "remote"
    fi
}

test_flush_commands() {
    log_info "=== Testing FLUSH Commands ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "flush gracefully $TEST_DATABASE" "flush gracefully $TEST_DATABASE" "true" "remote"
        execute_cli_test "flush idle $TEST_DATABASE" "flush idle $TEST_DATABASE" "true" "remote"
        execute_cli_test "flush all $TEST_DATABASE" "flush all $TEST_DATABASE" "true" "remote"
        execute_cli_test "flush gracefully all" "flush gracefully" "true" "remote"
    else
        execute_cli_test "flush gracefully" "flush gracefully" "true" "local"
        execute_cli_test "flush idle" "flush idle" "true" "local"
        execute_cli_test "flush postgres" "flush postgres" "true" "local"
        execute_cli_test "flush all postgres" "flush all postgres" "true" "remote"
    fi
}

test_clear_commands() {
    log_info "=== Testing CLEAR Commands ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "clear prometheus" "clear prometheus" "true" "remote"
        execute_cli_test "clear server $TEST_SERVER" "clear server $TEST_SERVER" "true" "remote"
    else
        execute_cli_test "clear prometheus" "clear prometheus" "true" "local"
        execute_cli_test "clear server primary" "clear server primary" "true" "local"
        execute_cli_test "clear prometheus" "clear prometheus" "true" "remote"
        execute_cli_test "clear server primary" "clear server primary" "true" "remote"
    fi
}

test_switch_to_command() {
    log_info "=== Testing SWITCH-TO Command ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "switch-to $TEST_SERVER" "switch-to $TEST_SERVER" "true" "remote"
        execute_cli_test "switch-to nonexistent" "switch-to nonexistent_server" "false" "remote"
    else
        execute_cli_test "switch-to primary" "switch-to primary" "true" "local"
        execute_cli_test "switch-to nonexistent" "switch-to nonexistent_server" "false" "local"
    fi
}

test_shutdown_commands() {
    log_info "=== Testing SHUTDOWN Commands ==="
    log_warning "This will test shutdown commands - server may become unavailable"
    
    # Test shutdown cancel sequence (safe way to test shutdown)
    log_info "Testing shutdown gracefully -> cancel sequence"
    
    if [[ "$SKIP_SETUP" == "true" ]]; then
        # Immediate shutdown (server goes down right away)
        execute_cli_test "shutdown immediate" "shutdown immediate" "true" "remote"
    else
        # Start graceful shutdown
        execute_cli_test "shutdown gracefully" "shutdown gracefully" "true" "local"
        # Verify server has been shut down
        execute_cli_test "ping after shutdown" "ping" "false" "local"
        log_info "Server has been shut down by test"
    fi
}

test_error_scenarios() {
    log_info "=== Testing ERROR Scenarios ==="
    if [[ "$SKIP_SETUP" == "true" ]]; then
        execute_cli_test "invalid command" "invalid_command" "false" "remote"
        execute_cli_test "conf set invalid" "conf set" "false" "remote"
    else
        execute_cli_test "invalid command" "invalid_command" "false" "local"
        execute_cli_test "conf set invalid" "conf set" "false" "local"
    fi
}

run_all_tests() {

    set +e
    log_info "Starting comprehensive CLI tests..."
    
    # Basic functionality tests
    test_ping_command
    test_status_commands
    test_conf_commands
    
    # Database management tests
    test_enable_disable_commands
    test_flush_commands
    
    # # Server management tests
    test_clear_commands
    test_switch_to_command
    
    # # Error scenario tests
    test_error_scenarios
    
    # Shutdown tests (must be last)
    test_shutdown_commands

    set -e
}

########################### CLEANUP AND REPORTING ############################

generate_test_report() {
    # Always show summary to both stdout and stderr (for file output mode)
    local summary_output=""
    
    summary_output+="\n"
    summary_output+="=== TEST SUMMARY ===\n"
    summary_output+="Total tests: $((TESTS_PASSED + TESTS_FAILED))\n"
    summary_output+="Passed: $TESTS_PASSED\n"
    summary_output+="Failed: $TESTS_FAILED\n"
    
    if [[ $TESTS_PASSED -gt 0 ]]; then
        summary_output+="\nPassed tests:\n"
        for test in "${PASSED_TESTS[@]}"; do
            summary_output+="  ✓ $test\n"
        done
    fi
    
    if [[ $TESTS_FAILED -gt 0 ]]; then
        summary_output+="\nFailed tests:\n"
        for test in "${FAILED_TESTS[@]}"; do
            summary_output+="  ✗ $test\n"
        done
        summary_output+="\n"
    else
        summary_output+="\nAll tests passed!\n\n"
    fi
    
    # Output to current stream (file if redirected)
    echo
    log_info "=== TEST SUMMARY ==="
    echo "Total tests: $((TESTS_PASSED + TESTS_FAILED))"
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    
    if [[ $TESTS_PASSED -gt 0 ]]; then
        echo
        log_success "Passed tests:"
        for test in "${PASSED_TESTS[@]}"; do
            echo -e "  ${GREEN}✓${NC} $test"
        done
    fi
    
    if [[ $TESTS_FAILED -gt 0 ]]; then
        echo
        log_error "Failed tests:"
        for test in "${FAILED_TESTS[@]}"; do
            echo -e "  ${RED}✗${NC} $test"
        done
        echo
    else
        echo
        log_success "All tests passed!"
        echo
    fi
    
    # Also output to original stderr (always visible to user, even with file output)
    if [[ -n "$OUTPUT_FILE" ]]; then
        echo -e "$summary_output" >&3
    fi
    
    if [[ $TESTS_FAILED -gt 0 ]]; then
        return 1
    else
        return 0
    fi
}

cleanup() {
    log_info "Cleaning up test environment..."
    stop_processes
    
    # Remove test directories
    if [ -d "$POSTGRES_OPERATION_DIR" ]; then
        rm -rf "$POSTGRES_OPERATION_DIR"
        log_info "Removed PostgreSQL test directory"
    fi
    
    if [ -d "$PGAGROAL_OPERATION_DIR" ]; then
        rm -rf "$PGAGROAL_OPERATION_DIR"
        log_info "Removed pgagroal test directory"
    fi
    
    if [ -d "$LOG_DIR" ]; then
        rm -rf "$LOG_DIR"
        log_info "Removed log directory"
    fi
    
    # Clean up master key to ensure independence from other tests
    log_info "Cleaning up master key for test independence"
    if [[ "$OS" == "FreeBSD" ]]; then
        su - postgres -c "rm -rf ~/.pgagroal" || true
    else
        rm -rf "$HOME/.pgagroal" || true
    fi
    
    log_success "Cleanup completed"
}

########################### MAIN EXECUTION ############################

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo
    echo "This script runs comprehensive tests for all pgagroal-cli commands."
    echo "It sets up its own PostgreSQL and pgagroal environment for testing."
    echo
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  --no-cleanup   Don't clean up test environment after completion"
    echo "  --debug        Enable debug mode (verbose logging)"
    echo "  --tls          Enable TLS testing mode"
    echo "  --skip-setup   Skip environment setup, use existing pgagroal instance"
    echo
    echo "Skip Setup Mode Options (when --skip-setup is used):"
    echo "  -p, --port PORT         Management port (required with --skip-setup)"
    echo "  -H, --host HOST         pgagroal host (default: localhost)"
    echo "  -U, --user USER         Admin username (default: admin)"
    echo "  -P, --password PASS     Admin password (default: password)"
    echo "  -c, --config FILE       Config file path (default: /etc/pgagroal/pgagroal.conf)"
    echo "  -d, --database NAME     Database name for testing (default: postgres)"
    echo "  -s, --server NAME       Server name for testing (default: primary)"
    echo "  --conf-param NAME       Config parameter for testing (default: log_level)"
    echo "  --conf-value VALUE      Config value for testing (default: info)"


    echo "  -F, --file FILE         Output results to file"
    echo
    echo "Environment Variables:"
    echo "  DEBUG=true     Enable debug mode (overridden by --debug argument)"
    echo
    echo "The script will:"
    echo "  1. Set up a temporary PostgreSQL cluster"
    echo "  2. Configure and start pgagroal with management interface"
    echo "  3. Test all CLI commands (local and remote connections)"
    echo "  4. Generate a comprehensive test report"
    echo "  5. Clean up the test environment"
    echo
    echo "TLS Mode:"
    echo "  When --tls is specified, the script will:"
    echo "  - Generate TLS certificates"
    echo "  - Configure pgagroal with TLS enabled"
    echo "  - Set up client certificates"
    echo "  - Test CLI commands over TLS connections"
    echo
    echo "Examples:"
    echo "  # Comprehensive testing (creates own environment)"
    echo "  $0 --debug                                    # Standard mode"
    echo "  $0 --debug --tls                              # TLS mode"
    echo
    echo "  # Quick testing against existing pgagroal"
    echo "  $0 --skip-setup -p 2347 -U admin -P password # Basic quick test"
    echo "  $0 --skip-setup -p 2347 -F results.log # Test with file output"
    echo
    echo "Note:"
    echo "  - Test summary is always shown, even when using -F file output"
    echo
}

run_single_test_suite() {
    local mode_name="$1"
    
    if [[ "$SKIP_SETUP" == "true" ]]; then
        log_info "Skip-setup mode: Testing against existing pgagroal instance"
        
        # Test connectivity first
        log_info "Testing connectivity to $REMOTE_HOST:$REMOTE_PORT"
        if ! timeout 10 $EXECUTABLE_DIR/pgagroal-cli -h $REMOTE_HOST -p $REMOTE_PORT -U $REMOTE_USER -P $REMOTE_PASSWORD ping --format json >/dev/null 2>&1; then
            log_error "Cannot connect to pgagroal on $REMOTE_HOST:$REMOTE_PORT"
            log_error "Please verify:"
            log_error "  - pgagroal is running"
            log_error "  - Management port $REMOTE_PORT is correct"
            log_error "  - Username/password are correct"
            return 1
        fi
        log_success "Connection successful"
        
        # Test execution phase
        run_all_tests
        
        return 0
    else
        log_info "Setting up test environment for $mode_name mode..."
        
        if ! check_system_requirements; then
            log_error "System requirements check failed"
            return 1
        fi
        
        initialize_log_files
        create_postgresql_cluster
        start_postgresql
        create_master_key
        create_tls_certificates
        create_pgagroal_configuration
        start_pgagroal
        
        log_success "Test environment setup completed for $mode_name mode"
        
        # Test execution phase
        run_all_tests
        
        # Stop processes for cleanup between modes
        stop_processes
        
        return 0
    fi
}

main() {
    local no_cleanup=false
    local debug_mode=false
    
    # Check environment variable first (lower priority)
    if [[ "${DEBUG:-false}" == "true" ]]; then
        debug_mode=true
    fi
    
    # Parse command line arguments (higher priority)
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                usage
                exit 0
                ;;
            --no-cleanup)
                no_cleanup=true
                shift
                ;;
            --debug)
                debug_mode=true
                shift
                ;;
            --tls)
                TLS_ENABLED=true
                shift
                ;;
            --skip-setup)
                SKIP_SETUP=true
                shift
                ;;
            -p|--port)
                REMOTE_PORT="$2"
                shift 2
                ;;
            -H|--host)
                REMOTE_HOST="$2"
                shift 2
                ;;
            -U|--user)
                REMOTE_USER="$2"
                shift 2
                ;;
            -P|--password)
                REMOTE_PASSWORD="$2"
                shift 2
                ;;
            -c|--config)
                REMOTE_CONFIG="$2"
                shift 2
                ;;
            -d|--database)
                TEST_DATABASE="$2"
                shift 2
                ;;
            -s|--server)
                TEST_SERVER="$2"
                shift 2
                ;;
            --conf-param)
                TEST_CONF_PARAM="$2"
                shift 2
                ;;
            --conf-value)
                TEST_CONF_VALUE="$2"
                shift 2
                ;;


            -F|--file)
                OUTPUT_FILE="$2"
                shift 2
                ;;
            *)
                log_error "Unknown argument: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    # Set DEBUG environment variable based on final decision
    if [[ "$debug_mode" == "true" ]]; then
        export DEBUG=true
        log_info "Debug mode enabled"
    else
        export DEBUG=false
    fi
    
    # Validate skip-setup mode requirements
    if [[ "$SKIP_SETUP" == "true" ]]; then
        if [[ -z "$REMOTE_PORT" ]]; then
            log_error "Management port (-p/--port) is required when using --skip-setup"
            usage
            exit 1
        fi
        

        
        log_info "Skip-setup mode: Using existing pgagroal instance"
        log_info "Target: $REMOTE_HOST:$REMOTE_PORT"
        log_info "Test database: $TEST_DATABASE, server: $TEST_SERVER"

    fi
    
    # Setup colors based on output mode
    setup_colors
    
    # Setup output redirection
    if [[ -n "$OUTPUT_FILE" ]]; then
        # Save original stderr for summary output
        exec 3>&2
        exec > "$OUTPUT_FILE" 2>&1
        echo "Results written to $OUTPUT_FILE" >&3
    fi
    
    # Adjust timeout for TLS mode (commands may hang, so fail faster)
    if [[ "$TLS_ENABLED" == "true" ]]; then
        TEST_TIMEOUT=15  # Shorter timeout to fail faster in TLS mode
        log_info "TLS mode: Using shorter timeout ($TEST_TIMEOUT seconds)"
    fi
    
    # Set up cleanup trap (unless disabled or in skip-setup mode)
    if [[ "$no_cleanup" != "true" && "$SKIP_SETUP" != "true" ]]; then
        trap cleanup EXIT
    fi
    
    # Find executable directory
    if EXECUTABLE_DIR=$(find_executable_dir); then
        log_success "Found executables in: $EXECUTABLE_DIR"
    else
        log_error "Could not find pgagroal executables in any expected location"
        log_error "Please ensure pgagroal is built before running this test"
        exit 1
    fi
    
    # Check required executables
    for exe in "pgagroal" "pgagroal-cli" "pgagroal-admin"; do
        if [[ ! -f "$EXECUTABLE_DIR/$exe" ]]; then
            log_error "$exe executable not found at $EXECUTABLE_DIR/$exe"
            exit 1
        fi
    done
    
    # Check for openssl if TLS is enabled
    if [[ "$TLS_ENABLED" == "true" ]]; then
        if ! which openssl > /dev/null 2>&1; then
            log_error "openssl not found in PATH (required for TLS testing)"
            exit 1
        fi
    fi
    
    # Display test mode
    if [[ "$SKIP_SETUP" == "true" ]]; then
        log_info "pgagroal CLI Quick Test Suite (Skip Setup Mode)"

    elif [[ "$TLS_ENABLED" == "true" ]]; then
        log_info "pgagroal CLI Comprehensive Test Suite (TLS Mode)"
    else
        log_info "pgagroal CLI Comprehensive Test Suite (Standard Mode)"
    fi
    log_info "======================================"
    
    # Run single test suite
    local mode_name
    if [[ "$SKIP_SETUP" == "true" ]]; then
        mode_name="Skip-Setup"
    elif [[ "$TLS_ENABLED" == "true" ]]; then
        mode_name="TLS"
    else
        mode_name="Standard"
    fi
    
    if ! run_single_test_suite "$mode_name"; then
        exit 1
    fi
    
    # Report generation
    if generate_test_report; then
        exit 0
    else
        exit 1
    fi
}

# Run main function
main "$@"