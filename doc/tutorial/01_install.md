## Install pgagroal

This tutorial will show you how to do a simple installation of [**pgagroal**](https://github.com/agroal/pgagroal),
in order to get a running connection pool.

## Preface

This tutorial assumes that you already have an installation of [PostgreSQL](https://www.postgresql.org) 13 (or higher).

For RPM based distributions such as Fedora and RHEL you can add the
[PostgreSQL YUM repository](https://yum.postgresql.org/) and do the install via the distribution package manager `dnf`:

```
dnf install -y pgagroal
```

If you don't have [PostgreSQL](https://www.postgresql.org) already installed, you can install both [PostgreSQL](https://www.postgresql.org) and [**pgagroal**](https://github.com/agroal/pgagroal)
in a single pass:

```
dnf install -y postgresql14 postgresql14-server pgagroal
```

Assuming you want to install version 14 of [PostgreSQL](https://www.postgresql.org).

### PostgreSQL setup

In the case you don't have yet a [PostgreSQL](https://www.postgresql.org) running instance, you need to initialize the cluster the connection pooler will connect to. The followings are simple and quick steps to get a cluster up and running as soon as possible.

It is assumed that you run an RPM based distribution, like Fedora or RHEL. Some commands could be in different paths depending on the operating system distribution you are using.

**Initialize cluster**

You need to define a `PGDATA` data directory where [PostgreSQL](https://www.postgresql.org) will store the data in.
In the following, it is assumed that the [PostgreSQL](https://www.postgresql.org) directory is `/postgres/14/data`, then
you can do the following commands in a shell as the operating system user `postgres`:

```
export PATH=/usr/pgsql-14/bin:$PATH
mkdir -p /postgres/14/data
export PGDATA=/postgres/14/data
initdb -k $PGDATA
```

(`postgres` user)

**Remove default accesses**

By default, [PostgreSQL](https://www.postgresql.org) allows trusted accesses from the local machine to any database within the cluster.
It is better to harden your cluster, thus providing accesses only to who and when it is needed.
In order to do this, with your text editor of choice, edit the file `$PGDATA/pg_hba.conf` and remote the following lines:


```
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
```

from `/postgres/14/data/pg_hab.conf`

(`postgres` user)

**Add access for a user and a database**

Assume you will have a database named `mydb` and a user named `myuser` that will be granted access to such database. In order to do so, edit again the `$PGDATA/pg_hba.conf` file and add a couple of lines like the followings:

```
host    mydb             myuser          127.0.0.1/32            scram-sha-256
host    mydb             myuser          ::1/128                 scram-sha-256
```

The first line  grants access to the user `myuser` against the database `mydb` on IPv4 `localhost`; the second line  does the same but on IPv6 `localhost` connection.

Please check the value of the setting `password_encryption` in the configuration file `$PGDATA/postgresql.conf` in order to ensure it matches `scram-sha-256` as the last column in the previous two lines.

**Start PostgreSQL**

It is now time to run the [PostgreSQL](https://www.postgresql.org) instance, so as the `postgres` operating system user, run:

```
pg_ctl -D $PGDATA -l /tmp/logfile start
```

**Create the database and the user**

It is now time to create the database and the user of the previous step. As operating system user `postgres`, execute:

```
psql -c "CREATE ROLE myuser WITH LOGIN PASSWORD 'mypassword';"
psql -c "CREATE DATABASE mydb WITH OWNER myuser;"
```

It is strongly suggested to choose a strong password to protect the database access !

**Verify access**

You can check the connectivity of the database user executing, from a shell, as any operating system user, the following command:

```
psql -h localhost -p 5432 -U myuser -c 'SELECT current_timestamp:'  mydb
```

Type the `mypassword` password when asked, and if you get back the current date and time, everything is working fine!

### pgagroal setup

In order to run [**pgagroal**](https://github.com/agroal/pgagroal), you need at list to configure the main `pgagroal.conf` configuration file, that will tell the pooler how to work, and then `pgagroal_hba.conf` that will instrument the pooler about which users are allowed to connect thru the pooler.

[**pgagroal**](https://github.com/agroal/pgagroal) as a daemon cannot be run by `root` operating system user, it is a good idea to create an unprivileged operating system user to run the pooler.

**Add a user to run pgagroal**

As a privileged operating system user (either `root` or via `sudo` or `doas`), run the followings:

```
useradd -ms /bin/bash pgagroal
passwd pgagroal
```

The above will create an operating system [**pgagroal**](https://github.com/agroal/pgagroal) that is the one that is going to run the pooler.


**Create basic configuration**

As the [**pgagroal**](https://github.com/agroal/pgagroal) operating system user, add a master key to protect the [**pgagroal**](https://github.com/agroal/pgagroal) vault and then add the `myuser` to the pooler:

```
pgagroal-admin master-key
PGAGROAL_PASSWORD=password pgagroal-admin -f /etc/pgagroal/pgagroal_users.conf -U myuser user add
```

**You have to choose a password for the master key - remember it !**

It is now time to create the main `/etc/pgagroal/pgagroal.conf` configuration file with your editor of choice or using `cat` from the command line, create the following content:

```
cd /etc/pgagroal
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

and press `Ctrl-d` (if running `cat`) to save the file.

Similarly, create the `/etc/pgagroal/pgagroal_hba.conf` file;

```
cd /etc/pgagroal
cat > pgagroal_hba.conf
host   mydb   myuser   all   all
```

and press `Ctrl-d` (if using `cat`) to save the file. The above line tells `pgagral` to allow the user `myuser` to *try*
to connect to `mydb` using a TCP-IP connection.

See [the documentation about `pgagroal_hba.conf` for more details](https://github.com/agroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal_hba-configuration).

**Start pgagroal**

It is now time to start [**pgagroal**](https://github.com/agroal/pgagroal), so as the [**pgagroal**](https://github.com/agroal/pgagroal) operating system user run:

```
pgagroal -d
```

If the system is running, you will see some output on the log file `/tmp/pgagroal.log`.

Since the default configuration files are usually searched into the `/etc/pgagroal/` directory, and have well defined names, you can omit the files from the command line if you named them `pgagroal.conf`, `pgagroal_hba.conf` and `pgagroal_users.conf`.

You will not need to specify any command line flag for files that have the standard name like:

* `/etc/pgagroal/pgagroal.conf` (main configuration file)
* `/etc/pgagroal/pgagroal_hba.conf` (host based access configuration file)
* `/etc/pgagroal/pgagroal_databases.conf` (limits file)
* `/etc/pgagroal/pgagroal_admins.conf` (remote management file)
* `/etc/pgagroal/pgagroal_frontend_users.conf` (split security user remapping)

**In the case you named the configuration files differently or in a different folder, you need to specify them on the command line!**

### Shell completion

There is a minimal shell completion support for `pgagroal-cli` and `pgagroal-admin`. If you are running such commands from a Bash or Zsh, you can take some advantage of command completion.

**Installing command completions in Bash**

There is a completion script into `contrib/shell_comp/pgagroal_comp.bash` that can be used
to help you complete the command line while you are typing.

It is required to source the script into your current shell, for instance
by doing:

``` shell
source contrib/shell_comp/pgagroal_comp.bash
```

At this point, the completions should be active, so you can type the name of one the commands between `pgagroal-cli` and `pgagroal-admin` and hit `<TAB>` to help the command line completion.

**Installing the command completions on Zsh**

In order to enable completion into `zsh` you first need to have `compinit` loaded;
ensure your `.zshrc` file contains the following lines:

``` shell
autoload -U compinit
compinit
```

and add the sourcing of the `contrib/shell_comp/pgagroal_comp.zsh` file into your `~/.zshrc`
also associating the `_pgagroal_cli` and `_pgagroal_admin` functions
to completion by means of `compdef`:

``` shell
source contrib/shell_comp/pgagroal_comp.zsh
compdef _pgagroal_cli    pgagroal-cli
compdef _pgagroal_admin  pgagroal-admin
```

If you want completions only for one command, e.g., `pgagroal-admin`, remove the `compdef` line that references the command you don't want to have automatic completion.
At this point, digit the name of a `pgagroal-cli` or `pgagroal-admin` command and hit `<TAB>` to trigger the completion system.
