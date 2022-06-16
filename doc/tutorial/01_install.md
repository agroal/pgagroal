# Install pgagroal

This tutorial will show you how to do a simple installation of pgagroal.

At the end of this tutorial you will have a running connection pool.

## Preface

This tutorial assumes that you have an installation of PostgreSQL 10+ and pgagroal.

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via

```
dnf install -y postgresql10 postgresql10-server pgagroal
```

## Initialize cluster

```
export PATH=/usr/pgsql-10/bin:$PATH
initdb /tmp/pgsql
```

(`postgres` user)

## Remove default access

Remove

```
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
```

from `/tmp/pgsql/pg_hba.conf`

(`postgres` user)

## Add access for a user and a database

Add

```
host    mydb             myuser          127.0.0.1/32            md5
host    mydb             myuser          ::1/128                 md5
```

to `/tmp/pgsql/pg_hba.conf`

Remember to check the value of `password_encryption` in `/tmp/pgsql/postgresql.conf`
to setup the correct authentication type.

(`postgres` user)

## Start PostgreSQL

```
pg_ctl  -D /tmp/pgsql/ start
```

(`postgres` user)

## Add user and a database

```
createuser -P myuser
createdb -E UTF8 -O myuser mydb
```

with `mypass` as the password.

(`postgres` user)

## Verify access

For the user (standard) (using `mypass`)

```
psql -h localhost -p 5432 -U myuser mydb
\q
```

(`postgres` user)

## Add pgagroal user

```
sudo su -
useradd -ms /bin/bash pgagroal
passwd pgagroal
exit
```

(`postgres` user)

## Create pgagroal configuration

Switch to the pgagroal user

```
sudo su -
su - pgagroal
```

Add the master key and create vault

```
pgagroal-admin master-key
pgagroal-admin -f pgagroal_users.conf -U myuser -P mypass add-user
```

You have to choose a password for the master key - remember it !

Create the `pgagroal.conf` configuration

```
cat > pgagroal.conf
[pgagroal]
host = *
port = 2345

log_type = file
log_level = info
log_path = /tmp/pgagroal.log

max_connections = 100
idle_timeout = 600
validation = off
unix_socket_dir = /tmp/

[primary]
host = localhost
port = 5432
```

and press `Ctrl-D`

Next create the `pgagroal_hba.conf` configuration

```
cat > pgagroal_hba.conf
host   mydb   myuser   all   all
```

and press `Ctrl-D`


(`pgagroal` user)

## Start pgagroal

```
pgagroal -c pgagroal.conf -a pgagroal_hba.conf -u pgagroal_users.conf
```

(`pgagroal` user)
