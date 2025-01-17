# Developer guide

For Fedora 40

## Install PostgreSql

``` sh
dnf install postgresql-server
```

, this will install PostgreSQL 15.

## Install pgagroal

### Pre-install

#### Basic dependencies

``` sh
dnf install git gcc cmake make libev libev-devel openssl openssl-devel systemd systemd-devel python3-docutils libatomic zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel bzip2 bzip2-devel
```

#### Generate user and developer guide

This process is optional. If you choose not to generate the PDF and HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

1. Download dependencies

    ``` sh
    dnf install pandoc texlive-scheme-basic
    ```

2. Download Eisvogel

    Use the command `pandoc --version` to locate the user data directory. On Fedora systems, this directory is typically located at `$HOME/.local/share/pandoc`.

    Download the `Eisvogel` template for `pandoc`, please visit the [pandoc-latex-template](https://github.com/Wandmalfarbe/pandoc-latex-template) repository. For a standard installation, you can follow the steps outlined below.

    ```sh
    wget https://github.com/Wandmalfarbe/pandoc-latex-template/releases/download/2.4.2/Eisvogel-2.4.2.tar.gz
    tar -xzf Eisvogel-2.4.2.tar.gz
    mkdir -p $HOME/.local/share/pandoc/templates
    mv eisvogel.latex $HOME/.local/share/pandoc/templates/
    ```

3. Add package for LaTeX

    Download the additional packages required for generating PDF and HTML files.

    ```sh
    dnf install 'tex(footnote.sty)' 'tex(footnotebackref.sty)' 'tex(pagecolor.sty)' 'tex(hardwrap.sty)' 'tex(mdframed.sty)' 'tex(sourcesanspro.sty)' 'tex(ly1enc.def)' 'tex(sourcecodepro.sty)' 'tex(titling.sty)' 'tex(csquotes.sty)' 'tex(zref-abspage.sty)' 'tex(needspace.sty)'
    ```

#### Generate API guide

This process is optional. If you choose not to generate the API HTML files, you can opt out of downloading these dependencies, and the process will automatically skip the generation.

Download dependencies

    ``` sh
    dnf install graphviz doxygen
    ```

### Build

``` sh
cd /usr/local
git clone https://github.com/agroal/pgagroal.git
cd pgagroal
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
make install
```

This will install [**pgagroal**](https://github.com/agroal/pgagroal) in the `/usr/local` hierarchy with the debug profile.

### Check version

You can navigate to `build/src` and execute `./pgagroal -?` to make the call. Alternatively, you can install it into `/usr/local/` and call it directly using:

``` sh
pgagroal -?
```

If you see an error saying `error while loading shared libraries: libpgagroal.so.1: cannot open shared object` running the above command. you may need to locate where your `libpgagroal.so.1` is. It could be in `/usr/local/lib` or `/usr/local/lib64` depending on your environment. Add the corresponding directory into `/etc/ld.so.conf`.

To enable these directories, you would typically add the following lines in your `/etc/ld.so.conf` file:

``` sh
/usr/local/lib
/usr/local/lib64
```

Remember to run `ldconfig` to make the change effective.

## Setup pgagroal

Let's give it a try. The basic idea here is that we will use two users: one is `postgres`, which will run PostgreSQL, and one is [**pgagroal**](https://github.com/agroal/pgagroal), which will run [**pgagroal**](https://github.com/agroal/pgagroal) to do backup of PostgreSQL.

In many installations, there is already an operating system user named `postgres` that is used to run the PostgreSQL server. You can use the command

``` sh
getent passwd | grep postgres
```

to check if your OS has a user named postgres. If not use

``` sh
useradd -ms /bin/bash postgres
passwd postgres
```

If the postgres user already exists, don't forget to set its password for convenience.

### 1. postgres

Open a new window, switch to the `postgres` user. This section will always operate within this user space.

``` sh
sudo su -
su - postgres
```

#### Initialize cluster

If you use dnf to install your postgresql, chances are the binary file is in `/usr/bin/`

``` sh
export PATH=/usr/bin:$PATH
initdb /tmp/pgsql
```

#### Remove default acess

Remove last lines from `/tmp/pgsql/pg_hba.conf`

``` ini
host    all             all             127.0.0.1/32            trust
host    all             all             ::1/128                 trust
host    replication     all             127.0.0.1/32            trust
host    replication     all             ::1/128                 trust
```

#### Add access for users and a database

Add new lines to `/tmp/pgsql/pg_hba.conf`

``` ini
host    mydb             myuser          127.0.0.1/32            scram-sha-256
host    mydb             myuser          ::1/128                 scram-sha-256
host    postgres         repl            127.0.0.1/32            scram-sha-256
host    postgres         repl            ::1/128                 scram-sha-256
```

#### Set password_encryption

Set `password_encryption` value in `/tmp/pgsql/postgresql.conf` to be `scram-sha-256`

``` sh
password_encryption = scram-sha-256
```

For version 12/13, the default is `md5`, while for version 14 and above, it is `scram-sha-256`. Therefore, you should ensure that the value in `/tmp/pgsql/postgresql.conf` matches the value in `/tmp/pgsql/pg_hba.conf`.

#### Start PostgreSQL

``` sh
pg_ctl  -D /tmp/pgsql/ start
```

Here, you may encounter issues such as the port being occupied or permission being denied. If you experience a failure, you can go to `/tmp/pgsql/log` to check the reason.

You can use

``` sh
pg_isready
```

to test

#### Add users and a database

``` sh
createuser -P myuser
createdb -E UTF8 -O myuser mydb
```

#### Verify access

For the user `myuser` (standard) use `mypass`

``` sh
psql -h localhost -p 5432 -U myuser mydb
\q
```

#### Add pgagroal user

``` sh
sudo su -
useradd -ms /bin/bash pgagroal
passwd pgagroal
exit
```

#### Create pgagroal configuration

Create the `pgagroal_hba.conf` configuration file

``` ini
cat > pgagroal_hba.conf
#
# TYPE  DATABASE USER  ADDRESS  METHOD
#
host    all      all   all      all
```

Create the `pgagroal.conf` configuration file

``` ini
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

In our main section called `[pgagroal]` we setup [**pgagroal**](https://github.com/agroal/pgagroal) to listen on all network addresses.
Logging will be performed at `info` level and put in a file called `/tmp/pgagroal.log`. We will use 100 connections at a maximum, and they will idle out after 10 minutes. No validation will be performed.
Last we specify the location of the `unix_socket_dir` used for management operations.

Next we create a section called `[primary]` which has the information about our PostgreSQL instance. In this case it is running on localhost on port 5432.

#### Start pgagroal

``` sh
pgagroal -c pgagroal.conf -a pgagroal_hba.conf
```

#### Connect through pgagroal

``` sh
psql -h localhost -p 2345 -U myuser mydb
```

#### Shutdown pgagroal

``` sh
pgagroal-cli -c pgagroal.conf shutdown
```

## Basic git guide

Here are some links that will help you

* [How to Squash Commits in Git](https://www.git-tower.com/learn/git/faq/git-squash)
* [ProGit book](https://github.com/progit/progit2/releases)

### Start by forking the repository

This is done by the "Fork" button on GitHub.

### Clone your repository locally

This is done by

```sh
git clone git@github.com:<username>/pgagroal.git
```

### Add upstream

Do

```sh
cd pgagroal
git remote add upstream https://github.com/agroal/pgagroal.git
```

### Do a work branch

```sh
git checkout -b mywork main
```

### Make the changes

Remember to verify the compile and execution of the code

### AUTHORS

Remember to add your name to

```
AUTHORS
```

in your first pull request

### Multiple commits

If you have multiple commits on your branch then squash them

``` sh
git rebase -i HEAD~2
```

for example. It is `p` for the first one, then `s` for the rest

### Rebase

Always rebase

``` sh
git fetch upstream
git rebase -i upstream/main
```

### Force push

When you are done with your changes force push your branch

``` sh
git push -f origin mywork
```

and then create a pull requests for it

### Repeat

Based on feedback keep making changes, squashing, rebasing and force pushing

### Undo

Normally you can reset to an earlier commit using `git reset <commit hash> --hard`. 
But if you accidentally squashed two or more commits, and you want to undo that, 
you need to know where to reset to, and the commit seems to have lost after you rebased. 

But they are not actually lost - using `git reflog`, you can find every commit the HEAD pointer
has ever pointed to. Find the commit you want to reset to, and do `git reset --hard`.
