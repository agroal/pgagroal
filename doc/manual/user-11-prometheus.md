\newpage

# Prometheus metrics

[**pgagroal**][pgagroal] has the following [Prometheus][prometheus] metrics.

## pgagroal_state

The state of pgagroal

| Attribute | Description |
|-----------|------------------------------------|
|value      | State                              |
|           |   * Running                        |
|           |   * Graceful shutdown              |

## pgagroal_pipeline_mode

The mode of pipeline

| Attribute | Description |
|-----------|------------------------------------|
|value 	    | Mode                               |
|           | * Performance                      |
|           | * Session                          |
|           | * Transaction                      |

## pgagroal_logging_info

The number of INFO statements

## pgagroal_logging_warn

The number of WARN statements

## pgagroal_logging_error

The number of ERROR statements

## pgagroal_logging_fatal

The number of FATAL statements

## pgagroal_server_error

Errors for servers

| Attribute | Description |
|-----------|------------------------------------|
|name 	    | The name of the server             |
|state 	    | The server state                   |
|           | * not_init                         |
|           | * primary                          |
|           | * replica                          |
|           | * failover                         |
|           | * failed                           |

## pgagroal_failed_servers

The number of failed servers.

Only set if failover is enabled

## pgagroal_wait_time

The waiting time of clients

## pgagroal_query_count

The number of queries.

Only session and transaction modes are supported

## pgagroal_connection_query_count

The number of queries per connection.

Only session and transaction modes are supported

| Attribute | Description |
|-----------|------------------------------------|
|id 	    | The connection identifier          |
|user 	    | The user name                      |
|database 	| The database                       |
|application_name |	The application name         |

## pgagroal_tx_count

The number of transactions. Only session and transaction modes are supported

## pgagroal_active_connections

The number of active connections

## pgagroal_total_connections

The number of total connections

## pgagroal_max_connections

The maximum number of connections

## pgagroal_connection

Connection information

| Attribute | Description |
|-----------|------------------------------------|
|id 	    | The connection identifier          |
|user 	    | The user name                      |
|database 	| The database                       |
|application_name |	The application name         |
|state 	    | The connection state               |
|           | * not_init                         |
|           | * init                             |
|           | * free                             |
|           | * in_use                           |
|           | * gracefully                       |
|           | * flush                            |
|           | * idle_check                       |
|           | * max_connection_age               |
|           | * validation                       |
|           | * remove                           |

## pgagroal_limit

Limit information

| Attribute | Description |
|-----------|------------------------------------|
|user 	    | The user name                      |
|database 	| The database                       |
|type 	    | The information type               |
|           | * not_init                         |
|           | * min                              |
|           | * initial                          |
|           | * max                              |
|           | * active                           |

## pgagroal_limit_awaiting

Connections awaiting on hold reported by limit entries

| Attribute | Description |
|-----------|------------------------------------|
|user 	    | The user name                      |
|database 	| The database                       |

## pgagroal_session_time

Histogram of session times

## pgagroal_connection_error

Number of connection errors

## pgagroal_connection_kill

Number of connection kills

## pgagroal_connection_remove

Number of connection removes

## pgagroal_connection_timeout

Number of connection time outs

## pgagroal_connection_return

Number of connection returns

## pgagroal_connection_invalid

Number of connection invalids

## pgagroal_connection_get

Number of connection gets

## pgagroal_connection_idletimeout

Number of connection idle timeouts

## pgagroal_connection_max_connection_age

Number of connection max age timeouts

## pgagroal_connection_flush

Number of connection flushes

## pgagroal_connection_success

Number of connection successes

## pgagroal_connection_awaiting

Number of connection suspended due to blocking_timeout

## pgagroal_auth_user_success

Number of successful user authentications

## pgagroal_auth_user_bad_password

Number of bad passwords during user authentication

## pgagroal_auth_user_error

Number of errors during user authentication

## pgagroal_client_wait

Number of waiting clients

## pgagroal_client_active

Number of active clients

## pgagroal_network_sent

Bytes sent by clients. Only session and transaction modes are supported

## pgagroal_network_received

Bytes received from servers. Only session and transaction modes are supported

## pgagroal_client_sockets

Number of sockets the client used

## pgagroal_self_sockets

Number of sockets used by pgagroal itself

