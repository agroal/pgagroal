# pgagroal configuration

The configuration of `pgagroal` is split into sections using the `[` and `]` characters.

The main section, called `[pgagroal]`, is where you configure the overall properties
of the connection pool.

Other sections doesn't have any requirements to their naming so you can give them
meaningful names like `[primary]` for the primary [PostgreSQL](https://www.postgresql.org)
instance.

All properties are in the format `key = value`.

The characters `#` and `;` can be used for comments; must be the first character on the line.
The `Bool` data type supports the following values: `on`, `1`, `true`, `off`, `0` and `false`.

See a [sample](./etc/pgagroal.conf) configuration for running `pgagroal` on `localhost`.

## [pgagroal]

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgagroal |
| port | | Int | Yes | The bind port for pgagroal |
| log_type | | String | Yes | The logging type (console, file, syslog) |
| log_level | | String | Yes | The logging level (fatal, error, warn, info, debug1, ..., debug5) |
| log_path | | String | Yes | The log file location |
| unix_socket_dir | | String | Yes | The Unix Domain Socket location |
| idle_timeout | 0 | Int | No | The number of seconds between idle timeout notifications (disable = 0) |
| max_connections | 0 | Int | No | The maximum number of connections (0 = up to 512) |
| buffer_size | 65535 | Int | No | The network buffer size (`SO_RCVBUF` and `SO_SNDBUF`) |
| keep_alive | on | Bool | No | Have `SO_KEEPALIVE` on sockets |
| nodelay | on | Bool | No | Have `TCP_NODELAY` on sockets |
| non_blocking | on | Bool | No | Have `O_NONBLOCK` on sockets |

## Server section

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the PostgreSQL instace |
| port | | Int | Yes | The port of the PostgreSQL instace |
| primary | | Bool | No | Identify the instance as primary (hint) |
