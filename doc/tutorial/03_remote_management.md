## Remote administration for pgagroal

This tutorial will show you how to do setup remote management for [**pgagroal**](https://github.com/agroal/pgagroal).

[**pgagroal**](https://github.com/agroal/pgagroal) is managed via a command line tool named `pgagroal-cli`. Such tool
connects via a local Unix socket if running on the same machine the pooler is
running on, but it is possible to use `pgagroal-cli` from a different machine
and make it to connect to the pooler machine via *remote management*.

## Preface

This tutorial assumes that you have already an installation of [PostgreSQL](https://www.postgresql.org) 13 (or higher) and [**pgagroal**](https://github.com/agroal/pgagroal).

In particular, this tutorial refers to the configuration done in [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/master/doc/tutorial/01_install.md).

### Enable remote management

On the pooler machine, you need to enable the remote management. In order to do so,
add the `management` setting to the main `pgagroal.conf` configuration file.
The value of setting is the number of a free TCP/IP port to which the remote
management will connect to.

With your editor of choice, edit the `/etc/pgagroal/pgagroal.conf` file and add the
`management` option likely the following:

```
management = 2347
```

under the `[pgagroal]` section, so that the configuration file looks like:

```
[pgagroal]
...
management = 2347
```

See [the pgagroal configuration settings](https://github.com/agroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal) for more details.

### Add remote admin user

Remote management is done via a specific admin user, that has to be created within the pooler vault.

As the [**pgagroal**](https://github.com/agroal/pgagroal) operating system user, run the following command:

```
cd /etc/pgagroal
pgagroal-admin -f pgagroal_admins.conf -U admin -P admin1234 add-user
```

The above will create the `admin` username with the `admin1234` password.

**We strongly encourage you to choose non trivial usernames and passwords!**

### Restart pgagroal

In order to make the changes available, and therefore activate the remote management, you have to restart [**pgagroal**](https://github.com/agroal/pgagroal), for example by issuing the following commands from the [**pgagroal**](https://github.com/agroal/pgagroal) operatng system user:

```
pgagroal-cli shutdown
pgagroal -d
```

Since the default configuration files are usually searched into the `/etc/pgagroal/` directory, and have well defined names, you can omit the files from the command line
if you named them `pgagroal.conf`, `pgagroal_hba.conf`, `pgagroal_users.conf` and `pgagroal_admins.conf`.

### Connect via remote administration interface

In order to connect remotely, you need to specify at least the `-h` and `-p` flags on the `pgagroal-cli` command line. Such flags will tell `pgagroal-cli` to connect to a remote host. You can also specify the username you want to connect with by specifying the `-U` flag.
So, to get the status of the pool remotely, you can issue:

```
pgagroal-cli -h localhost -p 2347 -U admin status
```

and type the password `admin1234` when asked for it.

If you don't specify the `-U` flag on the command line, you will be asked for a username too.

Please note that the above example uses `localhost` as the remote host, but clearly you can specify any *real* remote host you want to manage.
