## Prometheus metrics for pgagroal

This tutorial will show you how to do basic  [Prometheus](https://prometheus.io/) setup  for [**pgagroal**](https://github.com/agroal/pgagroal).

[**pgagroal**](https://github.com/agroal/pgagroal) is able to provide a set of metrics about what it is happening within the pooler,
so that a Prometheus instance can collect them and help you monitor the pooler.

### Preface

This tutorial assumes that you have already an installation of [PostgreSQL](https://www.postgresql.org) 13 (or higher) and [**pgagroal**](https://github.com/agroal/pgagroal).

In particular, this tutorial refers to the configuration done in [Install pgagroal](https://github.com/agroal/pgagroal/blob/master/doc/tutorial/01_install.md).

### Change the pgagroal configuration

In order to enable to export of the metrics, you need to add the `metrics` option in the main `pgagroal.conf` configuration. The value of this setting is the TCP/IP port number that Prometheus will use to grab the exported metrics.

Add a line like the following to `/etc/pgagroal/pgagroal.conf` by editing such file with your editor of choice:

```
metrics = 2346
```

Place it withingr the `[pgagroal]` section, like

```
[pgagroal]
...
metrics = 2346
```

This will bind the TCP/IP port number `2346` to the metrics export.

See [the pgagroal configuration settings](https://github.com/agroal/pgagroal/blob/master/doc/CONFIGURATION.md#pgagroal) with particular regard to `metrics`, `metrics_cache_max_age` and `metrics_cache_max_size` for more details.

### Restart pgagroal

In order to apply changes, you need to restart [**pgagroal**](https://github.com/agroal/pgagroal), therefore run the following commands
as the [**pgagroal**](https://github.com/agroal/pgagroal) operating system user:

```
pgagroal-cli shutdown
pgagroal -d
```

If you need to specify other configuration files, for example for remote management (see [the related tutorial](https://github.com/agroal/pgagroal/blob/master/doc/tutorial/03_remote_management.md)), add them on the [**pgagroal**](https://github.com/agroal/pgagroal) command line.
If the cofiguration files have standard names, you can omit them.

### Get Prometheus metrics

Once [**pgagroal**](https://github.com/agroal/pgagroal) is running you can access the metrics with a browser at the pooler address, specifying the `metrics` port number and routing to the `/metrics` page. For example, point your web browser at:

```
http://localhost:2346/metrics
```

It is also possible to get an explaination of what is the meaning of each metric by pointing your web browser at:

```
http://localhost:2346/
```

## Prometheus metrics for pgagroal-vault

This tutorial will show you how to do basic  [Prometheus](https://prometheus.io/) setup  for [**pgagroal-vault**](https://github.com/agroal/pgagroal).

**pgagroal-vault** is able to provide a set of metrics about what it is happening within the vault, so that a Prometheus instance can collect them and help you monitor the vault activities.

### Change the pgagroal-vault configuration

In order to enable to export of the metrics, you need to add the `metrics` option in the main `pgagroal_vault.conf` configuration. The value of this setting is the TCP/IP port number that Prometheus will use to grab the exported metrics.

Add a line like the following to `/etc/pgagroal/pgagroal_vault.conf` by editing such file with your editor of choice:

```
metrics = 2501
```

Place it within the `[pgagroal-vault]` section, like

```
[pgagroal-vault]
...
metrics = 2501
```

This will bind the TCP/IP port number `2501` to the metrics export.

See [the pgagroal-vault configuration settings](https://github.com/agroal/pgagroal/blob/master/doc/VAULT.md#pgagroal-vault) with particular regard to `metrics`, `metrics_cache_max_age` and `metrics_cache_max_size` for more details.

### Get Prometheus metrics

Once **pgagroal-vault** is running you can access the metrics with a browser at the pgagroal-vault address, specifying the `metrics` port number and routing to the `/metrics` page. For example, point your web browser at:

```
http://localhost:2501/metrics
```

It is also possible to get an explaination of what is the meaning of each metric by pointing your web browser at:

```
http://localhost:2501/
```

## TLS support
To add TLS support for Prometheus metrics, first we need a self-signed certificate.
1. Generate CA key and certificate
```bash
openssl genrsa -out ca.key 2048
openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 -out ca.crt -subj "/CN=My Local CA"
```

2. Generate server key and CSR
```bash
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr -subj "/CN=localhost"
```

3. Create a config file for Subject Alternative Name
```bash
cat > server.ext << EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
IP.1 = 127.0.0.1
EOF
```

4. Sign the server certificate with our CA
```bash
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 3650 -sha256 -extfile server.ext
```

5. Generate client key and certificate
```bash
openssl genrsa -out client.key 2048
openssl req -new -key client.key -out client.csr -subj "/CN=Client Certificate"
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt -days 3650 -sha256
```

6. Create PKCS#12 file (Optional, needed for browser import)
```bash
openssl pkcs12 -export -out client.p12 -inkey client.key -in client.crt -certfile ca.crt -passout pass:<your_password>
```

Edit `pgagroal.conf` to add the following keys under pgagroal section:
```
[pgagroal]
.
.
.
metrics_cert_file=<path_to_server_cert_file>
metrics_key_file=<path_to_server_key_file>
metrics_ca_file=<path_to_ca_file>
```

You can now access the metrics at `https://localhost:5001` using curl as follows:
```
curl -v -L "https://localhost:5001" --cacert <path_to_ca_file> --cert <path_to_client_cert_file> --key <path_to_client_key_file>
```

(Optional) If you want to access the page through the browser:
- First install the certificates on your system
    - For Fedora:
    ```
    # Create directory if it doesn't exist
    sudo mkdir -p /etc/pki/ca-trust/source/anchors/

    # Copy CA cert to the trust store
    sudo cp ca.crt /etc/pki/ca-trust/source/anchors/

    # Update the CA trust store
    sudo update-ca-trust extract
    ```

    - For Ubuntu:
    ```
    # Copy the CA certificate to the system certificate store
    sudo cp ca.crt /usr/local/share/ca-certificates/

    # Update the CA certificate store
    sudo update-ca-certificates
    ```

    - For MacOS:
        - Open Keychain Access and import the certificate file
        - Set the certificate to "Always Trust"

- For browsers like Firefox
    - Go to Menu → Preferences → Privacy & Security
    - Scroll down to "Certificates" section and click "View Certificates"
    - Go to "Authorities" tab and click "Import"
    - Select your `ca.crt` file
    - Check "Trust this CA to identify websites" and click OK
    - Go to "Your Certificates" tab
    - Click "Import" and select the `client.p12` file
    - Enter the password you set when creating the PKCS#12 file

- For browsers like Chrome/Chromium
    - For client certificates, go to Settings → Privacy and security → Security → Manage certificates
    - Click on "Import" and select your `client.p12` file
    - Enter the password you set when creating it

You can now access metrics at `https://localhost:5001`
