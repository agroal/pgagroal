============
pgagroal-cli
============

---------------------------------
Command line utility for pgagroal
---------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal-cli [ -c CONFIG_FILE ] [ COMMAND ]

DESCRIPTION
===========

pgagroal-cli is a command line utility for pgagroal.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgagroal.conf file

-h, --host HOST
  Set the host name

-p, --port PORT
  Set the port number

-U, --user USERNAME
  Set the user name

-P, --password PASSWORD
  Set the password

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

flush-idle
  Flush idle connections

flush-gracefully
  Flush all connections gracefully

flush-all
  Flush all connections. USE WITH CAUTION !

is-alive
  Is pgagroal alive

enable
  Enable a database. Optional parameter with the
  database name, if not specified all will be enabled

disable
  Disable a database. Optional parameter with the
  database name, if not specified all will be disabled

gracefully
  Stop pgagroal gracefully

stop
  Stop pgagroal

cancel-shutdown
  Cancel the graceful shutdown

status
  Status of pgagroal

details
  Detailed status of pgagroal

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/agroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal(1), pgagroal-admin(1)
