# pgagroal security

## Pass-through security

pgagroal use pass-through security by default.

This means that pgagroal delegates to PostgreSQL to determine if the credentials used are valid.

Once a connection is obtained pgagroal will replay the previous communication sequence to verify
the new client. This only works for connections using `trust`, `password` or `md5` authentication
methods, so `scram-sha-256` based connections are not cached.

Note, that this can lead to replay attacks against the `md5` based connections since the hash
doesn't change. Make sure that pgagroal is deployed on a private trusted network, but consider
using either a user vault or authentication query instead.

## User vault

A user vault is a vault which defines the known users and their password.

The vault is static, and is managed through the `pgagroal-admin` tool.

The user vault is specified using the `-u` or `--users` command line parameter.

### Frontend users

The `-F` or `--frontend` command line parameter allows users to be defined for the client to
`pgagroal` authentication. This allows the setup to use different passwords for the `pgagroal` to
PostgreSQL authentication.

All users defined in the frontend authentication must be defined in the user vault (`-u`).

Frontend users (`-F`) requires a user vault (`-u`) to be defined.

## Authentication query

Authentication query will use the below defined function to query the database
for the user password

```
CREATE FUNCTION public.pgagroal_get_password(
  IN  p_user     name,
  OUT p_password text
) RETURNS text
LANGUAGE sql SECURITY DEFINER SET search_path = pg_catalog AS
$$SELECT passwd FROM pg_shadow WHERE usename = p_user$$;
```

This function needs to be installed in each database.

The function requires a user that is able to execute it, like

```
-- Create a role used for the authentication query
CREATE ROLE pgagroal LOGIN;
-- Set the password
\password pgagroal

-- Only allow access to "pgagroal"
REVOKE EXECUTE ON FUNCTION public.pgagroal_get_password(name) FROM PUBLIC;
GRANT EXECUTE ON FUNCTION public.pgagroal_get_password(name) TO pgagroal;
```

Make sure that the user is different from the actual application users accessing
the database. The user accessing the function needs to have its credential present
in the vault passed to the `-S` or `--superuser` command line parameter.

The user executing the authentication query must use either a MD5 or a SCRAM-SHA-256
password protected based account.
