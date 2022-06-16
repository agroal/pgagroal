# Remote administration for pgagroal

This tutorial will show you how to do setup remote management for pgagroal.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgagroal.

See [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/main/doc/tutorial/01_install.md)
for more detail.

## Change the pgagroal configuration

Change `pgagroal.conf` to add

```
management = 2347
```

under the `[pgagroal]` setting, like

```
[pgagroal]
...
management = 2347
```

(`pgagroal` user)

## Add pgagroal admin

```
pgagroal-admin -f pgagroal_admins.conf -U admin -P admin1234 add-user
```

(`pgagroal` user)

## Restart pgagroal

Stop pgagroal and start it again with

```
pgagroal-cli -c pgagroal.conf stop
pgagroal -c pgagroal.conf -a pgagroal_hba.conf -u pgagroal_users.conf -A pgagroal_admins.conf
```

(`pgagroal` user)

## Connect via remote administration interface

```
pgagroal-cli -h localhost -p 2347 -U admin details
```

and use `admin1234` as the password

(`pgagroal` user)
