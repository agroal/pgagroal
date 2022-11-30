# pgagroal-cli user guide

```
pgagroal-cli [ -c CONFIG_FILE ] [ COMMAND ]

-c, --config CONFIG_FILE Set the path to the pgagroal.conf file
-h, --host HOST          Set the host name
-p, --port PORT          Set the port number
-U, --user USERNAME      Set the user name
-P, --password PASSWORD  Set the password
-L, --logfile FILE       Set the log file
-v, --verbose            Output text string of result
-V, --version            Display version information
-?, --help               Display help
```

Commands are described in the following sections.
Several commands work against an optional specified database.
It is possible to specify *all database* at once by means of the special string `*` (take care of shell expansion!).
If no database name is specified, the command is automatically run against all databases (i.e., as if `*` has been specified).

## flush-idle
Flush idle connections.
Without any argument, or with `*` as only argument,
works against all configured databases.

Command

```
pgagroal-cli flush-idle [*|<database>]
```

Example

```
pgagroal-cli flush-idle
```

## flush-gracefully
Flush all connections gracefully.
Without any argument, or with `*` as only argument,
works against all configured databases.

Command

```
pgagroal-cli flush-gracefully [*|<database>]
```

Example

```
pgagroal-cli flush-gracefully
```

## flush-all
Flush all connections. **USE WITH CAUTION !**
Without any argument, or with `*` as only argument,
works against all configured databases.

Command

```
pgagroal-cli flush-all [*|<database>]
```

Example

```
pgagroal-cli flush-all mydb
```

## is-alive
Is pgagroal alive

Command

```
pgagroal-cli is-alive
```

Example

```
pgagroal-cli is-alive
```

## enable
Enables the specified database.
Without any argument, or with `*` as only argument,
works against all configured databases.

Command

```
pgagroal-cli enable [<database>|*]
```

Example

```
pgagroal-cli enable
```

## disable
Disables a database specified by its name.
Without any argument, or with `*` as only argument,
works against all configured databases.


Command

```
pgagroal-cli disable [<database>|*]
```

Example

```
pgagroal-cli disable
```

## gracefully
Stop pgagroal gracefully

Command

```
pgagroal-cli gracefully
```

Example

```
pgagroal-cli gracefully
```

## stop
Stop pgagroal

Command

```
pgagroal-cli stop
```

Example

```
pgagroal-cli stop
```

## cancel-shutdown
Cancel the graceful shutdown

Command

```
pgagroal-cli cancel-shutdown
```

Example

```
pgagroal-cli cancel-shutdown
```

## status
Status of pgagroal

Command

```
pgagroal-cli status
```

Example

```
pgagroal-cli status
```

## details
Detailed status of pgagroal

Command

```
pgagroal-cli details
```

Example

```
pgagroal-cli details
```

## switch-to
Switch to another primary

Command

```
pgagroal-cli switch-to <server>
```

Example

```
pgagroal-cli switch-to replica
```

## reload
Reload the configuration

Command

```
pgagroal-cli reload
```

Example

```
pgagroal-cli reload
```

## reset
Reset the Prometheus statistics
Command

```
pgagroal-cli reset
```

Example

```
pgagroal-cli reset
```

## reset-server
Reset the state of a server

Command

```
pgagroal-cli reset-server <server>
```

Example

```
pgagroal-cli reset-server primary
```

## config-get
Given a configuration setting name, provides the current value for such setting.

The configuration setting name must be the same as the one used in the configuration files.
It is possible to specify the setting name with words separated by dots, so that it can assume
the form `section.context.key` where:
- `section` can be either
   - `pgagroal` (optional) the search will be performed into the main configuration settings, that is
      those under the `[pgagroal]` settings in the `pgagroal.conf` file;
   - `limit` the search will match against the dataabse/limit configuration, i.e., the file `pgagroal_databases.conf`;
   - `hba` the search will match against the Host Based Access configuration, i.e., the `pgagroal_hbs.conf`;
   - `server` the search will match against a single server defined in the main `pgagroal.conf` file.
- `context` is the match criteria to find the information into a specific context, depending on the value of the `section`:
   - if the `section` is set to `limit`, than the `context` is the database name to match into the `pgagroal_databases.conf`.
     Please note that the same user could be listed more than once, in such case *only the first match* is reported back;
   - if the `section` is set to `hba`, than the `context` is the username to match into the `pgagroal_hba.conf`.
     Please note that the same user could be listed more than once, in such case *only the first match* is reported back;
   - if the `section` is set to `server`, than the `context` is the name of the server in the `pgagroal.conf` main file;
   - if the `section` is set to `pgagroal`, the `context` must be empty;
- `key` is the configuration key to search for.


Examples
```
pgagroal-cli config-get pipeline
performance

pgagroal-cli config-get limit.pgbench.max_size
2

pgagroal-cli config-get server.venkman.primary
off

```

In the above examples, `pipeline` is equivalent to `pgagroal.pipeline` and looks for a global configuration setting named `pipeline`.
The `limit.pgbench.max_size` looks for the `max_size` set into the *limit* file (`pgagroal_databases.conf`) for the database `pgbench`.
The `server.venkman.primary` searches for the configuration parameter `primary` into the *server* section named `venkman` in the main configuration file `pgagraol.conf`.

If the `--verbose` option is specified, a descriptive string of the configuration parameter is printed as *name = value*:

```
pgagroal-cli config-get max_connections --verbose
max_connections = 4
Success (0)
```

If the parameter name specified is not found or invalid, the program `pgagroal-cli` exit normally without printing any value.


## config-set
Allows the setting of a configuration parameter at run-time, if possible.

Examples
```
pgagroal-cli config-set log_level debug
pgagroal-cli config-set server.venkman.port 6432
pgagroal config-set limit.pgbench.max_size 2
```

The syntax for setting parameters is the same as for the command `config-get`, therefore parameters are organized into namespaces:
- `main` (optional) is the main pgagroal configuration namespace, for example `main.log_level` or simply `log_level`;
- `server` is the namespace referred to a specific server. It has to be followed by the name of the server and the name of the parameter to change, in a dotted notation, like `server.venkman.port`;
- `limit` is the namespace referred to a specific limit entry, followed by the name of the username used in the limit entry.

When executed, the `config-set` command returns the run-time setting of the specified parameter: if such parameter is equal to the value supplied, the change has been applied, otherwise it means that the old setting has been kept.
The `--verbose` flag can be used to understand if the change has been applied:

```
$ pgagroal-cli config-set log_level debug
debug

$ pgagroal-cli config-set log_level debug --verbose
log_level = debug
pgagroal-cli: Success (0)
```

When a setting modification cannot be applied, the system returns the "old" setting value and, if `--verbose` is specified, the error indication:

```
$ pgagroal-cli config-set max_connections 100
40

$ pgagroal-cli config-set max_connections 100 --verbose
max_connections = 40
pgagroal-cli: Error (2)
```

When a `config-set` cannot be applied, the system will report in the logs an indication about the problem. With regard to the previous example, the system reports in the logs something like the following (depending on your `log_level`):

```
DEBUG Trying to change main configuration setting <max_connections> to <100>
INFO  Restart required for max_connections - Existing 40 New 100
WARN  1 settings cannot be applied
DEBUG pgagroal_management_write_config_set: unable to apply changes to <max_connections> -> <100>
```


## Shell completions

There is a minimal shell completion support for `pgagroal-cli`.
Please refer to the [Install pgagroal](https://github.com/agroal/pgagroal/blob/master/doc/tutorial/01_install.md) tutorial for detailed information about how to enable and use shell completions.
