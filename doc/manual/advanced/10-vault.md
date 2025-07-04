\newpage

# pgagroal-vault

## Enable rotation of frontend passwords

In the main configuration file of [**pgagroal**][pgagroal] add the following configuration for rotating frontend passwords and make sure management port is enabled in the [**pgagroal**][pgagroal] section of the configuration file.

```
management = 2347
rotate_frontend_password_timeout = 60
rotate_frontend_password_length = 12
```

## Configuration

In order to run `pgagroal-vault`, you need to configure the vault `pgagroal_vault.conf` configuration file, that will tell the vault how to work, which address to listen, address of management service in [**pgagroal**][pgagroal] so on,
and then `pgagroal_vault_users.conf` that will instrument the vault about the admin username and password of the remote management.

### pgagroal-vault.conf

It is now time to create the main `/etc/pgagroal/pgagroal_vault.conf` configration file, with your editor of choice or using `cat` from the command line, create the following content:

```
cd /etc/pgagroal
cat > pgagroal_vault.conf
[pgagroal-vault]
host = localhost
port = 2500

metrics = 2501

ev_backend = auto

log_type = console
log_level = info
log_path = /tmp/pgagroal-vault.log

[main]
host = localhost
port = 2347
user = admin
```

and press `Ctrl-d` (if running `cat`) to save the file.

### Add users file

As the [**pgagroal**][pgagroal] operating system user, run the following command:

```
pgagroal-admin -f /etc/pgagroal/pgagroal_vault_users.conf -U admin -P admin1234 user add
```

The above will create the `admin` username with the `admin1234` password. Alternately, `/etc/pgagroal/pgagroal_admins.conf` can be provided for vault users information.

See [the documentation about `pgagroal_vault.conf` for more details](https://github.com/agroal/pgagroal/blob/master/doc/VAULT.md).

## Start the vault

It is now time to start `pgagroal-vault`, so as the [**pgagroal**][pgagroal] operating system user run:

```
pgagroal-vault -d
```

If both `pgagroal` and `pgagroal-vault` are on the same operating system they can use the same `pgagroal_admins.conf` file.

This command initializes an HTTP server on localhost port 2500, which is primed to exclusively handle GET requests from clients.

## Connect to the vault

Since we have deployed an HTTP server we can simply use `curl` to send GET requests

### Correct requests

If the requested URL is of form `http://<hostname>:<port>/users/<frontend_user>` such that `<frontend_user>` exists, the server will return a header response with a 200 status code and the frontend password corresponding to the `<frontend_user>` in the response body.

**Example**

`
curl -i http://localhost:2500/users/myuser
`

Output

```
HTTP/1.1 200 OK
Content-Type: text/plain


password
```

### Incorrect requests

All the POST requests will be ignored and the server will send a `HTTP 404 ERROR` as a response.

Any URL other than the format: `http://<hostname>:<port>/users/*` will result in `HTTP 404 ERROR`.

**Example**

`
curl -i http://localhost:2500/user
`

Output

```
HTTP/1.1 404 Not Found

```

A URL of form `http://<hostname>:<port>/users/<frontend_user>` such that `<frontend_user>` does not exist will also give `HTTP 404 ERROR`.

**Example**

`
curl -i http://localhost:2500/users/randomuser
`

Output

```
HTTP/1.1 404 Not Found

```


## Transport Level Security (TLS)

### Enable TLS

It is now time to modify the `[pgagroal-vault]` section of configuration file `/etc/pgagroal/pgagroal_vault.conf` with your editor of choice by adding the following lines in the [pgagroal-vault] section.

```
tls = on
tls_cert_file = </path/to/server.crt>
tls_key_file = </path/to/server.key>
```

This will add TLS support to the server alongside the standard `http` endpoint, allowing clients to make requests to either the `https` or `http` endpoint.

### Only Server Authentication

If you wish to do only server authentication the aforementioned configuration suffice.

**Client Request**

```
curl --cacert </path/to/server_root_ca.crt> -i https://localhost:2500/users/<frontend_user>
```

### Full Client and Server Authentication

To enable the server to request the client certificates add the following configuration lines

```
tls = on
tls_cert_file = </path/to/server.crt>
tls_key_file = </path/to/server.key>
tls_ca_file = </path/to/client_root_ca.crt>
```

**Client Request**

```
curl --cert </path/to/client.crt> --key </path/to/client.key> --cacert </path/to/server_root_ca.crt> -i https://localhost:2500/users/<frontend_user>
```
