# Prometheus metrics for pgagroal

This tutorial will show you how to do setup [Prometheus](https://prometheus.io/) metrics for pgagroal.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgagroal.

See [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/main/doc/tutorial/01_install.md)
for more detail.

## Change the pgagroal configuration

Change `pgagroal.conf` to add

```
metrics = 2346
```

under the `[pgagroal]` setting, like

```
[pgagroal]
...
metrics = 2346
```

(`pgagroal` user)

## Restart pgagroal

Stop pgagroal and start it again with

```
pgagroal-cli -c pgagroal.conf stop
pgagroal -c pgagroal.conf -a pgagroal_hba.conf -u pgagroal_users.conf
```

(`pgagroal` user)

## Get Prometheus metrics

You can now access the metrics via

```
http://localhost:2346/metrics
```

(`pgagroal` user)
