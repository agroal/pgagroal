# pgagroal configuration

The configuration is loaded from either the path specified by the `-c` flag or `/etc/pgagroal/pgagroal.conf`.

The configuration of `pgagroal` is split into sections using the `[` and `]` characters.

The main section, called `[pgagroal]`, is where you configure the overall properties
of the connection pool.

The other sections configure each one server. There can be no more than `64` server sections, therefore
no more than `64` servers configured as backends.
Server sections don't have any requirements to their naming so you can give them
meaningful names like `[primary]` for the primary [PostgreSQL](https://www.postgresql.org)
instance.

In any case, it is not possible to have duplicated sections, that means section names must be unique within
the configuration file.

All properties within a section are in the format `key = value`.

The characters `#` and `;` can be used for comments. A line is totally ignored if the
very first non-space character is a comment one, but it is possible to put a comment at the end of a line.
The `Bool` data type supports the following values: `on`, `1`, `true`, `off`, `0` and `false`.

Each value can be quoted by means of `"` or `'`. Quoted strings must begin and end with the very same quoting character. It is possible to nest quotes.
As an example of configuration snippet including quoting and comments:

```
# This line is ignored since it starts with '#'
# and so is this one
log_line_prefix = "PGAGROAL #%Y-%m-%d-%H:%M:%S" # quoted because it contains spaces
log_type= console#log to stdout
pipeline = 'performance' # no need to quote since it does not contain any spaces

```

See a more complete [sample](./etc/pgagroal.conf) configuration for running `pgagroal` on `localhost`.

## [pgagroal]

This section is mandatory and the pooler will refuse to start if the configuration file does not specify one and only one. Usually this section is place on top of the configuration file, but its position within the file does not really matter.
The available keys and their accepted values are reported in the table below.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgagroal |
| port | | Int | Yes | The bind port for pgagroal |
| unix_socket_dir | | String | Yes | The Unix Domain Socket location |
| metrics | 0 | Int | No | The metrics port (disable = 0) |
| metrics_cache_max_age | 0 | String | No | The number of seconds to keep in cache a Prometheus (metrics) response. If set to zero, the caching will be disabled. Can be a string with a suffix, like `2m` to indicate 2 minutes |
| metrics_cache_max_size | 256k | String | No | The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart. This parameter determines the size of memory allocated for the cache even if `metrics_cache_max_age` or `metrics` are disabled. Its value, however, is taken into account only if `metrics_cache_max_age` is set to a non-zero value. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes).|
| management | 0 | Int | No | The remote management port (disable = 0) |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgagroal.log | String | No | The log file location. Can be a strftime(3) compatible string. |
| log_rotation_age | 0 | String | No | The age that will trigger a log file rotation. If expressed as a positive number, is managed as seconds. Supports suffixes: 'S' (seconds, the default), 'M' (minutes), 'H' (hours), 'D' (days), 'W' (weeks). A value of `0` disables. |
| log_rotation_size | 0 | String | No | The size of the log file that will trigger a log rotation. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes). A value of `0` (with or without suffix) disables. |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces. |
| log_mode | append | String | No | Append to or create the log file (append, create) |
| log_connections | `off` | Bool | No | Log connects |
| log_disconnections | `off` | Bool | No | Log disconnects |
| blocking_timeout | 30 | Int | No | The number of seconds the process will be blocking for a connection (disable = 0) |
| idle_timeout | 0 | Int | No | The number of seconds a connection is been kept alive (disable = 0) |
| validation | `off` | String | No | Should connection validation be performed. Valid options: `off`, `foreground` and `background` |
| background_interval | 300 | Int | No | The interval between background validation scans in seconds |
| max_retries | 5 | Int | No | The maximum number of iterations to obtain a connection |
| max_connections | 100 | Int | No | The maximum number of connections to PostgreSQL (max 10000) |
| allow_unknown_users | `true` | Bool | No | Allow unknown users to connect |
| authentication_timeout | 5 | Int | No | The number of seconds the process will wait for valid credentials |
| pipeline | `auto` | String | No | The pipeline type (`auto`, `performance`, `session`, `transaction`) |
| auth_query | `off` | Bool | No | Enable authentication query |
| failover | `off` | Bool | No | Enable failover support |
| failover_script | | String | No | The failover script to execute |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) |
| tls_cert_file | | String | No | Certificate file for TLS. This file must be owned by either the user running pgagroal or root. |
| tls_key_file | | String | No | Private key file for TLS. This file must be owned by either the user running pgagroal or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise. |
| tls_ca_file | | String | No | Certificate Authority (CA) file for TLS. This file must be owned by either the user running pgagroal or root.  |
| libev | `auto` | String | No | Select the [libev](http://software.schmorp.de/pkg/libev.html) backend to use. Valid options: `auto`, `select`, `poll`, `epoll`, `iouring`, `devpoll` and `port` |
| buffer_size | 65535 | Int | No | The network buffer size (`SO_RCVBUF` and `SO_SNDBUF`) |
| keep_alive | on | Bool | No | Have `SO_KEEPALIVE` on sockets |
| nodelay | on | Bool | No | Have `TCP_NODELAY` on sockets |
| non_blocking | off | Bool | No | Have `O_NONBLOCK` on sockets |
| backlog | `max_connections` / 4 | Int | No | The backlog for `listen()`. Minimum `16` |
| hugepage | `try` | String | No | Huge page support (`off`, `try`, `on`) |
| tracker | off | Bool | No | Track connection lifecycle |
| track_prepared_statements | off | Bool | No | Track prepared statements (transaction pooling) |
| pidfile | | String | No | Path to the PID file. If omitted, automatically set to `unix_socket_dir`/pgagroal.`port`.pid |
| update_process_title | `verbose` | String | No | The behavior for updating the operating system process title, mainly related to connection processes. Allowed settings are: `never` (or `off`), does not update the process title; `strict` to set the process title without overriding the existing initial process title length; `minimal` to set the process title to `username/database`; `verbose` (or `full`) to set the process title to `user@host:port/database`. Please note that `strict` and `minimal` are honored only on those systems that do not provide a native way to set the process title (e.g., Linux). On other systems, there is no difference between `strict` and `minimal` and the assumed behaviour is `minimal` even if `strict` is used. `never` and `verbose` are always honored, on every system. On Linux systems the process title is always trimmed to 255 characters, while on system that provide a natve way to set the process title it can be longer. |

__Danger zone__

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| disconnect_client | 0 | Int | No | Disconnect clients that have been idle for more than the specified seconds. This setting __DOES NOT__ take long running transactions into account  |
| disconnect_client_force | off | Bool | No | Disconnect clients that have been active for more than the specified seconds. This setting __DOES NOT__ take long running transactions into account  |

## Server section

Each section with a name different from `pgagroal` will be treated as an host section.
There can be up to `64` host sections, each with an unique name and different combination of `host` and `port` settings, otherwise the pooler will complain about duplicated server configuration.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the PostgreSQL instance |
| port | | Int | Yes | The port of the PostgreSQL instance |
| primary | | Bool | No | Identify the instance as primary (hint) |
| tls | `off` | Bool | No | Enable Transport Layer Security (TLS) support (Experimental - no pooling) |

Note, that if `host` starts with a `/` it represents a path and `pgagroal` will connect using a Unix Domain Socket.

# pgagroal_hba configuration

The `pgagroal_hba` configuration controls access to `pgagroal` through host-based authentication.

The configuration is loaded from either the path specified by the `-a` flag or `/etc/pgagroal/pgagroal_hba.conf`.

The format of the file follows the similar [PostgreSQL HBA configuration format](https://www.postgresql.org/docs/current/auth-pg-hba-conf.html), and as such looks like

```
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

| Column | Required | Description |
|--------|----------|-------------|
| TYPE   | Yes      | Specifies the access method for clients. `host` and `hostssl` are supported |
| DATABASE | Yes      | Specifies the database for the rule. Either specific name or `all` for all databases |
| USER | Yes      | Specifies the user for the rule. Either specific name or `all` for all users |
| ADDRESS | Yes      | Specifies the network for the rule. `all` for all networks, or IPv4 address with a mask (`0.0.0.0/0`) or IPv6 address with a mask (`::0/0`) |
| METHOD | Yes      | Specifies the authentication mode for the user. `all` for all methods, otherwise `trust`, `reject`, `password`, `md5` or `scram-sha-256` |

Remote management users needs to have their database set to `admin` in order for the entry to be considered.

There could be up to `64` HBA entries in the configuration file.

# pgagroal_databases configuration

The `pgagroal_databases` configuration defines limits for a database or a user or both. The limits are the number
of connections from `pgagroal` to PostgreSQL for each entry.

The file also defines the initial and minimum pool size for a database and user pair. Note, that this feature requires
a user definition file, see below.

The configuration is loaded from either the path specified by the `-l` flag or `/etc/pgagroal/pgagroal_databases.conf`.

```
#
# DATABASE USER    MAX_SIZE INITIAL_SIZE MIN_SIZE
#
mydb       myuser  all
```

| Column | Required | Description |
|--------|----------|-------------|
| DATABASE | Yes | Specifies the database for the rule |
| USER | Yes | Specifies the user for the rule |
| MAX_SIZE | Yes | Specifies the maximum pool size for the entry. `all` for `max_connections` |
| INITIAL_SIZE | No | Specifies the initial pool size for the entry. `all` for `MAX_SIZE` connections. Default is 0 |
| MIN_SIZE | No | Specifies the minimum pool size for the entry. `all` for `MAX_SIZE` connections. Default is 0 |


There can be up to `64` entries in the configuration file.

# pgagroal_users configuration

The `pgagroal_users` configuration defines the users known to the system. This file is created and managed through
the `pgagroal-admin` tool.

The configuration is loaded from either the path specified by the `-u` flag or `/etc/pgagroal/pgagroal_users.conf`.

There can be up to `64` users known to `pgagroal`.

# pgagroal_frontend_users configuration

The `pgagroal_frontend_users` configuration defines the passwords for the users connecting to pgagroal.
This allows the setup to use different passwords for the `pgagroal` to PostgreSQL authentication.
This file is created and managed through the `pgagroal-admin` tool.

All users defined in the frontend authentication must be defined in the user vault (`-u`).

Frontend users (`-F`) requires a user vault (`-u`) to be defined.

The configuration is loaded from either the path specified by the `-F` flag or `/etc/pgagroal/pgagroal_frontend_users.conf`.

# pgagroal_admins configuration

The `pgagroal_admins` configuration defines the administrators known to the system. This file is created and managed through
the `pgagroal-admin` tool.

The configuration is loaded from either the path specified by the `-A` flag or `/etc/pgagroal/pgagroal_admins.conf`.

If pgagroal has both Transport Layer Security (TLS) and `management` enabled then `pgagroal-cli` can
connect with TLS using the files `~/.pgagroal/pgagroal.key` (must be 0600 permission),
`~/.pgagroal/pgagroal.crt` and `~/.pgagroal/root.crt`.

# pgagroal_superuser configuration

The `pgagroal_superuser` configuration defines the superuser known to the system. This file is created and managed through
the `pgagroal-admin` tool. It may only have one user defined.

The configuration is loaded from either the path specified by the `-S` flag or `/etc/pgagroal/pgagroal_superuser.conf`.
