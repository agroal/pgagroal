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
## flush-idle
Flush idle connections

Command

```
pgagroal-cli flush-idle [*|<database>]
```

Example

```
pgagroal-cli flush-idle
```

## flush-gracefully
Flush all connections gracefully

Command

```
pgagroal-cli flush-gracefully [*|<database>]
```

Example

```
pgagroal-cli flush-gracefully
```

## flush-all
Flush all connections. USE WITH CAUTION !

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
Enable a database

Command

```
pgagroal-cli enable [<database>|*]
```

Example

```
pgagroal-cli enable
```

## disable
Disable a database

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
