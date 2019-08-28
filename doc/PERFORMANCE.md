# pgagroal performance

Performance is an important goal for `pgagroal` and effort have been made
to make `pgagroal` scale and use a limited number of resources.

This report describe `pgagroal` in relationship to 3 other [PostgreSQL](https://www.postgresql.org)
connection pool implementations.

The [pgbench](https://www.postgresql.org/docs/11/pgbench.html) program was used in the runs. All pool
configurations were made with performance in mind.

All diagrams are using the same identifier for the connection pool in question, so `a` is `a` in all
diagrams and so on.

## Simple

This run uses

```
pgbench -M simple
```

![pgbench simple](https://github.com/agroal/pgagroal/raw/master/doc/images/perf-simple.png "pgbench simple")

## Extended

This run uses

```
pgbench -M extended
```

![pgbench extended](https://github.com/agroal/pgagroal/raw/master/doc/images/perf-extended.png "pgbench extended")

## Prepared

This run uses

```
pgbench -M prepared
```

![pgbench prepared](https://github.com/agroal/pgagroal/raw/master/doc/images/perf-prepared.png "pgbench prepared")

## ReadOnly

This run uses

```
pgbench -S -M prepared
```

![pgbench readonly](https://github.com/agroal/pgagroal/raw/master/doc/images/perf-readonly.png "pgbench readonly")

## Closing

**Please**, run your own benchmarks to see how `pgagroal` compare to your existing connection pool
deployment.
