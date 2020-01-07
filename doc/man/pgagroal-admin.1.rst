==============
pgagroal-admin
==============

-----------------------------------
Administration utility for pgagroal
-----------------------------------

:Manual section: 1

SYNOPSIS
========

pgagroal-admin [ -u USERS_FILE ] [ COMMAND ]

DESCRIPTION
===========

pgagroal-admin is an administration utility for pgagroal.

OPTIONS
=======

-u, --users USERS_FILE
  Set the path to the pgagroal_users.conf file

-V, --version
  Display version information

-?, --help
  Display help

COMMANDS
========

master-key
  Create or update the master key

add-user
  Add a user

update-user
  Update a user

remove-user
  Remove a user

list-users
  List all users

REPORTING BUGS
==============

pgagroal is maintained on GitHub at https://github.com/agroal/pgagroal

COPYRIGHT
=========

pgagroal is licensed under the 3-clause BSD License.

SEE ALSO
========

pgagroal.conf(5), pgagroal_hba.conf(5), pgagroal_databases.conf(5), pgagroal(1), pgagroal-cli(1)