# pgagroal pipelines

pgagroal supports 3 different pipelines

* Performance
* Session
* Transaction

The pipeline is defined in `pgagroal.conf` under the setting of

```
pipeline = auto
```

pgagroal will choose either the performance or the session pipeline
based on the configuration settings by default.

# Performance

The performance pipeline is fastest pipeline as it is a minimal implementation
of the pipeline architecture.

However, it doesn't support Transport Layer Security (TLS), failover support and
the `disconnect_client` setting.

A `DISCARD ALL` query is run after each client session.

Select the performance pipeline by

```
pipeline = performance
```

# Session

The session pipeline supports all features of pgagroal.

A `DISCARD ALL` query is run after each client session.

Select the session pipeline by

```
pipeline = session
```

# Transaction

The transaction pipeline will release the connection back to the pool after each
transaction completes. This feature will support many more clients than there are
database connections.

The transaction pipeline requires that there are users defined such that connections
can be kept in the pool in all security scenarios.

However, there are some session based features of PostgreSQL that can't be supported in this
pipeline.

* `SET` / `RESET`
* `LISTEN` / `NOTIFY`
* `WITH HOLD CURSOR`
* `PREPARE` / `DEALLOCATE`

It is assumed that all clients using the same user name and database pair share the same
startup parameters towards PostgreSQL.

__`SET` / `RESET`__

The `SET` functionality is a session based feature.

__`LISTEN` / `NOTIFY`__

The `LISTEN` functionality is a session based feature.

__`WITH HOLD CURSOR`__

The `WITH HOLD CURSOR` functionality is a session based feature.

__`PREPARE` / `DEALLOCATE`__

While using `PREPARE` and `EXECUTE` can be used the prepared statements are tied to the
connection they were created in which means that clients can't be sure that they created
the prepared statement on the connection unless it is issued within the same transaction
where it is used.

pgagroal can issue a `DEALLOCATE ALL` statement before a connection is returned back to
the pool if the `track_prepared_statements` setting is set to `on`. If `off` then no
statement is issued.

Note, that pgagroal does not issue a `DISCARD ALL` statement when using the transaction
pipeline.

__Performance considerations__

Clients may need to wait for a connection between transactions leading to a higher
latency.

__Important__

Make sure that the `blocking_timeout` settings to set to 0. Otherwise active clients
may timeout during their workload. Likewise it is best to disable idle connection timeout 
and max connection age by setting `idle_timeout` and `max_connection_age` to 0.

It is highly recommended that you prefill all connections for each user.

The transaction pipeline doesn't support the `disconnect_client` or
`allow_unknown_users` settings.

Select the transaction pipeline by

```
pipeline = transaction
```
