# Enable prefill for pgagroal

This tutorial will show you how to do enable prefill for pgagroal

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgagroal.

See [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/main/doc/tutorial/01_install.md)
for more detail.

## Create prefill configuration

Create the `pgagroal_databases.conf` configuration

```
cat > pgagroal_databases.conf
mydb   myuser   2   1   0
```

and press `Ctrl-D`

This will create a configuration where `mydb` will have a maximum connection size of 2,
an initial connection size of 1 and a minimum connection size of 0 for the `myuser` user.

(`pgagroal` user)

## Restart pgagroal

Stop pgagroal and start it again with

```
pgagroal-cli -c pgagroal.conf stop
pgagroal -c pgagroal.conf -a pgagroal_hba.conf -u pgagroal_users.conf -l pgagroal_databases.conf
```

(`pgagroal` user)
