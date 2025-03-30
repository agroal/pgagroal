## Creating Certificates

This tutorial will show you how to create self-signed certificate for the server, valid for 365 days, use the following OpenSSL command, replacing `dbhost.yourdomain.com` with the server's host name, here `localhost`:

```
openssl req -new -x509 -days 365 -nodes -text -out server.crt \
  -keyout server.key -subj "/CN=dbhost.yourdomain.com"
```

then do -

```
chmod og-rwx server.key
```

because the server will reject the file if its permissions are more liberal than this. For more details on how to create your server private key and certificate, refer to the OpenSSL documentation.

For the purpose of this tutorial we will assume the client certificate and key same as the server certificate and server key and therefore, these equations always holds -

* `</path/to/client.crt>` = `</path/to/server.crt>`
* `</path/to/client.key>` = `</path/to/server.key>`
* `</path/to/server_root_ca.crt>` = `</path/to/server.crt>`
* `</path/to/client_root_ca.crt>` = `</path/to/server_root_ca.crt>`

## TLS in `pgagroal`

This tutorial will show you how to enable TLS between `client` and [**pgagroal**](https://github.com/agroal/pgagroal).

### Preface

This tutorial assumes that you have already an installation of [PostgreSQL](https://www.postgresql.org) 13 (or higher) and [**pgagroal**](https://github.com/agroal/pgagroal).

In particular, this tutorial refers to the configuration done in [Install pgagroal](https://github.com/agroal/pgagroal/blob/master/doc/tutorial/01_install.md).

### Modify the `pgagroal` configuration

It is now time to modify the [pgagroal] section of configuration file `/etc/pgagroal/pgagroal.conf`, with your editor of choice by adding the following lines in the [pgagroal] section.

```
tls = on
tls_cert_file = </path/to/server.crt>
tls_key_file = </path/to/server.key>
```

**Only Server Authentication**

If you wish to do only server authentication the aforementioned configuration suffice.

**Client Request**

```
PGSSLMODE=verify-full PGSSLROOTCERT=</path/to/server_root_ca.crt> psql -h localhost -p 2345 -U <postgres_user> <postgres_database>
```

**Full Client and Server Authentication**

To enable the server to request the client certificates add the following configuration lines

```
tls = on
tls_cert_file = </path/to/server.crt>
tls_key_file = </path/to/server.key>
tls_ca_file = </path/to/client_root_ca.crt>
```

**Client Request**

```
PGSSLMODE=verify-full PGSSLCERT=</path/to/client.crt> PGSSLKEY=</path/to/client.key> PGSSLROOTCERT=</path/to/server_root_ca.crt> psql -h localhost -p 2345 -U <postgres_user> <postgres_database>
```

## TLS in `pgagroal-vault`

This tutorial will show you how to enable tls between [**pgagroal-vault**](https://github.com/agroal/pgagroal) and the client (`curl`).

### Preface

This tutorial assumes that you have already an installation of [PostgreSQL](https://www.postgresql.org) 13 (or higher) and [**pgagroal**](https://github.com/agroal/pgagroal).

This tutorial also assumes that you have a functional [**pgagroal-vault**](https://github.com/agroal/pgagroal).

In particular, this tutorial refers to the configuration done in [Install pgagroal](https://github.com/agroal/pgagroal/blob/master/doc/tutorial/01_install.md) and
the configuration done in [Setup pgagroal-vault](https://github.com/agroal/pgagroal/blob/master/doc/tutorial/07_vault.md).

### Modify the `pgagroal-vault` configuration

It is now time to modify the [pgagroal-vault] section of configuration file `/etc/pgagroal/pgagroal_vault.conf`, with your editor of choice by adding the following lines in the [pgagroal-vault] section.

```
tls = on
tls_cert_file = </path/to/server.crt>
tls_key_file = </path/to/server.key>
```

This will add TLS support to the server alongside the standard `http` endpoint, allowing clients to make requests to either the `https` or `http` endpoint.

**Only Server Authentication**

If you wish to do only server authentication the aforementioned configuration suffice.

**Client Request**

```
curl --cacert </path/to/server_root_ca.crt> -i https://localhost:2500/users/<frontend_user>
```

**Full Client and Server Authentication**

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
