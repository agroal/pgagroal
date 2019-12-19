========
pgagroal
========

-----------------------------------------------
High-performance connection pool for PostgreSQL
-----------------------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal [ -c CONFIG_FILE ] [ -a HBA_CONFIG_FILE ] [ -l LIMIT_CONFIG_FILE ] [ -d ]

DESCRIPTION
===========

pgagroal is a high-performance protocol-native connection pool for PostgreSQL.

OPTIONS
=======

-c, --config CONFIG_FILE
  Set the path to the pgagroal.conf file

-a, --hba HBA_CONFIG_FILE
  Set the path to the pgagroal_hba.conf file

-l, --limit LIMIT_CONFIG_FILE
  Set the path to the pgagroal_databases.conf file

-u, --users USERS_FILE
  Set the path to the pgagroal_users.conf file

-d, --daemon
  Run as a daemon

-V, --version
  Display version information

-?, --help
  Display help

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/agroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal-cli(1), pgagroal-admin(1)
