=======================
pgagroal_databases.conf
=======================

------------------------------------------------
Limits for databases, users or both for pgagroal
------------------------------------------------

:Manual section: 5

DESCRIPTION
===========

The pgagroal_databases.conf configuration file defines limits for a database or a user or both.

FORMAT
======

DATABASE
  Specifies the database for the rule. Either specific name or all for all databases

USER
  Specifies the user for the rule. Either specific name or all for all users

MAX_CONNECTIONS
  Specifies the maximum number of connections for the entry. all for all connections

INITIAL_SIZE
  Specifies the initial pool size for a database and user pair. Requires a pgagroal_users.conf configuration

EXAMPLE
=======

::
   
  #
  # DATABASE USER  MAX_CONNECTIONS INITIAL_SIZE
  #
  all        all   all


REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/agroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal(1), pgagroal-cli(1), pgagroal-admin(1)