[**pgagroal-vault**][pgagroal-vault] has the following [Prometheus][prometheus] metrics.

## pgagroal_vault_logging_info

The number of INFO statements

## pgagroal_vault_logging_warn

The number of WARN statements

## pgagroal_vault_logging_error

The number of ERROR statements

## pgagroal_vault_logging_fatal

The number of FATAL statements

## pgagroal_vault_client_sockets

Number of sockets the client used

## pgagroal_vault_self_sockets

Number of sockets used by pgagroal-vault itself

# Prometheus Metrics for OS Info 

pgagroal exposes operating system information through three distinct Prometheus metrics, depending on the underlying operating system:

---

| Operating System           |    Metric Name       |       Included Labels                | 
|----------------------------|----------------------|--------------------------------------|
| **Linux**                  | `pgagroal_os_linux`  | `os`, `major`, `minor`, `patch`      |
| **macOS**                  | `pgagroal_os_macos`  | `os`, `major`, `minor`, `patch`      |
|**BSD (FreeBSD, OpenBSD)**  | `pgagroal_os_bsd`    | `os`, `major`, `minor`               |

---


## pgagroal_os_linux

The `pgagroal_os_linux` metric reports the kernel version for **Linux-based** systems. It includes the following labels:

- `os`: The name of the operating system.
- `major`: The major version of the kernel.
- `minor`: The minor version of the kernel.
- `patch`: The patch version of the kernel.

###  Conditions for Exposure:
- The metric is exposed **only if the kernel version retrieval succeeds**.
- If kernel version retrieval fails, **no metric will be emitted**.

### Example Output:
```text
# HELP pgagroal_os_linux Kernel version as major.minor.patch
# TYPE pgagroal_os_linux gauge
pgagroal_os_linux{os="Linux", major="6", minor="13", patch="4"} 1
```

---

## pgagroal_os_macos

The `pgagroal_os_macos` metric reports the kernel version for **macOS-based** systems. It includes the same labels as Linux:

- `os`: The name of the operating system.
- `major`: The major version of the kernel.
- `minor`: The minor version of the kernel.
- `patch`: The patch version of the kernel.

###  Conditions for Exposure:
- The metric is exposed **only if the kernel version retrieval succeeds**.
- If kernel version retrieval fails, **no metric will be emitted**.

### Example Output:
```text
# HELP pgagroal_os_macos Kernel version as major.minor.patch
# TYPE pgagroal_os_macos gauge
pgagroal_os_macos{os="macOS", major="23", minor="3", patch="0"} 1
```

---

## pgagroal_os_bsd

The `pgagroal_os_bsd` metric reports the operating system version for **BSD-based** systems (such as FreeBSD and OpenBSD).  
Since BSD systems do not provide a patch version in the same way as Linux/macOS, the metric includes only:

- `os`: The name of the operating system.
- `major`: The major version of the operating system.
- `minor`: The minor version of the operating system.

Unlike Linux/macOS, which report the kernel version, BSD reports the operating system version.

### Conditions for Exposure:
- The metric is exposed **only if the operating system version retrieval succeeds**.
- The **patch version is not available** on BSD systems, so it is omitted from the metric.
- If operating system version retrieval fails, **no metric will be emitted**.

### Example Output:
```text
# HELP pgagroal_os_bsd operating system version as major.minor
# TYPE pgagroal_os_bsd gauge
pgagroal_os_bsd{os="FreeBSD", major="13", minor="2"} 1
```
---

## Behavior Summary

| Operating System           | Metric Name                       | Availability Conditions                                       |
|----------------------------|-----------------------------------|---------------------------------------------------------------|
| **Linux**                  | `pgagroal_os_linux`               |    Available if kernel version retrieval succeeds               |
| **macOS**                  | `pgagroal_os_macos`               |    Available if kernel version retrieval succeeds               |
| **BSD (FreeBSD, OpenBSD)** | `pgagroal_os_bsd`                 |    Available if operating system version retrieval succeeds (patch not available) |

---


##  When Metrics Are Not Exposed

- If version retrieval **fails** (e.g., unsupported system call, permission issues), **no metric will be emitted**.
- If the system is **unsupported** (e.g., Windows or an unknown OS), **pgagroal will not attempt to expose a metric**.

---

