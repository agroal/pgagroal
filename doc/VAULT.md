# pgagroal-vault configuration

The configuration which is mandatory is loaded from either the path specified by the `-c` flag or `/etc/pgagroal/pgagroal_vault.conf`.

The configuration of `pgagroal-vault` is split into sections using the `[` and `]` characters.

The pgagroal-vault section, called `[pgagroal-vault]`, is where you configure the overall properties of the vault's server.

The other section provide configuration for the management port of pgagroal. For now there can be only one pgagroal management port to connect.
This section don't have any requirements to their naming so you can give them
meaningful names but generally named as `[main]`.

All properties within a section are in the format `key = value`.

The characters `#` and `;` can be used for comments. A line is totally ignored if the
very first non-space character is a comment one, but it is possible to put a comment at the end of a line.
The `Bool` data type supports the following values: `on`, `yes`, `1`, `true`, `off`, `no`, `0` and `false`.

See a more complete [sample](./etc/pgagroal_vault.conf) configuration for running `pgagroal-vault` on `localhost`.

## [pgagroal-vault]

This section is mandatory and the pooler will refuse to start if the configuration file does not specify one and only one. Usually this section is place on top of the configuration file, but its position within the file does not really matter.
The available keys and their accepted values are reported in the table below.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The bind address for pgagroal-vault |
| port | | Int | Yes | The bind port for pgagroal-vault |
| log_type | console | String | No | The logging type (console, file, syslog) |
| log_level | info | String | No | The logging level, any of the (case insensitive) strings `FATAL`, `ERROR`, `WARN`, `INFO` and `DEBUG` (that can be more specific as `DEBUG1` thru `DEBUG5`). Debug level greater than 5 will be set to `DEBUG5`. Not recognized values will make the log_level be `INFO` |
| log_path | pgagroal.log | String | No | The log file location. Can be a strftime(3) compatible string. |
| log_rotation_age | 0 | String | No | The age that will trigger a log file rotation. If expressed as a positive number, is managed as seconds. Supports suffixes: 'S' (seconds, the default), 'M' (minutes), 'H' (hours), 'D' (days), 'W' (weeks). A value of `0` disables. |
| log_rotation_size | 0 | String | No | The size of the log file that will trigger a log rotation. Supports suffixes: 'B' (bytes), the default if omitted, 'K' or 'KB' (kilobytes), 'M' or 'MB' (megabytes), 'G' or 'GB' (gigabytes). A value of `0` (with or without suffix) disables. |
| log_line_prefix | %Y-%m-%d %H:%M:%S | String | No | A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces. |
| log_mode | append | String | No | Append to or create the log file (append, create) |
| log_connections | `off` | Bool | No | Log connects |
| log_disconnections | `off` | Bool | No | Log disconnects |

## [main]

The section with a name different from `pgagroal-vault` will be treated as a main section.

| Property | Default | Unit | Required | Description |
|----------|---------|------|----------|-------------|
| host | | String | Yes | The address of the pgagroal running the management server |
| port | | Int | Yes | The management port of pgagroal |
| user | | String | Yes | The admin user of the pgagroal remote management service | 

Note: For `pgagroal-vault` to function and connect properly to pgagroal, the remote server for management of the [**pgagroal**](https://github.com/agroal/pgagroal) should be enabled i.e. `management` should be greater than 0.

# Enable SSL connection with management port

The SSL handshake has to be initiated between `vault` and `remote` of `pgagroal` to enable secured SSL connection between the both. 

The `vault` serves as the client, while the `remote` functions as the server. The `vault` initiates the SSL/TLS handshake with the `remote`, and concurrently, the `remote` accepts the SSL/TLS request from the `vault`.

## SSL configuration at the server side

Update the `[pgagroal]` section in main configuration file by including the following:-

- Enable management port
- Enable `tls` to `on`
- Add `tls_cert_file`, `tls_key_file` and `tls_ca_file` fields

```
management = 2347
tls = on
tls_cert_file = /path/to/server_cert_file
tls_key_file = /path/to/server_key_file
tls_ca_file = /path/to/CA_root_cert_file
```

## SSL configuration at the client side

For client side authentication, add the required certificates like the certificate of `vault` in `pgagroal.crt`, private key of `vault` in `pgagroal.key` and the root certificate of CA in `root.crt` in the `.pgagroal` directory.

Some permissions requirements:-

- The certificate file `pgagroal.crt` must be owned by either the user running pgagroal or root.
- The key file `pgagroal.key` must be owned by either the user running pgagroal or root. Additionally permissions must be at least `0640` when owned by root or `0600` otherwise.
- The Certificate Authority (CA) file `root.crt` must be owned by either the user running pgagroal or root.
