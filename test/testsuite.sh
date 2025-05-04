#!/bin/bash

# set -e

## Platform specific variables
OS=$(uname)

THIS_FILE=$(realpath "$0")
USER=$(whoami)
WAIT_TIMEOUT=5

PSQL_USER=$USER
if [ "$OS" = "FreeBSD" ]; then
  PSQL_USER=postgres
fi

## Default values
PGAGROAL_PORT=2345
PORT=5432
PGPASSWORD="password"

## Already present directories
PROJECT_DIRECTORY=$(pwd)
EXECUTABLE_DIRECTORY=$(pwd)/src
TEST_DIRECTORY=$(pwd)/test
TEST_CONFIG_DIRECTORY=$(pwd)/conf

## Create directories and files
LOG_DIRECTORY=$(pwd)/log
PGCTL_LOG_FILE=$LOG_DIRECTORY/logfile
PGAGROAL_LOG_FILE=$LOG_DIRECTORY/pgagroal.log
PGBENCH_LOG_FILE=$LOG_DIRECTORY/pgbench.log

POSTGRES_OPERATION_DIR=$(pwd)/pgagroal-postgresql
DATA_DIRECTORY=$POSTGRES_OPERATION_DIR/data

PGAGROAL_OPERATION_DIR=$(pwd)/pgagroal-testsuite
CONFIGURATION_DIRECTORY=$PGAGROAL_OPERATION_DIR/conf

########################### UTILS ############################
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
    while true; do
        pg_isready -h localhost -p $port
        if [ $? -eq 0 ]; then
            echo "pgagroal is ready for accepting responses"
            return 0
        fi
        if [ $(($SECONDS - $start_time)) -gt $WAIT_TIMEOUT ]; then
            echo "waiting for server timed out"
            return 1
        fi

        # Avoid busy-waiting
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

