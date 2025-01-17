## Split security model in pgagroal

This tutorial will show you how to split the security model of [**pgagroal**](https://github.com/agroal/pgagroal).

The idea is that the pooler can act as a *user proxy* between your application and
the [PostgreSQL](https://www.postgresql.org) instance, so that your application does not need to know the exact password
to use to connect to [PostgreSQL](https://www.postgresql.org).
[**pgagroal**](https://github.com/agroal/pgagroal) will authenticate the connection request with its credentials, and then will
authenticate against [PostgreSQL](https://www.postgresql.org) with the correct password.

This *user mapping* is named *frontend users*.

### Preface

This tutorial assumes that you have already an installation of [PostgreSQL](https://www.postgresql.org) 13 (or higher) and [**pgagroal**](https://github.com/agroal/pgagroal).

In particular, this tutorial refers to the configuration done in [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/master/doc/tutorial/01_install.md).


### Create frontend users

Frontend users are stored into the `pgagroal_frontend_users.conf` file, that can be managed via the `pgagroal-admin` command line tool.
See [the documentation on frontend users](https://github.com/agroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal_frontend_users-configuration) for more details.

As an example, consider the user `myuser` created in the [Installing pgagroal tutorial](https://github.com/pgagroal/pgagroal/blob/master/doc/tutorial/01_install.md)): such user has the `mypassword` password defined on the [PostgreSQL](https://www.postgresql.org) side. It is possible to *remap* the user password on the [**pgagroal**](https://github.com/agroal/pgagroal) side, so that an application can connect to the [**pgagroal**](https://github.com/agroal/pgagroal) using a different password, like `application_password`. In turn, [**pgagroal**](https://github.com/agroal/pgagroal) will connect to [PostgreSQL](https://www.postgresql.org) using the `mypassword` password. Therefore, the application could not know the *real* password used to connect to [PostgreSQL](https://www.postgresql.org).

To achieve this, as [**pgagroal**](https://github.com/agroal/pgagroal) operating system run the following command:

```
pgagroal-admin -f /etc/pgagroal/pgagroal_frontend_users.conf -U myuser -P application_password user add
```

([**pgagroal**](https://github.com/agroal/pgagroal) user)

You will need a password mapping for each user defined in the `pgagroal_users.conf` configuration file.

### Restart pgagroal

In order to apply changes, you need to restart [**pgagroal**](https://github.com/agroal/pgagroal), so as the [**pgagroal**](https://github.com/agroal/pgagroal) operating system user do:

```
pgagroal-cli shutdown
pgagroal -d
```

### Connect to PostgreSQL

You can now use the "application password" to access the [PostgreSQL](https://www.postgresql.org) instance. As an example,
run the following as any operatng system user:

```
psql -h localhost -p 2345 -U myuser mydb
```

using `application_password` as the password.
As already explained, [**pgagroal**](https://github.com/agroal/pgagroal) will then use the `mypassword` password against [PostgreSQL](https://www.postgresql.org).

This **split security model** allows you to avoid sharing password between applications and [PostgreSQL](https://www.postgresql.org),
letting the [**pgagroal**](https://github.com/agroal/pgagroal) to be the secret-keeper. This not only improves security, but also allows you
to change the [PostgreSQL](https://www.postgresql.org) password without having the application to note it.
