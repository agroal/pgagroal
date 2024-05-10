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
