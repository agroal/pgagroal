===================
pgagroal_vault.conf
===================

------------------------------------------
Main configuration file for pgagroal-vault
------------------------------------------

:Manual section: 5

DESCRIPTION
===========

pgagroal_vault.conf is the main configuration file for pgagroal-vault.

The file is split into different sections specified by the ``[`` and ``]`` characters. The main section is called ``[pgagroal-vault]``.

Other sections (generally called the ``main`` section) specifies the ``pgagroal`` remote management configuration.

All properties are in the format ``key = value``.

The characters ``#`` and ``;`` can be used for comments; must be the first character on the line.
The ``Bool`` data type supports the following values: ``on``, ``1``, ``true``, ``off``, ``0`` and ``false``.

OPTIONS
=======

The options for the pgagroal-vault section are

host
  The bind address for pgagroal-vault. Mandatory

port
  The bind port for pgagroal-vault. Mandatory

metrics
  The metrics port. Default is 0 (disabled)

metrics_cache_max_age
  The number of seconds to keep in cache a Prometheus (metrics) response.
  If set to zero, the caching will be disabled. Can be a string with a suffix, like ``2m`` to indicate 2 minutes.
  Default is 0 (disabled)

metrics_cache_max_size
  The maximum amount of data to keep in cache when serving Prometheus responses. Changes require restart.
  This parameter determines the size of memory allocated for the cache even if ``metrics_cache_max_age`` or
  ``metrics`` are disabled. Its value, however, is taken into account only if ``metrics_cache_max_age`` is set
  to a non-zero value. Supports suffixes: ``B`` (bytes), the default if omitted, ``K`` or ``KB`` (kilobytes),
  ``M`` or ``MB`` (megabytes), ``G`` or ``GB`` (gigabytes).
  Default is 256k

log_type
  The logging type (console, file, syslog). Default is console

log_level
  The logging level, any of the (case insensitive) strings ``FATAL``, ``ERROR``, ``WARN``, ``INFO`` and ``DEBUG``
  (that can be more specific as ``DEBUG1`` thru ``DEBUG5``). Debug level greater than 5 will be set to ``DEBUG5``.
  Not recognized values will make the ``log_level`` be ``INFO``. Default is info

log_path
  The log file location. Default is pgagroal-vault.log. Can be a strftime(3) compatible string

log_rotation_age
  The age that will trigger a log file rotation. If expressed as a positive number, is managed as seconds.
  Supports suffixes: ``S`` (seconds, the default), ``M`` (minutes), ``H`` (hours), ``D`` (days), ``W`` (weeks).
  A value of ``0`` disables. Default is 0 (disabled)

log_rotation_size
  The size of the log file that will trigger a log rotation. Supports suffixes: ``B`` (bytes), the default if omitted,
  ``K`` or ``KB`` (kilobytes), ``M`` or ``MB`` (megabytes), ``G`` or ``GB`` (gigabytes). A value of ``0`` (with or without suffix) disables.
  Default is 0

log_line_prefix
  A strftime(3) compatible string to use as prefix for every log line. Must be quoted if contains spaces.
  Default is ``%Y-%m-%d %H:%M:%S``

log_mode
  Append to or create the log file (append, create). Default is append

log_connections
  Log connects. Default is off

log_disconnections
  Log disconnects. Default is off

authentication_timeout
  The number of seconds the process will wait for valid credentials. Default is 5

hugepage
  Huge page support. Default is try

tls
  Enable Transport Layer Security (TLS). Default is false. Changes require restart in the server section.

tls_cert_file
  Certificate file for TLS. Changes require restart in the server section.

tls_key_file
  Private key file for TLS. Changes require restart in the server section.

tls_ca_file
  Certificate Authority (CA) file for TLS. Changes require restart in the server section.

The options for the main section are

host
  The address of the pgagroal instance running the management server. Mandatory

port
  The management port of pgagroal. Mandatory
  
user
  The admin user of the pgagroal remote management service. Mandatory

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/agroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal(1), pgagroal-cli(1), pgagroal-admin(1), pgagroal-vault(1)