verify_configuration () {
    ## check if the hostname is `localhost`
    if run_as_postgres "awk '/^\[pgagroal\]/ {found=1} found && /^host *= *localhost/ {print \"FOUND\"; exit}' \"$CONFIGURATION_DIRECTORY/pgagroal.conf\" | grep -q FOUND"; then
        echo "pgagroal host is configured as localhost ... ok"
        ## ok
    else
        echo "pgagroal host isn't configured as localhost in \"$CONFIGURATION_DIRECTORY/pgagroal.conf\""
        return 1
    fi

    ## check if the port is correctly configured
    if run_as_postgres "awk -v new_port=\"$PORT\" '
        /^\[pgagroal\]/ { skip=1; print; next }
        /^\[/ { skip=0 }
        {
            if (!skip && /^port[[:space:]]*=/) {
                sub(/=.*/, \"= \" new_port)
            }
            print
        }' \"$CONFIGURATION_DIRECTORY/pgagroal.conf\" > temp.ini && mv temp.ini \"$CONFIGURATION_DIRECTORY/pgagroal.conf\""; then
        echo "pgagroal port is configured as $PORT ... ok"
    else
        echo "port is not configured correctly in \"$CONFIGURATION_DIRECTORY/pgagroal.conf\""
        return 1
    fi

    ## check if the log_file parameter is present
    if run_as_postgres "awk '/^\[pgagroal\]/ {found=1} found && /^log_type *= *file/ {print \"FOUND\"; exit}' \"$CONFIGURATION_DIRECTORY/pgagroal.conf\" | grep -q FOUND"; then ## check if log_type is file
        run_as_postgres "awk -v new_path=\"$PGAGROAL_LOG_FILE\" '
            /^\[pgagroal\]/ {found=1}
            found && /^log_path *=/ {sub(/=.*/, \"= \" new_path)}
            {print}
        ' \"$CONFIGURATION_DIRECTORY/pgagroal.conf\" > temp.ini && mv temp.ini \"$CONFIGURATION_DIRECTORY/pgagroal.conf\""
        echo "pgagroal log_path is configured as $PGAGROAL_LOG_FILE ... ok"
    else
        echo "log_file parameter is required but not found in \"$CONFIGURATION_DIRECTORY/pgagroal.conf\""
        return 1
    fi

    return 0
}
##############################################################

############### CHECK POSTGRES DEPENDENCIES ##################
check_inidb() {
    if which initdb > /dev/null 2>&1; then
        echo "check initdb in path ... ok"
        return 0
    else
        echo "check initdb in path ... not present"
        return 1
    fi
}

check_pgbench() {
    if which pgbench > /dev/null 2>&1; then
        echo "check pgbench in path ... ok"
        return 0
    else
        echo "check pgbench in path ... not present"
        return 1
    fi
}

check_port() {
    is_port_in_use $PORT
    if [ $? -ne 0 ]; then
        echo "check port ... $PORT"
        return 0
    else
        echo "port $PORT already in use ... not ok"
        return 1
    fi
}

check_pg_ctl() {
    if which pg_ctl > /dev/null 2>&1; then
        echo "check pg_ctl in path ... ok"
        return 0
    else
        echo "check pg_ctl in path ... not ok"
        return 1
    fi
}

stop_pgctl(){
   if [[ "$OS" == "FreeBSD" ]]; then
      su - postgres -c "$PGCTL_PATH -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop"
   else
      pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE stop
   fi
}

run_as_postgres() {
  if [[ "$OS" == "FreeBSD" ]]; then
    su - postgres -c "$*"
  else
    eval "$@"
  fi
}

check_psql() {
    if which psql > /dev/null 2>&1; then
        echo "check psql in path ... ok"
        return 0
    else
        echo "check psql in path ... not present"
        return 1
    fi
}

check_postgres_version() {
    version=$(psql --version | awk '{print $3}')
    major_version=$(echo "$version" | cut -d'.' -f1)
    required_major_version=$1
    if [ "$major_version" -ge "$required_major_version" ]; then
        echo "check postgresql version: $version ... ok"
        return 0
    else
        echo "check postgresql version: $version ... not ok"
        return 1
    fi
}

check_system_requirements() {
    echo -e "\e[34mCheck System Requirements \e[0m"
    echo "check system os ... $OS"
    check_inidb
    if [ $? -ne 0 ]; then
        exit 1
    fi
    check_pg_ctl
    if [ $? -ne 0 ]; then
        exit 1
    fi
    check_pgbench
    if [ $? -ne 0 ]; then
        exit 1
    fi
    check_psql
    if [ $? -ne 0 ]; then
        exit 1
    fi
    check_port
    if [ $? -ne 0 ]; then
        exit 1
    fi
    echo ""
}

initialize_log_files() {
    echo -e "\e[34mInitialize Test logfiles \e[0m"
    mkdir -p $LOG_DIRECTORY
    echo "create log directory ... $LOG_DIRECTORY"
    touch $PGAGROAL_LOG_FILE
    echo "create log file ... $PGAGROAL_LOG_FILE"
    touch $PGCTL_LOG_FILE
    echo "create log file ... $PGCTL_LOG_FILE"
    touch $PGBENCH_LOG_FILE
    echo "create log file ... $PGBENCH_LOG_FILE"
    echo ""
}
##############################################################

##################### POSTGRES OPERATIONS ####################
create_cluster() {
    local port=$1
    echo -e "\e[34mInitializing Cluster \e[0m"

    if [ "$OS" = "FreeBSD" ]; then
        mkdir -p "$POSTGRES_OPERATION_DIR"
        mkdir -p "$DATA_DIRECTORY"
        mkdir -p $CONFIGURATION_DIRECTORY
        if ! pw user show postgres >/dev/null 2>&1; then
            pw groupadd -n postgres -g 770
            pw useradd -n postgres -u 770 -g postgres -d /var/db/postgres -s /bin/sh
        fi
        chown postgres:postgres $PGCTL_LOG_FILE
        chown postgres:postgres $PGBENCH_LOG_FILE
        chown -R postgres:postgres "$DATA_DIRECTORY"
        chown -R postgres:postgres $CONFIGURATION_DIRECTORY
        chown -R postgres:postgres $TEST_CONFIG_DIRECTORY
    fi

    echo $DATA_DIRECTORY
   
    INITDB_PATH=$(command -v initdb)
    if [ -z "$INITDB_PATH" ]; then
        echo "Error: initdb not found!" >&2
        exit 1
    fi

    run_as_postgres "$INITDB_PATH -k -D $DATA_DIRECTORY"
    echo "initdb exit code: $?"
    echo "initialize database ... ok"
    set +e
    echo "setting postgresql.conf"
         LOG_ABS_PATH=$(realpath "$LOG_DIRECTORY")
   sed_i "s/^#*logging_collector.*/logging_collector = on/" "$DATA_DIRECTORY/postgresql.conf"
   sed_i "s/^#*log_destination.*/log_destination = 'stderr'/" "$DATA_DIRECTORY/postgresql.conf"
   sed_i "s|^#*log_directory.*|log_directory = '$LOG_ABS_PATH'|" "$DATA_DIRECTORY/postgresql.conf"
   sed_i "s/^#*log_filename.*/log_filename = 'logfile'/" "$DATA_DIRECTORY/postgresql.conf"

   # If any of the above settings are missing, append them
   grep -q "^logging_collector" "$DATA_DIRECTORY/postgresql.conf" || echo "logging_collector = on" >> "$DATA_DIRECTORY/postgresql.conf"
   grep -q "^log_destination" "$DATA_DIRECTORY/postgresql.conf" || echo "log_destination = 'stderr'" >> "$DATA_DIRECTORY/postgresql.conf"
   grep -q "^log_directory" "$DATA_DIRECTORY/postgresql.conf" || echo "log_directory = '$LOG_ABS_PATH'" >> "$DATA_DIRECTORY/postgresql.conf"
   grep -q "^log_filename" "$DATA_DIRECTORY/postgresql.conf" || echo "log_filename = 'logfile'" >> "$DATA_DIRECTORY/postgresql.conf"

    error_out=$(sed_i "s|#unix_socket_directories = '/var/run/postgresql'|unix_socket_directories = '/tmp'|" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting unix_socket_directories ... $error_out"
        clean
        exit 1
    else
        echo "setting unix_socket_directories ... '/tmp'"
    fi
    error_out=$(sed_i "s/#port = 5432/port = $port/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting port ... $error_out"
        clean
        exit 1
    else
        echo "setting port ... $port"
    fi
    error_out=$(sed_i "s/#max_connections = 100/max_connections = 200/" $DATA_DIRECTORY/postgresql.conf 2>&1)
    if [ $? -ne 0 ]; then
        echo "setting max_connections ... $error_out"
        clean
        exit 1
    else
        echo "setting max_connections ... 200"
    fi
    echo ""
}

initialize_hba_configuration() {
    echo -e "\e[34mCreate HBA Configuration \e[0m"
    echo "
    local   all             all                                     trust
    host    all             all             127.0.0.1/32            trust
    host    all             all             ::1/128                 trust
    host    replication     all             127.0.0.1/32            trust
    host    replication     all             ::1/128                 trust
    host    mydb            myuser          127.0.0.1/32            scram-sha-256
    host    mydb            myuser          ::1/128                 scram-sha-256
    " > $DATA_DIRECTORY/pg_hba.conf
    echo "initialize hba configuration at $DATA_DIRECTORY/pg_hba.conf ... ok"
    echo ""
}

initialize_cluster() {
    echo -e "\e[34mInitializing Cluster \e[0m"
    set +e

    PGCTL_PATH=$(command -v pg_ctl)
    if [ -z "$PGCTL_PATH" ]; then
        echo "Error: pg_ctl not found!" >&2
        exit 1
    fi

    run_as_postgres "$PGCTL_PATH -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE start"
    if [ $? -ne 0 ]; then
        clean
        exit 1
    fi
    pg_isready -h localhost -p $PORT
    if [ $? -eq 0 ]; then
        echo "postgres server is accepting requests ... ok"
    else
        echo "postgres server is not accepting response ... not ok"
        stop_pgctl
        clean
        exit 1
    fi
    err_out=$(psql -h localhost -p $PORT -U $PSQL_USER -d postgres -c "CREATE ROLE myuser WITH LOGIN PASSWORD '$PGPASSWORD';" 2>&1)
    if [ $? -ne 0 ]; then
        echo "create role myuser ... $err_out"
        stop_pgctl
        clean
        exit 1
    else
        echo "create role myuser ... ok"
    fi
    err_out=$(psql -h localhost -p $PORT -U $PSQL_USER -d postgres -c "CREATE DATABASE mydb WITH OWNER myuser;" 2>&1)
    if [ $? -ne 0 ]; then
        echo "create a database mydb with owner myuser ... $err_out"
        stop_pgctl
        clean
        exit 1
    else
        echo "create a database mydb with owner myuser ... ok"
    fi
    err_out=$(pgbench -i -s 1 -n -h localhost -p $PORT -U $PSQL_USER -d postgres 2>&1)
    if [ $? -ne 0 ]; then
        echo "initialize pgbench ... $err_out"
        stop_pgctl
        clean
        exit 1
    else
        echo "initialize pgbench on user: $PSQL_USER and database: postgres ... ok"
    fi
    set -e
    stop_pgctl
    echo ""
}

clean_logs() {
    if [ -d $LOG_DIRECTORY ]; then
        rm -r $LOG_DIRECTORY
        echo "remove log directory $LOG_DIRECTORY ... ok"
    else
        echo "$LOG_DIRECTORY not present ... ok"
    fi
}

clean() {
    echo -e "\e[34mClean Test Resources \e[0m"
    if [ -d $POSTGRES_OPERATION_DIR ]; then
        rm -r $POSTGRES_OPERATION_DIR
        echo "remove postgres operations directory $POSTGRES_OPERATION_DIR ... ok"
    else
      echo "$POSTGRES_OPERATION_DIR not present ... ok"
    fi

    if [ -d $PGAGROAL_OPERATION_DIR ]; then
        rm -r $PGAGROAL_OPERATION_DIR
        echo "remove pgagroal operations directory $PGAGROAL_OPERATION_DIR ... ok"
    else
        echo "$PGAGROAL_OPERATION_DIR not present ... ok"
    fi
}

##############################################################

#################### PGAGROAL OPERATIONS #####################
pgagroal_initialize_configuration() {
    echo -e "\e[34mInitialize pgagroal configuration files \e[0m"
    mkdir -p $CONFIGURATION_DIRECTORY
    echo "create configuration directory $CONFIGURATION_DIRECTORY ... ok"
    touch $CONFIGURATION_DIRECTORY/pgagroal.conf $CONFIGURATION_DIRECTORY/pgagroal_hba.conf
    cat << EOF > $CONFIGURATION_DIRECTORY/pgagroal.conf
[pgagroal]
host = localhost
port = 2345

log_type = file
log_level = debug5
log_path = $PGAGROAL_LOG_FILE

max_connections = 100
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/
pipeline = 'performance'

[primary]
host = localhost
port = $PORT

EOF

    echo "create pgagroal.conf inside $CONFIGURATION_DIRECTORY ... ok"
    cat << EOF > $CONFIGURATION_DIRECTORY/pgagroal_hba.conf
host    all all all all
EOF
    echo "create pgagroal_hba.conf inside $CONFIGURATION_DIRECTORY ... ok"
    if [[ "$OS" == "FreeBSD" ]]; then
        chown -R postgres:postgres $CONFIGURATION_DIRECTORY
        chown -R postgres:postgres $PGAGROAL_LOG_FILE
    fi
    echo ""
}

execute_testcases() {
    if [ $# -eq 1 ]; then
        local config_dir=$1
        echo -e "\e[34mExecute Testcases for config:$config_dir\e[0m"
    else
        echo -e "\e[34mExecute Testcases \e[0m"
    fi

    set +e
    run_as_postgres "pg_ctl -D $DATA_DIRECTORY -l $PGCTL_LOG_FILE start"

    pg_isready -h localhost -p $PORT
    if [ $? -eq 0 ]; then
        echo "postgres server accepting requests ... ok"
    else
        echo "postgres server is not accepting response ... not ok"
        stop_pgctl
        clean
        exit 1
    fi

    run_as_postgres "$EXECUTABLE_DIRECTORY/pgagroal -c $CONFIGURATION_DIRECTORY/pgagroal.conf -a $CONFIGURATION_DIRECTORY/pgagroal_hba.conf -d"
    wait_for_server_ready $PGAGROAL_PORT
    if [ $? -eq 0 ]; then
        echo "pgagroal server started in daemon mode ... ok"
    else
        echo "pgagroal server not started ... not ok"
        stop_pgctl
        clean
        exit 1
    fi

    ### RUN TESTCASES ###
    run_as_postgres "$TEST_DIRECTORY/pgagroal_test $PROJECT_DIRECTORY"
    if [ $? -ne 0 ]; then
        stop_pgctl
        clean
        exit 1
    fi

    run_as_postgres "$EXECUTABLE_DIRECTORY/pgagroal-cli -c $CONFIGURATION_DIRECTORY/pgagroal.conf shutdown"
    echo "shutdown pgagroal server ... ok"

    stop_pgctl
    set -e
    echo ""
}

##############################################################

run_tests() {
    PORT=$(next_available_port $PORT)

    ## Postgres operations
    check_system_requirements

    initialize_log_files
    create_cluster $PORT

    initialize_hba_configuration
    initialize_cluster

    pgagroal_initialize_configuration
    execute_testcases
    # clean cluster
    clean
}

run_multiple_config_tests() {
    PORT=$(next_available_port $PORT)

    ## Postgres operations
    check_system_requirements

    initialize_log_files
    create_cluster $PORT

    initialize_hba_configuration
    initialize_cluster

    pgagroal_initialize_configuration

    if [ -d $TEST_CONFIG_DIRECTORY ]; then
        for entry in "$TEST_CONFIG_DIRECTORY"/*; do
            entry=$(realpath $entry)
            if [[ -d "$entry" && -f "$entry/pgagroal.conf" && -f "$entry/pgagroal_hba.conf" ]]; then
                run_as_postgres "cp $entry/pgagroal.conf $CONFIGURATION_DIRECTORY/pgagroal.conf"
                run_as_postgres "cp $entry/pgagroal_hba.conf $CONFIGURATION_DIRECTORY/pgagroal_hba.conf"
                verify_configuration
                if [ $? -ne 0 ]; then
                    continue
                fi
                execute_testcases $entry
            else
                # warning (yellow text)
                echo "either '$entry' is not a directory"
                echo "or 'pgagroal.conf or pgagroal_hba.conf is not present'"
                echo "conditions of a configuration directory are not met ... not ok"
                echo ""
            fi
        done
        ## Clean test environment
        clean
    else
        echo "configuration directory $TEST_CONFIG_DIRECTORY not present"
        clean
        exit 1
    fi
}

usage() {
    echo "Usage: $0 [ COMMAND ]"
    echo " Command:"
    echo "   clean                   clean up test suite environment"
    echo "   run-configs             run the testsuite on multiple pgagroal configurations"
    exit 1
}

if [ $# -eq 0 ]; then
    # If no arguments are provided, run function_without_param
    run_tests
elif [ $# -eq 1 ]; then
    if [ "$1" == "clean" ]; then
        # If the parameter is 'clean', run clean_function
        clean
        clean_logs
    elif [ "$1" == "run-configs" ]; then
        # If the first parameter is '-C' or '--config-dir', run function_with_param
        run_multiple_config_tests
    else
        echo "Invalid parameter: $1"
        usage  # If an invalid parameter is provided, show usage and exit
    fi
# elif [ $# -eq 2 ]; then
#     if [ "$1" == "-C" ] || [ "$1" == "--config-dir" ]; then
#         # If the first parameter is '-C' or '--config-dir', run function_with_param
#         run_multiple_config_tests
#     else
#         echo "Invalid parameter: $1"
#         usage  # If an invalid parameter is provided, show usage and exit
#     fi
else
    usage  # If an invalid number of parameters is provided, show usage and exit
fi
