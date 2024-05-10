\newpage

# Quick start

Make sure that [**pgagroal**][pgagroal] is installed and in your path by using `pgagroal -?`. You should see

``` console
pgagroal 1.7.0
  High-performance connection pool for PostgreSQL

Usage:
  pgagroal [ -c CONFIG_FILE ] [ -a HBA_FILE ] [ -d ]

Options:
  -c, --config CONFIG_FILE           Set the path to the pgagroal.conf file
  -a, --hba HBA_FILE                 Set the path to the pgagroal_hba.conf file
  -l, --limit LIMIT_FILE             Set the path to the pgagroal_databases.conf file
  -u, --users USERS_FILE             Set the path to the pgagroal_users.conf file
  -F, --frontend FRONTEND_USERS_FILE Set the path to the pgagroal_frontend_users.conf file
  -A, --admins ADMINS_FILE           Set the path to the pgagroal_admins.conf file
  -S, --superuser SUPERUSER_FILE     Set the path to the pgagroal_superuser.conf file
  -d, --daemon                       Run as a daemon
  -V, --version                      Display version information
  -?, --help                         Display help
```

If you encounter any issues following the above steps, you can refer to the **Installation** chapter to see how to install or compile pgagroal on your system.

## Configuration

Lets create a simple configuration file called `pgagroal.conf` with the content

``` ini
[pgagroal]
host = *
port = 2345

log_type = file
log_level = info
log_path = /tmp/pgagroal.log

max_connections = 100
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
```

In our main section called `[pgagroal]` we setup [**pgagroal**][pgagroal] to listen on all network addresses. Logging will be performed at `info` level and put in a file called `/tmp/pgagroal.log`.
We will use 100 connections in the pool with a idle timeout of 10 minutes. Validation is off. Last we specify the location of the `unix_socket_dir` used for management operations and the path for the PostgreSQL command line tools.

Next we create a section called `[primary]` which has the information about our [PostgreSQL][postgresql] instance. In this case it is running on `localhost` on port `5432`.

Now we need a host based authentication (HBA) file. Create one called `pgagroal_hba.conf`
with the content

```
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

This tells [**pgagroal**](https://github.com/agroal/pgagroal) that it can accept connections from all network addresses
for all databases and all user names.

We are now ready to run [**pgagroal**](https://github.com/agroal/pgagroal).

See the **Configuration** charpter for all configuration options.

## Running

We will run [**pgagroal**][pgagroal] using the command

``` sh
pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

If this doesn't give an error, then we are ready to do backups.

[**pgagroal**][pgagroal] is stopped by pressing Ctrl-C (`^C`) in the console where you started it, or by sending the `SIGTERM` signal to the process using `kill <pid>`.

## Run-time administration

[**pgagroal**][pgagroal] has a run-time administration tool called `pgagroal-cli`.

You can see the commands it supports by using `pgagroal-cli -?` which will give

``` console
pgagroal-cli 1.7.0
  Command line utility for pgagroal

Usage:
  pgagroal-cli [ OPTIONS ] [ COMMAND ] 

Options:
  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file
                           Default: /etc/pgagroal/pgagroal.conf
  -h, --host HOST          Set the host name
  -p, --port PORT          Set the port number
  -U, --user USERNAME      Set the user name
  -P, --password PASSWORD  Set the password
  -L, --logfile FILE       Set the log file
  -F, --format text|json   Set the output format
  -v, --verbose            Output text string of result
  -V, --version            Display version information
  -?, --help               Display help

Commands:
  flush [mode] [database]  Flush connections according to <mode>.
                           Allowed modes are:
                           - 'gracefully' (default) to flush all connections gracefully
                           - 'idle' to flush only idle connections
                           - 'all' to flush all connections. USE WITH CAUTION!
                           If no <database> name is specified, applies to all databases.
  ping                     Verifies if pgagroal is up and running
  enable   [database]      Enables the specified databases (or all databases)
  disable  [database]      Disables the specified databases (or all databases)
  shutdown [mode]          Stops pgagroal pooler. The <mode> can be:
                           - 'gracefully' (default) waits for active connections to quit
                           - 'immediate' forces connections to close and terminate
                           - 'cancel' avoid a previously issued 'shutdown gracefully'
  status [details]         Status of pgagroal, with optional details
  switch-to <server>       Switches to the specified primary server
  conf <action>            Manages the configuration (e.g., reloads the configuration
                           The subcommand <action> can be:
                           - 'reload' to issue a configuration reload;
                           - 'get' to obtain information about a runtime configuration value;
                                   conf get <parameter_name>
                           - 'set' to modify a configuration value;
                                   conf set <parameter_name> <parameter_value>;
                           - 'ls'  lists the configuration files used.
  clear <what>             Resets either the Prometheus statistics or the specified server.
                           <what> can be
                           - 'server' (default) followed by a server name
                           - a server name on its own
                           - 'prometheus' to reset the Prometheus metrics

pgagroal: <https://agroal.github.io/pgagroal/>
Report bugs: <https://github.com/agroal/pgagroal/issues>
```

This tool can be used on the machine running [**pgagroal**](https://github.com/agroal/pgagroal) to flush connections.

To flush all idle connections you would use

```
pgagroal-cli -c pgagroal.conf flush idle
```

To stop pgagroal you would use

```
pgagroal-cli -c pgagroal.conf stop
```

Check the outcome of the operations by verifying the exit code, like

```
echo $?
```

or by using the `-v` flag.

If pgagroal has both Transport Layer Security (TLS) and `management` enabled then `pgagroal-cli` can
connect with TLS using the files `~/.pgagroal/pgagroal.key` (must be 0600 permission),
`~/.pgagroal/pgagroal.crt` and `~/.pgagroal/root.crt`.

## Administration

[**pgagroal**][pgagroal] has an administration tool called `pgagroal-admin`, which is used to control user registration with [**pgagroal**][pgagroal].

You can see the commands it supports by using `pgagroal-admin -?` which will give

``` console
pgagroal-admin 1.7.0
 Administration utility for pgagroal

Usage:
  pgagroal-admin [ -f FILE ] [ COMMAND ]

Options:
  -f, --file FILE         Set the path to a user file
                          Defaults to /etc/pgagroal/pgagroal_users.conf
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -V, --version           Display version information
  -?, --help              Display help

Commands:
  master-key              Create or update the master key
  user <subcommand>       Manage a specific user, where <subcommand> can be
                          - add  to add a new user
                          - del  to remove an existing user
                          - edit to change the password for an existing user
                          - ls   to list all available users

pgagroal: https://agroal.github.io/pgagroal/
Report bugs: https://github.com/agroal/pgagroal/issues
```

In order to set the master key for all users you can use

``` sh
pgagroal-admin -g master-key
```

The master key must be at least 8 characters.

Then use the other commands to add, update, remove or list the current user names, f.ex.

``` sh
pgagroal-admin -f pgagroal_users.conf user add
```

## Next Steps

Next steps in improving pgagroal's configuration could be

* Update `pgagroal.conf` with the required settings for your system
* Set the access rights in `pgagroal_hba.conf` for each user and database
* Add a `pgagroal_users.conf` file using `pgagroal-admin` with a list of known users
* Disable access for unknown users by setting `allow_unknown_users` to `false`
* Define a `pgagroal_databases.conf` file with the limits and prefill settings for each database
* Enable Transport Layer Security v1.2+ (TLS)
* Deploy Grafana dashboard

See [Configuration][configuration] for more information on these subjects.
