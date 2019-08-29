# pgagroal architecture

## Overview

`pgagroal` use a process model (`fork()`), where each process handles one connection to [PostgreSQL](https://www.postgresql.org).
This was done such a potential crash on one connection won't take the entire pool down.

The main process is defined in [main.c](../src/main.c). When a client connects it is processed in its own process, which
is handle in [worker.h](../src/include/worker.h) ([worker.c](../src/libpgagroal/worker.c)).

## Shared memory

A memory segment ([shmem.h](../src/include/shmem.h)) is shared between all processes which contains the `pgagroal`
state containing the configuration of the pool, the list of servers and the state of each connection.

The configuration of `pgagroal` (`struct configuration`), the configuration of the servers (`struct server`) and
the state of each connection (`struct connection`) is initialized in this shared memory segment.
These structs are all defined in [pgagroal.h](../src/include/pgagroal.h).

The shared memory segment is created using the `mmap()` call.

## Atomic operations

The [atomic operation library](https://en.cppreference.com/w/c/atomic) is used to define the state of each of the
connection, and move them around in the connection state diagram. The state diagram has the follow states

| State name | Description |
|------------|-------------|
| `STATE_NOTINIT` | The connection has not been initialized |
| `STATE_INIT` | The connection is being initialized |
| `STATE_FREE` | The connection is free |
| `STATE_IN_USE` | The connection is in use |
| `STATE_GRACEFULLY` | The connection will be killed upon return to the pool |
| `STATE_FLUSH` | The connection is being flushed |
| `STATE_IDLE_CHECK` | The connection is being idle timeout checked |

These state are defined in [pgagroal.h](../src/include/pgagroal.h).

## Management

`pgagroal` has a management interface which serves two purposes.

First, it defines the administrator abilities that can be performed on the pool when it is running. This include
for example flushing the pool. The `pgagroal-cli` program is used for these operations ([cli.c](../src/cli.c)).

Second, the interface is used internally to transfer the connection (socket descriptor) from the child process
to the main `pgagroal` process after a new connection has been created. This is necessary since the socket descriptor
needs to be available to subsequent client and hence processes.

The management interface is defined in [management.h](../src/include/management.h).

## libev usage

[libev](http://software.schmorp.de/pkg/libev.html) is used to handle network interactions, which is "activated"
upon an `EV_READ` event.

One of the goals for `pgagroal` is performance, so `pgagroal` will only look for the
[`Terminate`](https://www.postgresql.org/docs/11/protocol-message-formats.html) message from the client and act on that.
This makes the pipeline very fast, since there is a minimum overhead in the interaction.

The pipeline is defined in [worker.c](../src/libpgagroal/worker.c) in the functions

| Function | Description |
|----------|-------------|
| `client_pgagroal_cb` | Client to `pgagroal` communication |
| `server_pgagroal_cb` | [PostgreSQL](https://www.postgresql.org) to `pgagroal` communication |

