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

## Pool

The `pgagroal` pool API is defined in [pool.h](../src/include/pool.h) ([pool.c](../src/libpgagroal/pool.c)).

This API defines the functionality of the pool such as getting a connection from the pool, and returning it.
There is no ordering between processes, so a newly created process can obtain a connection before an older process.

The pool operates on the `struct connection` data type defined in [pgagroal.h](../src/include/pgagroal.h).

## Network and messages

All communication is abstracted using the `struct message` data type defined in [pgagroal.h](../src/include/pgagroal.h).

Reading and writing messages are handled in the [message.h](../src/include/message.h) ([message.c](../src/libpgagroal/message.c))
files.

Network operations are defined in [network.h](../src/include/network.h) ([network.c](../src/libpgagroal/network.c)).

## Memory

Each process uses a fixed memory block for its network communication, which is allocated upon startup of the worker.

That way we don't have to allocate memory for each network message, and more importantly free it after end of use.

The memory interface is defined in [memory.h](../src/include/memory.h) ([memory.c](../src/libpgagroal/memory.c)).

## Management

`pgagroal` has a management interface which serves two purposes.

First, it defines the administrator abilities that can be performed on the pool when it is running. This include
for example flushing the pool. The `pgagroal-cli` program is used for these operations ([cli.c](../src/cli.c)).

Second, the interface is used internally to transfer the connection (socket descriptor) from the child process
to the main `pgagroal` process after a new connection has been created. This is necessary since the socket descriptor
needs to be available to subsequent client and hence processes.

The management interface is defined in [management.h](../src/include/management.h). The management interface
uses its own protocol which always consist of a header

| Field      | Type | Description |
|------------|------|-------------|
| `id` | Byte | The identifier of the message type |
| `slot` | Int | The slot that the message is for |

The rest of the message is depending on the message type.

## libev usage

[libev](http://software.schmorp.de/pkg/libev.html) is used to handle network interactions, which is "activated"
upon an `EV_READ` event.

Each process has its own event loop, such that the process only gets notified when data related only to that process
is ready. The main loop handles the system wide "services", such as idle timeout checks, as well.

One of the goals for `pgagroal` is performance, so `pgagroal` will only look for the
[`Terminate`](https://www.postgresql.org/docs/11/protocol-message-formats.html) message from the client and act on that.
Likewise `pgagroal` will only look for `FATAL` errors from the server. This makes the pipeline very fast, since there
is a minimum overhead in the interaction.

The pipeline is defined in [worker.c](../src/libpgagroal/worker.c) in the functions

| Function | Description |
|----------|-------------|
| `client_pgagroal_cb` | Client to `pgagroal` communication |
| `server_pgagroal_cb` | [PostgreSQL](https://www.postgresql.org) to `pgagroal` communication |

## Signals

The main process of `pgagroal` supports the following signals `SIGTERM`, `SIGHUP`, `SIGINT` and `SIGALRM`
as a mechanism for shutting down. The `SIGTRAP` signal will put `pgagroal` into graceful shutdown, meaning that
exisiting connections are allowed to finish their session. The `SIGABRT` is used to request a core dump (`abort()`).

The child processes support `SIGQUIT` as a mechanism to shutdown. This will not shutdown the pool itself.

It should not be needed to use `SIGKILL` for `pgagroal`. Please, consider using `SIGABRT` instead, and share the
core dump and debug logs with the `pgagroal` community.

## Logging

[zf_log](https://github.com/wonder-mice/zf_log) is used for the logging framework.

`zf_log` is licensed under the [MIT](https://opensource.org/licenses/MIT) license.
