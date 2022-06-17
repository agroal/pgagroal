# Split security model in pgagroal

This tutorial will show you how to split the security model of pgagroal such that
applications will use a different password than the one used against PostgreSQL.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgagroal.

See [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/main/doc/tutorial/01_install.md)
for more detail.

## Create pgagroal_frontend_users.conf

Create the configuration file

```
pgagroal-admin -f pgagroal_frontend_users.conf -U myuser -P apppass add-user
```

You need a password mapping for each user defined in the `pgagroal_users.conf` configuration file.

(`pgagroal` user)

## Restart pgagroal

Stop pgagroal and start it again with

```
pgagroal-cli -c pgagroal.conf stop
pgagroal -c pgagroal.conf -a pgagroal_hba.conf -u pgagroal_users.conf -F pgagroal_frontend_users.conf
```

(`pgagroal` user)

## Connect to PostgreSQL

You can now use the "application password" to access the PostgreSQL instance

```
psql -h localhost -p 2345 -U myuser mydb
```

using `apppass` as the password. pgagroal will use the `mypass` password against PostgreSQL.

Using this split security model allow you to use other passwords than used on the PostgreSQL
instance.

(`pgagroal` user)
