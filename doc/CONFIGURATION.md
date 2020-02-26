# pgagroal configuration

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgagroal/pgagroal.conf`.

The configuration of `pgagroal` is split into sections using the `[` and `]` characters.

The main section, called `[pgagroal]`, is where you configure the overall properties
of the connection pool.

Other sections doesn't have any requirements to their naming so you can give them
meaningful names like `[primary]` for the primary [PostgreSQL](https://www.postgresql.org)
instance.

All properties are in the format `key = value`.

The characters `#` and `;` can be used for comments; must be the first character on the line.
The `Bool` data type supports the following values: `on`, `1`, `true`, `off`, `0` and `false`.

See a [sample](./etc/pgagroal/pgagroal.conf) configuration for running `pgagroal` on `localhost`.

## [pgagroal]

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgagroal |
| port | | Int | Yes | The bind port for pgagroal |
| unix_socket_dir | | String | Yes | The Unix Domain Socket location |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level (fatal, error, warn, info, debug1, ..., debug5) |
| log_path | pgagroal.log | String | No | The log file location |
| log_connections | `off` | Bool | No | Log connects |
| log_disconnections | `off` | Bool | No | Log disconnects |
| blocking_timeout | 30 | Int | No | The number of seconds the process will be blocking for a connection (disable = 0) |
| idle_timeout | 0 | Int | No | The number of seconds a connection is been kept alive (disable = 0) |
| validation | `off` | String | No | Should connection validation be performed. Valid options: `off`, `foreground` and `background` |
| background_interval | 300 | Int | No | The interval between background validation scans in seconds |
| max_retries | 5 | Int | No | The maximum number of iterations to obtain a connection |
| max_connections | 1000 | Int | No | The maximum number of connections (max 1000) |
| allow_unknown_users | `true` | Bool | No | Allow unknown users to connect |
| libev | `auto` | String | No | Select the [libev](http://software.schmorp.de/pkg/libev.html) backend to use. Valid options: `auto`, `select`, `poll`, `epoll`, `linuxaio`, `iouring`, `devpoll` and `port` |
| buffer_size | 65535 | Int | No | The network buffer size (`SO_RCVBUF` and `SO_SNDBUF`) |
| keep_alive | on | Bool | No | Have `SO_KEEPALIVE` on sockets |
| nodelay | on | Bool | No | Have `TCP_NODELAY` on sockets |
| non_blocking | on | Bool | No | Have `O_NONBLOCK` on sockets |
| backlog | `max_connections` / 4 | Int | No | The backlog for `listen()`. Minimum `16` |

## Server section

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| primary | | Bool | No | Identify the instance as primary (hint) |

# pgagroal_hba configuration

The `pgagroal_hba` configuration controls access to `pgagroal` through host-based authentication.

The configuration is loaded from either the path specified by the `-a` flag or `/etc/pgagroal/pgagroal_hba.conf`.

The format follows [PostgreSQL](https://www.postgresql.org), and as such looks like

```
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

| Column | Required | Description |
|--------|----------|-------------|
| TYPE   | Yes      | Specifies the access method for clients. Only `host` supported |
| DATABASE | Yes      | Specifies the database for the rule. Either specific name or `all` for all databases |
| USER | Yes      | Specifies the user for the rule. Either specific name or `all` for all users |
| ADDRESS | Yes      | Specifies the network for the rule. `all` for all networks, or IPv4 address with a mask (`0.0.0.0/0`) or IPv6 address with a mask (`::0/0`) |
| METHOD | Yes      | Specifies the authentication mode for the user. `all` for all methods, otherwise `trust`, `reject`, `password`, `md5` or `scram-sha-256` |

# pgagroal_databases configuration

The `pgagroal_databases` configuration defines limits for a database or a user or both.

It also defines the initial pool size for a database and user pair. Note, that this feature requires
a user definition file, see below.

The configuration is loaded from either the path specified by the `-l` flag or `/etc/pgagroal/pgagroal_databases.conf`.

```
#
# DATABASE USER  MAX_CONNECTIONS INITIAL_SIZE
#
all        all   all
```

| Column | Required | Description |
|--------|----------|-------------|
| DATABASE | Yes | Specifies the database for the rule. Either specific name or `all` for all databases |
| USER | Yes | Specifies the user for the rule. Either specific name or `all` for all users |
| MAX_CONNECTIONS | Yes | Specifies the maximum number of connections for the entry. `all` for all connections |
| INITIAL_SIZE | No | Specifies the initial pool size for a database and user pair, e.g. `all` isn't supported |

# pgagroal_users configuration

The `pgagroal_users` configuration defines the users known to the system. This file is created and managed through
the `pgagroal-admin` tool.

The configuration is loaded from either the path specified by the `-u` flag or `/etc/pgagroal/pgagroal_users.conf`.
