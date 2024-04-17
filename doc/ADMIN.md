# `pgagroal-admin` user guide

`pgagroal-admin` is a command line interface to manage users known
to the `pgagroal` connection pooler.
The executable accepts a set of options, as well as a command to execute.
If no command is provided, the program will show the help screen.

The `pgagroal-admin` utility has the following synopsis:

```
pgagroal-admin [ OPTIONS ] [ COMMAND ]
```


## Options

Available options are the following ones:

```
  -f, --file FILE         Set the path to a user file
  -U, --user USER         Set the user name
  -P, --password PASSWORD Set the password for the user
  -g, --generate          Generate a password
  -l, --length            Password length
  -F, --format  text|json Set the output format
  -V, --version           Display version information
  -?, --help              Display help

```

Options can be specified either in short or long form, in any position of the command line.

The `-f` option is mandatory for every operation that involves user management. If no
user file is specified, `pgagroal-admin` will silently use the default one (`pgagroal_users.conf`).

## Commands

### user
The `user` command allows the management of the users known to the connection pooler.
The command accepts the following subcommands:
- `add` to add a new user to the system;
- `del` to remove an existing user from the system;
- `edit` to change the credentials of an existing user;
- `ls` to list all known users within the system.

The command will edit the `pgagroal_users.conf` file or any file specified by means of the `-f` option flag.

Unless the command is run with the `-U` and/or `-P` flags, the execution will be interactive.

Examples:

``` shell
pgagroal-admin user add -U simon -P secret
pgagroal-admin user del -U simon

```

## master-key

The `master-key` command allows the definition of a password to protect the vault of the users,
that is the "container" for users' credentials.


## JSON Output Format

It is possible to obtain the output of a command in a JSON format by specyfing the `-F` (`--format`) option on the command line.
Supported output formats are:
- `text` (the default)
- `json`

For more details, see the corresponding section in the CLI documentation.


## Deprecated commands

The following commands have been deprecated and will be removed
in later releases of `pgagroal`.
For each command, this is the corresponding current mapping
to the working command:

- `add-user` is now `user add`;
- `remove-user` is now `user del`;
- `update-user` is now `user edit`;
- `list-users` is now `user ls`.

Whenever you use a deprecated command, the `pgagroal-admin` will print on standard error a warning message.
If you don't want to get any warning about deprecated commands, you
can redirect the `stderr` to `/dev/null` or any other location with:

```
pgagroal-admin user-add -U luca -P strongPassword 2>/dev/null
```


## Shell completion

There is a minimal shell completion support for `pgagroal-admin`.
See the [Install pgagroal](https://github.com/pgagroal/pgagroal/blob/main/doc/tutorial/01_install.md) for more details.
