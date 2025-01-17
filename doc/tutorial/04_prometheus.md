## Prometheus metrics for pgagroal

This tutorial will show you how to do basic  [Prometheus](https://prometheus.io/) setup  for [**pgagroal**](https://github.com/agroal/pgagroal).

[**pgagroal**](https://github.com/agroal/pgagroal) is able to provide a set of metrics about what it is happening within the pooler,
so that a Prometheus instance can collect them and help you monitor the pooler.

### Preface

This tutorial assumes that you have already an installation of [PostgreSQL](https://www.postgresql.org) 13 (or higher) and [**pgagroal**](https://github.com/agroal/pgagroal).

In particular, this tutorial refers to the configuration done in [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/master/doc/tutorial/01_install.md).

### Change the pgagroal configuration

In order to enable to export of the metrics, you need to add the `metrics` option in the main `pgagroal.conf` configuration. The value of this setting is the TCP/IP port number that Prometheus will use to grab the exported metrics.

Add a line like the following to `/etc/pgagroal/pgagroal.conf` by editing such file with your editor of choice:

```
metrics = 2346
```

Place it withingr the `[pgagroal]` section, like

```
[pgagroal]
...
metrics = 2346
```

This will bind the TCP/IP port number `2346` to the metrics export.

See [the pgagroal configuration settings](https://github.com/agroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal) with particular regard to `metrics`, `metrics_cache_max_age` and `metrics_cache_max_size` for more details.

### Restart pgagroal

In order to apply changes, you need to restart [**pgagroal**](https://github.com/agroal/pgagroal), therefore run the following commands
as the [**pgagroal**](https://github.com/agroal/pgagroal) operating system user:

```
pgagroal-cli shutdown
pgagroal -d
```

If you need to specify other configuration files, for example for remote management (see [the related tutorial](https://github.com/pgagroal/pgagroal/blob/master/doc/tutorial/03_remote_management.md)), add them on the [**pgagroal**](https://github.com/agroal/pgagroal) command line.
If the cofiguration files have standard names, you can omit them.

### Get Prometheus metrics

Once [**pgagroal**](https://github.com/agroal/pgagroal) is running you can access the metrics with a browser at the pooler address, specifying the `metrics` port number and routing to the `/metrics` page. For example, point your web browser at:

```
http://localhost:2346/metrics
```

It is also possible to get an explaination of what is the meaning of each metric by pointing your web browser at:

```
http://localhost:2346/
```

## Prometheus metrics for pgagroal-vault

This tutorial will show you how to do basic  [Prometheus](https://prometheus.io/) setup  for [**pgagroal-vault**](https://github.com/agroal/pgagroal).

**pgagroal-vault** is able to provide a set of metrics about what it is happening within the vault, so that a Prometheus instance can collect them and help you monitor the vault activities.

### Change the pgagroal-vault configuration

In order to enable to export of the metrics, you need to add the `metrics` option in the main `pgagroal_vault.conf` configuration. The value of this setting is the TCP/IP port number that Prometheus will use to grab the exported metrics.

Add a line like the following to `/etc/pgagroal/pgagroal_vault.conf` by editing such file with your editor of choice:

```
metrics = 2501
```

Place it within the `[pgagroal-vault]` section, like

```
[pgagroal-vault]
...
metrics = 2501
```

This will bind the TCP/IP port number `2501` to the metrics export.

See [the pgagroal-vault configuration settings](https://github.com/agroal/pgagroal/blob/master/doc/VAULT.md#pgagroal-vault) with particular regard to `metrics`, `metrics_cache_max_age` and `metrics_cache_max_size` for more details.

### Get Prometheus metrics

Once **pgagroal-vault** is running you can access the metrics with a browser at the pgagroal-vault address, specifying the `metrics` port number and routing to the `/metrics` page. For example, point your web browser at:

```
http://localhost:2501/metrics
```

It is also possible to get an explaination of what is the meaning of each metric by pointing your web browser at:

```
http://localhost:2501/
```
