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

isalive
  Is pgagroal alive

gracefully
  Stop pgagroal gracefully

stop
  Stop pgagroal

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
