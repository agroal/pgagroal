# Getting started with pgagroal

First of all, make sure that `pgagroal` is installed and in your path by
using `pgagroal -?`. You should see

```
pgagroal 1.3.2
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

If you don't have `pgagroal` in your path see [README](../README.md) on how to
compile and install `pgagroal` in your system.

## Configuration

Lets create a simple configuration file called `pgagroal.conf` with the content

```
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

In our main section called `[pgagroal]` we setup `pgagroal` to listen on all
network addresses on port 2345. Logging will be performed at `info` level and
put in a file called `/tmp/pgagroal.log`. We want a maximum of 100 connections
that are being closed if they have been idle for 10 minutes, and we also specify that
we don't want any connection validation to be performed. Last we specify the
location of the `unix_socket_dir` used for management operations.

Next we create a section called `[primary]` which has the information about our
[PostgreSQL](https://www.postgresql.org) instance. In this case it is running
on `localhost` on port `5432`.

Now we need a host based authentication (HBA) file. Create one called `pgagroal_hba.conf`
with the content

```
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

This tells `pgagroal` that it can accept connections from all network addresses
for all databases and all user names.

We are now ready to run `pgagroal`.

See [Configuration](./CONFIGURATION.md) for all configuration options.

## Running

We will run `pgagroal` using the command

```
pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

If this doesn't give an error, then we are ready to connect.

We will assume that we have a user called `test` with the password `test` in our
[PostgreSQL](https://www.postgresql.org) instance. See their
[documentation](https://www.postgresql.org/docs/current/index.html) on how to setup
[PostgreSQL](https://www.postgresql.org), [add a user](https://www.postgresql.org/docs/current/app-createuser.html)
and [add a database](https://www.postgresql.org/docs/current/app-createdb.html).

We will connect to `pgagroal` using the [psql](https://www.postgresql.org/docs/current/app-psql.html)
application.

```
psql -h localhost -p 2345 -U test test
```

That should give you a password prompt where `test` should be typed in. You are now connected
to [PostgreSQL](https://www.postgresql.org) through `pgagroal`.

Type `\q` to quit [psql](https://www.postgresql.org/docs/current/app-psql.html) and `pgagroal`
will now put the connection that you used into its pool.

If you type the above `psql` command again `pgagroal` will reuse the existing connection and
thereby lower the overhead of getting a connection to [PostgreSQL](https://www.postgresql.org).

Now you are ready to point your applications to use `pgagroal` instead of going directly to
[PostgreSQL](https://www.postgresql.org). `pgagroal` will work with any
[PostgreSQL](https://www.postgresql.org) compliant driver, for example [pgjdbc](https://jdbc.postgresql.org/),
[Npgsql](https://www.npgsql.org/) and [pq](https://github.com/lib/pq).

`pgagroal` is stopped by pressing Ctrl-C (`^C`) in the console where you started it, or by sending
the `SIGTERM` signal to the process using `kill <pid>`.

## Run-time administration

`pgagroal` has a run-time administration tool called `pgagroal-cli`.

You can see the commands it supports by using `pgagroal-cli -?` which will give

```
pgagroal-cli 1.3.2
  Command line utility for pgagroal

Usage:
  pgagroal-cli [ -c CONFIG_FILE ] [ COMMAND ]

Options:
  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file
  -h, --host HOST          Set the host name
  -p, --port PORT          Set the port number
  -U, --user USERNAME      Set the user name
  -P, --password PASSWORD  Set the password
  -L, --logfile FILE       Set the log file
  -v, --verbose            Output text string of result
  -V, --version            Display version information
  -?, --help               Display help

Commands:
  flush-idle               Flush idle connections
  flush-gracefully         Flush all connections gracefully
  flush-all                Flush all connections. USE WITH CAUTION !
  is-alive                 Is pgagroal alive
  enable                   Enable a database
  disable                  Disable a database
  gracefully               Stop pgagroal gracefully
  stop                     Stop pgagroal
  cancel-shutdown          Cancel the graceful shutdown
  status                   Status of pgagroal
  details                  Detailed status of pgagroal
  switch-to                Switch to another primary
  reload                   Reload the configuration
  reset                    Reset the Prometheus statistics
  reset-server             Reset the state of a server
```

This tool can be used on the machine running `pgagroal` to flush connections.

To flush all idle connections you would use

```
pgagroal-cli -c pgagroal.conf flush-idle
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

`pgagroal` has an administration tool called `pgagroal-admin`, which is used to control user
registration with `pgagroal`.

You can see the commands it supports by using `pgagroal-admin -?` which will give

```
pgagroal-admin 1.3.2
  Administration utility for pgagroal

Usage:
  pgagroal-admin [ -f FILE ] [ COMMAND ]

Options:
  -f, --file FILE         Set the path to a user file
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -V, --version           Display version information
  -?, --help              Display help

Commands:
  master-key              Create or update the master key
  add-user                Add a user
  update-user             Update a user
  remove-user             Remove a user
  list-users              List all users
```

In order to set the master key for all users you can use

```
pgagroal-admin -g master-key
```

The master key must be at least 8 characters.

Then use the other commands to add, update, remove or list the current user names, f.ex.

```
pgagroal-admin -f pgagroal_users.conf add-user
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

See [Configuration](./CONFIGURATION.md) for more information on these subjects.

## Closing

The [pgagroal](https://github.com/agroal/pgagroal) community hopes that you find
the project interesting.

Feel free to

* [Ask a question](https://github.com/agroal/pgagroal/discussions)
* [Raise an issue](https://github.com/agroal/pgagroal/issues)
* [Submit a feature request](https://github.com/agroal/pgagroal/issues)
* [Write a code submission](https://github.com/agroal/pgagroal/pulls)

All contributions are most welcome !

Please, consult our [Code of Conduct](../CODE_OF_CONDUCT.md) policies for interacting in our
community.

Consider giving the project a [star](https://github.com/agroal/pgagroal/stargazers) on
[GitHub](https://github.com/agroal/pgagroal/) if you find it useful. And, feel free to follow
the project on [Twitter](https://twitter.com/pgagroal/) as well.
