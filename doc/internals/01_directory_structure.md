## pgagroal directory structure

This document takes one through the pgagroal directory structure from a high level
it is not meant to be a tutorial for end users, but for developers who are trying
to contribute to pgagroal

- Last Update time: 25/04/2023 (DD/MM/YYYY)

### Source code

Source code is found in the `/src` directory.

Headers are placed in `src/include/*.h` while the corresponding source files are in
`libpgagroal`

### 1. `configuration.h /configuration.c`

Contains configuration parsing code

`configuration.h` exposes the following configuration parsers

- `pgagroal_read_configuration` : Read main configuration
- `pgagroal_read_hba_configuration`: Read HBA configuration
- `pgagroal_read_limit_configuration` : Read limit configuration
- `pgagroal_read_users_configuration`: Read users configuration
- `pgagroal_read_frontend_users_configuration`: Read frontend users configuration
- `pgagroal_read_admins_configuration`: Read admin configuration
- `pgagroal_read_superuser_configuration`: Read superuser from a file.
- `pgagroal_reload_configuration`: Reload configuration
- `pgagroal_write_config_value`: Write configuration values, supporting `server`,`hba`,`limit` and `main` config files.

The configuration keys cannot exceed `MISC_LENGTH` ( which is currently defined as 128),
if such keys exist they are treated as errors.

### 2. `logging.h/logging.c`

Contain logging configurations

Verbosity of logging is configured by setting it in `pgagroal.conf` dynamically
there are six levels, from `trace` to `fatal`.

It also exposes the ability to do log rotation

The following indicate levels a config file generates given a level

- `debug5-debug0` - pgagroal supports multiple debug levels, each increasing verbosity, `debug5`  is the highest
  level , which activates the trace level of logging,

- `info`: Enables info level logging
- `warn`: Enables warn level logging
- `error`: Enables error level logging
- `fatal`: Enables fatal level logging

To configure logging you set the appropriate level in the config file (`pgagroal.conf`)

```toml
[pgagroal]
# set level here, can be 
# debug5,debug0,info,warn,error,fatal
log_level = info
```

There are no quotations in between configurations.

### 3. `management.h/management.c`

This contains code providing boilerplate for reading and writing to sockets
e.g error recovery and connection management.

### 4. `memory.h/memory.c`

These contain routines to manage two variables `message` and `data`.

The stated variables are global and the functions in `memory` help allocate, set and delete
the above memory.

`message`  is a struct that represents a message exchanged between pgagroal (defined in `message.h`)
while `data` contains raw contents of that message.

### 5. `message.h/message.c`

This defines a message struct, there are two types of messages to keep in mind,
the global message defined in `memory.h` and `memory.c` and local messages created by
common C initialization schemes.

The message struct is defined as

```C
struct message
{
   signed char kind;  /**< The kind of the message */
   ssize_t length;    /**< The length of the message */
   size_t max_length; /**< The maximum size of the message */
   void* data;        /**< The message data */
} __attribute__ ((aligned (64)));
```

The local copies all attribute the global message defined in `memory.h`, this means they all share `**data` which
means that there should not be two copies of message existing at the same time.

### 6. `pgagroal.h`

Defines mutliple things, shared memories for many other parts (`pipeline shared memory`, `promethus shared memory`)

and contains multiple definitions for various C's representations of config structs.

It defines `configuration`, which is a struct that holds configuration and state of `pgagroal`

`configuration` is stored in `shmem` ,
all child processes can access this struct since the memory is shared in processes.

i.e code like this

```C
struct configuration* config;

config = (struct configuration*)shmem;
```

exists in a lot of places.

### 7. `pipeline.h`

Contains code necessary for pipelines.

Sets up necessary functions for the three pipeline methods, `session`,`performance` and `transaction`

An extensive pipeline documentation is found [here](../PIPELINES.md)

### 8. `pool.h`

Contains code necessary for creating,managing and destroying connection pools

pgagroal pools are forked from main process to create a child process which then manage
independent connections to the client

Currently , the maximum supported number of connection pools is `8`
`pgagroal` warns on having a greater number than this

Warning looks like

```
WARN  configuration.c:833 pgagroal: max_connections (100) is greater than allowed (8)
```

### 9. `prometheus.h/prometheus.c`

This exposes metrics which can be viewed inside prometheus or in localhost when accessing
`localhost:{metrics_port}/metrics` where `{metrics_port}` is a port you specified for `pgagroal` to expose
prometheus metrics.

For more on the metrics see [Prometheus tutorial](../tutorial/04_prometheus.md)

#### 10. `remote.h/remote.c`

Code for creating remote management instances.

`pgagroal` supports remote administration and management.

The main turorial is on [Remote management](../tutorial/03_remote_management.md)

### 11. `security.h/security.c`

Implements security protocols

`pgagroal` implements secure auth with the following modes for password authentication

- trust (0)
- password (3)
- md5 (5)
- scram256

The security implementation is independent of `postgresql` instance,
`pgagroal` has its own independent implementation ( in hence the dependency on `openssl` to provide some cryptographic
primitives)

`security.c` also deals with TLS connections over postgresql for the 
additional security benefits arising with using https with databases

### 12. `server.h`

- TODO: Update in subsequent visits


### 13. `shmem.h/shmem.c`


Implements shared memory primitives

Since `pgagroal` is a process based connection pooler, pools communicate with each
other and with the parent memory via shared memory interfaces provided by the `posix` api

The main object shared is the `configuration` struct (discussed in `configuration.h`)


### 14. `tracker.h/tracker.c`
 - TODO: Ask

### 15. `utils.h/utils.c`

Small miscellaneous utility functions like `read_{byte,string}` are found here

### 16. `worker.h/worker.c`

Contains worker instances for each I/O code
 
- Todo: Expand on this