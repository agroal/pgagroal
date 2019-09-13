# Getting started with pgagroal

First of all, make sure that `pgagroal` is installed and in your path by
using `pgagroal -?`. You should see

```sh
pgagroal 0.1.0
  High-performance connection pool for PostgreSQL

Usage:
  pgagroal [ -c CONFIG_FILE ] [ -a HBA_CONFIG_FILE ]

Options:
  -c, --config CONFIG_FILE  Set the path to the pgagroal.conf file
  -a, --hba HBA_CONFIG_FILE Set the path to the pgagroal_hba.conf file
  -V, --version             Display version information
  -?, --help                Display help
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

idle_timeout = 600
validation = off
unix_socket_dir = /tmp/.s.pgagroal

[primary]
host = localhost
port = 5432
```

In our main section called `[pgagroal]` we setup `pgagroal` to listen on all
network addresses on port 2345. Logging will be performed at `info` level and
put in a file called `/tmp/pgagroal.log`. Then we specify that we want connections
to be closed if they have been idle for 10 minutes, and we also specific that
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
for all database and all user names.

We are now ready to run `pgagroal`.

See [Configuration](./CONFIGURATION.md) for all configuration options.

## Running

We will run `pgagroal` using the command

```sh
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

```sh
psql -h localhost -p 2345 -U test test
```

That should give you a password prompt where `test` should be typed in. You are now connected
to [PostgreSQL](https://www.postgresql.org) through `pgagroal`.

Type `\q` to quit [psql](https://www.postgresql.org/docs/current/app-psql.html) and `pgagroal`
will now put the connection that you used into its pool.

If you type the above `psql` command again `pgagroal` will reuse the existing connection and
thereby lower the overhead of getting a connection to [PostgreSQL](https://www.postgresql.org).

Now you are ready to point your applications to use `pgagroal` instead of going directly to
[PostgreSQL](https://www.postgresql.org).

`pgagroal` is stopped by pressing Ctrl-C (`^C`) in the console where you started it, or by sending
the `SIGTERM` signal to the process using `kill <pid>`.

## Administration

`pgagroal` has an administration tool called `pgagroal-cli`.

You can see the commands it supports by using `pgagroal-cli -?` which will give

```
pgagroal-cli 0.1.0
  Command line utility for pgagroal

Usage:
  pgagroal-cli [ -f CONFIG_FILE ] [ COMMAND ] 

Options:
  -c, --config CONFIG_FILE Set the path to the pgagroal.conf file
  -V, --version            Display version information
  -?, --help               Display help

Commands:
  flush-idle               Flush idle connections
  flush-gracefully         Flush all connections gracefully
  flush-all                Flush all connections. USE WITH CAUTION !
```

This tool can be used on the machine running `pgagroal` to flush connections.

To flush all idle connections you would use

```sh
pgagroal-cli -c pgagroal.conf flush-idle
```

## Closing

The [pgagroal](https://github.com/agroal/pgagroal) community hopes that you find
the project interesting.

Feel free to

* [Raise an issue or a question](https://github.com/agroal/pgagroal/issues)
* [Submit a feature request](https://github.com/agroal/pgagroal/issues)
* [Write a code submission](https://github.com/agroal/pgagroal/pulls)

All contributions are most welcome !
