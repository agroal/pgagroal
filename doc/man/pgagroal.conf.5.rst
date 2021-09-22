=============
pgagroal.conf
=============

------------------------------------
Main configuration file for pgagroal
------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgagroal.conf is the main configuration file for pgagroal.

The file is split into different sections specified by the ``[`` and ``]`` characters. The main section is called ``[pgagroal]``.

Other sections specifies the PostgreSQL server configuration.

All properties are in the format ``key = value``.

The characters ``#`` and ``;`` can be used for comments; must be the first character on the line.
The ``Bool`` data type supports the following values: ``on``, ``1``, ``true``, ``off``, ``0`` and ``false``.

OPTIONS
=======

The options for the main section are

host
  The bind address for pgagroal. Mandatory

port
  The bind port for pgagroal. Mandatory

unix_socket_dir
  The Unix Domain Socket location. Mandatory

metrics
  The metrics port. Default is 0 (disabled)

management
  The remote management port. Default is 0 (disabled)

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level (fatal, error, warn, info, debug1, ..., debug5). Default is info

log_path
  The log file location. Default is pgagroal.log

log_connections
  Log connects. Default is off

log_disconnections
  Log disconnects. Default is off

blocking_timeout
  The number of seconds the process will be blocking for a connection (disable = 0). Default is 30

idle_timeout
  The number of seconds a connection is been kept alive (disable = 0). Default is 0

validation
  Should connection validation be performed. Valid options: off, foreground and background. Default is off

background_interval
  The interval between background validation scans in seconds. Default is 300

max_retries
  The maximum number of iterations to obtain a connection. Default is 5

max_connections
  The maximum number of connections (max 1000). Default is 1000

allow_unknown_users
  Allow unknown users to connect. Default is true

authentication_timeout
  The number of seconds the process will wait for valid credentials. Default is 5

pipeline
  The pipeline type. Valid options are auto, performance, session and transaction. Default is auto

auth_query
  Enable authentication query. Default is false

failover
  Enable failover support. Default is false

failover_script
  The failover script

tls
  Enable Transport Layer Security (TLS). Default is false

tls_cert_file
  Certificate file for TLS

tls_key_file
  Private key file for TLS

tls_ca_file
  Certificate Authority (CA) file for TLS

libev
  The libev backend to use. Valid options: auto, select, poll, epoll, iouring, devpoll and port. Default is auto

buffer_size
  The network buffer size (SO_RCVBUF and SO_SNDBUF). Default is 65535

keep_alive
  Have SO_KEEPALIVE on sockets. Default is on

nodelay
  Have TCP_NODELAY on sockets. Default is on

non_blocking
  Have O_NONBLOCK on sockets. Default is off

backlog
  The backlog for listen(). Minimum 16. Default is max_connections / 4

hugepage
  Huge page support. Default is try

tracker
  Track connection lifecycle. Default is off

track_prepared_statements
  Track prepared statements (transaction pooling). Default is off

pidfile
  Path to the PID file

Danger zone

disconnect_client
  Disconnect clients that have been idle for more than the specified seconds. This setting DOES NOT take long running transactions into account. Default is 0

disconnect_client_force
  Disconnect clients that have been active for more than the specified seconds. This setting DOES NOT take long running transactions into account. Default is off

The options for the PostgreSQL section are

host
  The address of the PostgreSQL instance. Mandatory

port
  The port of the PostgreSQL instance. Mandatory
  
primary
  Identify the instance as the primary instance (hint)

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/agroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal(1), pgagroal-cli(1), pgagroal-admin(1)
