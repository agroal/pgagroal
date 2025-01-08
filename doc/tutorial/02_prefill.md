## Enable prefill for pgagroal

This tutorial will show you how to do enable *prefill* for [**pgagroal**](https://github.com/agroal/pgagroal).
The prefill is the capability to activate connections against a specific database
even if no one has been actively requested by a user or an application. This allows the pooler
to serve a connection faster once it is effectively requested, since the connection is already
established.

Prefill is done by making [**pgagroal**](https://github.com/agroal/pgagroal) to open the specified amount of connections to a specific database with a specific username, therefore you need to know credentials used on the PostgreSQL side.

### Preface

This tutorial assumes that you have already an installation of PostgreSQL 12 (or higher) and [**pgagroal**](https://github.com/agroal/pgagroal).

In particular, this tutorial refers to the configuration done in [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/master/doc/tutorial/01_install.md).


### Create prefill configuration

Prefill is instrumented by the `pgagroal_databases.conf` configuration file, where you need
to list databases, usernames, and limits.
Every username/database pair has to be specified on a separated line.
The limits are assumed as:
- *max number of allowed connections* for that username/database
- *initial number of connections*, that is the effective prefill;
- *minimum number of connections* to always keep open for the pair username/database.

Assuming you want to configure the prefill for the `mydb` database with the `myuser` username,
you have to edit the file `/etc/pgagroal/pgagroal_databases.conf` with your editor of choice
or using `cat` from the command line, as follows:

```
cd /etc/pgagroal
cat > pgagroal_databases.conf
mydb   myuser   2   1   0
```

and press `Ctrl-D` to save the file.

This will create a configuration where `mydb` will have a maximum connection size of 2,
an initial connection size of 1 and a minimum connection size of 0 for the `myuser` user.

The file must be owned by the operating system user [**pgagroal**](https://github.com/agroal/pgagroal).

The `max_size` value is mandatory, while the `initial_size` and `min_size` are optional and if not explicitly set are assumed to be `0`.
See [the `pgagroal_databases.conf` file documentation](https://github.com/agroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal_databases-configuration) for more details.

### Restart pgagroal

In order to apply changes to the prefill configuration, you need to restart [**pgagroal**](https://github.com/agroal/pgagroal).
You can do so by stopping it and then re-launch the daemon, as [**pgagroal**](https://github.com/agroal/pgagroal) operating system user:

```
pgagroal-cli -c /etc/pgagroal/pgagroal.conf shutdown
pgagroal -c /etc/pgagroal/pgagroal.conf -a /etc/pgagroal/pgagroal_hba.conf -u /etc/pgagroal/pgagroal_users.conf -l /etc/pgagroal/pgagroal_databases.conf
```

Note that the limit file `pgagroal_databases.conf` has been specified by means of the `l` command line flag.
If the file has the standard name `/etc/pgagroal/pgagroal_databases.conf`, you can omit it from the command line.


### Check the prefill

You can check the prefill by running, as the [**pgagroal**](https://github.com/agroal/pgagroal) operating system user, the `status` command:

```
pgagroal-cli status
Status:              Running
Active connections:  0
Total connections:   1
Max connections:     100

```

where the `Total connections` is set by the *initial* connection specified in the limit file.
