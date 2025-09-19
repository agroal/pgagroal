\newpage

# Prometheus metrics

[**pgagroal**][pgagroal] has the following [Prometheus][prometheus] metrics.

## pgagroal_state

The state of pgagroal

| Attribute | Description |
|-----------|------------------------------------|
|value      | State                              |
|           |   * Running                        |
|           |   * Graceful shutdown              |

## pgagroal_pipeline_mode

The mode of pipeline

| Attribute | Description |
|-----------|------------------------------------|
|value 	    | Mode                               |
|           | * Performance                      |
|           | * Session                          |
|           | * Transaction                      |

## pgagroal_logging_info

The number of INFO statements

## pgagroal_logging_warn

The number of WARN statements

## pgagroal_logging_error

The number of ERROR statements

## pgagroal_logging_fatal

The number of FATAL statements

## pgagroal_server_error

Errors for servers

| Attribute | Description |
|-----------|------------------------------------|
|name 	    | The name of the server             |
|state 	    | The server state                   |
|           | * not_init                         |
|           | * primary                          |
|           | * replica                          |
|           | * failover                         |
|           | * failed                           |

## pgagroal_failed_servers

The number of failed servers.

Only set if failover is enabled

## pgagroal_wait_time

The waiting time of clients

## pgagroal_query_count

The number of queries.

Only session and transaction modes are supported

## pgagroal_connection_query_count

The number of queries per connection.

Only session and transaction modes are supported

| Attribute | Description |
|-----------|------------------------------------|
|id 	    | The connection identifier          |
|user 	    | The user name                      |
|database 	| The database                       |
|application_name |	The application name         |

## pgagroal_tx_count

The number of transactions. Only session and transaction modes are supported

## pgagroal_active_connections

The number of active connections

## pgagroal_total_connections

The number of total connections

## pgagroal_max_connections

The maximum number of connections

## pgagroal_connection

Connection information

| Attribute | Description |
|-----------|------------------------------------|
|id 	    | The connection identifier          |
|user 	    | The user name                      |
|database 	| The database                       |
|application_name |	The application name         |
|state 	    | The connection state               |
|           | * not_init                         |
|           | * init                             |
|           | * free                             |
|           | * in_use                           |
|           | * gracefully                       |
|           | * flush                            |
|           | * idle_check                       |
|           | * max_connection_age               |
|           | * validation                       |
|           | * remove                           |

## pgagroal_limit

Limit information

| Attribute | Description |
|-----------|------------------------------------|
|user 	    | The user name                      |
|database 	| The database                       |
|type 	    | The information type               |
|           | * not_init                         |
|           | * min                              |
|           | * initial                          |
|           | * max                              |
|           | * active                           |

## pgagroal_limit_awaiting

Connections awaiting on hold reported by limit entries

| Attribute | Description |
|-----------|------------------------------------|
|user 	    | The user name                      |
|database 	| The database                       |

## pgagroal_session_time

Histogram of session times

## pgagroal_connection_error

Number of connection errors

## pgagroal_connection_kill

Number of connection kills

## pgagroal_connection_remove

Number of connection removes

## pgagroal_connection_timeout

Number of connection time outs

## pgagroal_connection_return

Number of connection returns

## pgagroal_connection_invalid

Number of connection invalids

## pgagroal_connection_get

Number of connection gets

## pgagroal_connection_idletimeout

Number of connection idle timeouts

## pgagroal_connection_max_connection_age

Number of connection max age timeouts

## pgagroal_connection_flush

Number of connection flushes

## pgagroal_connection_success

Number of connection successes

## pgagroal_connection_awaiting

Number of connection suspended due to blocking_timeout

## pgagroal_auth_user_success

Number of successful user authentications

## pgagroal_auth_user_bad_password

Number of bad passwords during user authentication

## pgagroal_auth_user_error

Number of errors during user authentication

## pgagroal_client_wait

Number of waiting clients

## pgagroal_client_active

Number of active clients

## pgagroal_network_sent

Bytes sent by clients. Only session and transaction modes are supported

## pgagroal_network_received

Bytes received from servers. Only session and transaction modes are supported

## pgagroal_client_sockets

Number of sockets the client used

## pgagroal_self_sockets

Number of sockets used by pgagroal itself

[**pgagroal-vault**][pgagroal-vault] has the following [Prometheus][prometheus] metrics.

## pgagroal_vault_logging_info

The number of INFO statements

## pgagroal_vault_logging_warn

The number of WARN statements

## pgagroal_vault_logging_error

The number of ERROR statements

## pgagroal_vault_logging_fatal

The number of FATAL statements

## pgagroal_vault_client_sockets

Number of sockets the client used

## pgagroal_vault_self_sockets

Number of sockets used by pgagroal-vault itself

## pgagroal_os_info

Displays the operating system version information of the host system running pgagroal.

| Attribute | Description |
| :-------- | :---------- |
| os | Operating system name (Linux, OpenBSD, FreeBSD, Darwin). |
| major | Major version number. |
| minor | Minor version number. |
| patch | Patch version number (0 for BSD systems, actual patch for Linux/Darwin). |


## TLS Certificate Metrics

pgagroal automatically monitors all configured TLS certificates and exposes comprehensive metrics about their status, expiration, and properties.

### Certificate Overview Metrics

These metrics are **always exposed** when certificates are configured in `pgagroal.conf`, providing an overview of certificate health even when all certificates are invalid:

## pgagroal_certificates_total

Total number of TLS certificates configured across all components (main server, metrics endpoint, database connections)

## pgagroal_certificates_accessible

Number of TLS certificate files that can be read from disk

## pgagroal_certificates_valid

Number of TLS certificates that are valid and properly formatted

## pgagroal_certificates_expired

Number of TLS certificates that have expired

## pgagroal_certificates_expiring_soon

Number of TLS certificates expiring within 30 days

## pgagroal_certificates_inaccessible

Number of TLS certificate files that cannot be read (missing or permission issues)

## pgagroal_certificates_parse_errors

Number of TLS certificates with parsing or format errors

### Certificate Status Monitoring

## pgagroal_tls_certificate_status

Certificate status indicator - **exposed whenever certificates are configured**, regardless of their validity

| Attribute | Description                        |
|-----------|------------------------------------|
|server     | The component using the certificate|
|path       | Full path to the certificate file  |
|value      | Certificate status                 |
|           | * 1 = valid and accessible         |
|           | * 0 = invalid or inaccessible      |

**Exposure Conditions:**
- **Always exposed** when certificates are configured in `pgagroal.conf`
- Provides monitoring information for both valid and invalid certificates
- Not exposed when no certificates are configured

### Detailed Certificate Information

The following metrics are **only exposed when at least one valid certificate exists**:

## pgagroal_tls_certificate_expiration_seconds

Unix timestamp when the certificate expires

| Attribute | Description                                      |
|-----------|--------------------------------------------------|
|server     | The component using the certificate              |
|           | * pgagroal (main server)                         |
|           | * metrics (metrics endpoint)                     |
|           | * primary, primary1, etc. (database connections) |

**Exposure Conditions:**
- Only exposed for valid certificates
- Not exposed if all configured certificates are invalid/inaccessible

## pgagroal_tls_certificate_key_size_bits

Size of the certificate's public key in bits

| Attribute | Description                        |
|-----------|------------------------------------|
|server     | The component using the certificate|

**Exposure Conditions:**
- Only exposed for valid certificates
- Not exposed if all configured certificates are invalid/inaccessible

## pgagroal_tls_certificate_is_ca

Whether the certificate is a Certificate Authority

| Attribute | Description                         |
|-----------|-------------------------------------|
|server     | The component using the certificate |
|value      | CA status                           |
|           | * 1 = CA certificate                |
|           | * 0 = end-entity certificate        |

**Exposure Conditions:**
- Only exposed for valid certificates
- Not exposed if all configured certificates are invalid/inaccessible

## pgagroal_tls_certificate_key_type

Certificate key algorithm type

| Attribute | Description                         |
|-----------|-------------------------------------|
|server     | The component using the certificate |
|value      | Key type (see Key Type Values)      |
|           | * 0 = UNKNOWN                       |
|           | * 1 = RSA                           |
|           | * 2 = ECDSA                         |
|           | * 3 = ED25519                       |
|           | * 4 = ED448                         |
|           | * 5 = DSA                           |
|           | * 6 = DH                            |

**Exposure Conditions:**
- Only exposed for valid certificates
- Not exposed if all configured certificates are invalid/inaccessible

## pgagroal_tls_certificate_signature_algorithm

Certificate signature algorithm

| Attribute | Description                                          |
|-----------|------------------------------------------------------|
|server     | The component using the certificate                  |
|value      | Signature algorithm (see Signature Algorithm Values) |
|           | * 0 = UNKNOWN                                        |
|           | * 1 = SHA256WithRSA                                  |
|           | * 2 = SHA384WithRSA                                  |
|           | * 3 = SHA512WithRSA                                  |
|           | * 4 = SHA1WithRSA                                    |
|           | * 5 = ECDSAWithSHA256                                |
|           | * 6 = ECDSAWithSHA384                                |
|           | * 7 = ECDSAWithSHA512                                |
|           | * 8 = ED25519                                        |
|           | * 9 = ED448                                          |
|           | * 10 = SHA256WithPSS                                 |
|           | * 11 = SHA384WithPSS                                 |
|           | * 12 = SHA512WithPSS                                 |

**Exposure Conditions:**
- Only exposed for valid certificates
- Not exposed if all configured certificates are invalid/inaccessible

## pgagroal_tls_certificate_info

Comprehensive certificate metadata as labels

| Attribute               | Description                              |
|-------------------------|------------------------------------------|
|server                   | The component using the certificate      |
|subject                  | Certificate subject DN                   |
|issuer                   | Certificate issuer DN                    |
|serial_number            | Certificate serial number                |
|expires_date             | Certificate expiration date              |
|valid_from_date          | Certificate valid from date              |
|key_type_name            | Human-readable key type name             |
|signature_algorithm_name | Human-readable signature algorithm name  |
|key_size                 | Key size in bits                         |
|value                    | Always 1 (use labels for details)        |

**Exposure Conditions:**
- Only exposed for valid certificates
- Not exposed if all configured certificates are invalid/inaccessible

### Certificate Metrics Exposure Behavior

The following table shows exactly which TLS certificate metrics are exposed under different certificate configuration scenarios:

| Metric Name                                      | No Certificates Configured | Certificates Configured (All Invalid) | At Least One Valid Certificate |
|--------------------------------------------------|----------------------------|---------------------------------------|--------------------------------|
| **Summary/Overview Metrics**                     |                            |                                       |                                |
| `pgagroal_certificates_total`                    | Exposed (shows 0)          | Exposed (shows count)                 | Exposed (shows count)          |
| `pgagroal_certificates_accessible`               | Exposed (shows 0)          | Exposed (shows count)                 | Exposed (shows count)          |
| `pgagroal_certificates_valid`                    | Exposed (shows 0)          | Exposed (shows 0)                     | Exposed (shows count)          |
| `pgagroal_certificates_expired`                  | Exposed (shows 0)          | Exposed (shows count)                 | Exposed (shows count)          |
| `pgagroal_certificates_expiring_soon`            | Exposed (shows 0)          | Exposed (shows count)                 | Exposed (shows count)          |
| `pgagroal_certificates_inaccessible`             | Exposed (shows 0)          | Exposed (shows count)                 | Exposed (shows count)          |
| `pgagroal_certificates_parse_errors`             | Exposed (shows 0)          | Exposed (shows count)                 | Exposed (shows count)          |
| **Status Monitoring Metrics**                    |                            |                                       |                                |
| `pgagroal_tls_certificate_status`                | Not exposed                | Exposed (status=0 for all)            | Exposed (status=0/1 per cert)  |
| **Detailed Certificate Metrics**                 |                            |                                       |                                |
| `pgagroal_tls_certificate_expiration_seconds`    | Not exposed                | Not exposed                           | Exposed (valid certs only)     |
| `pgagroal_tls_certificate_key_size_bits`         | Not exposed                | Not exposed                           | Exposed (valid certs only)     |
| `pgagroal_tls_certificate_is_ca`                 | Not exposed                | Not exposed                           | Exposed (valid certs only)     |
| `pgagroal_tls_certificate_key_type`              | Not exposed                | Not exposed                           | Exposed (valid certs only)     |
| `pgagroal_tls_certificate_signature_algorithm`   | Not exposed                | Not exposed                           | Exposed (valid certs only)     |
| `pgagroal_tls_certificate_info`                  | Not exposed                | Not exposed                           | Exposed (valid certs only)     |

#### Metric Categories Explained:

**Summary/Overview Metrics:**
- Always exposed when certificates are configured (regardless of validity)
- Provide aggregate counts and health overview
- Essential for monitoring overall certificate configuration status

**Status Monitoring Metrics:**
- Always exposed when certificates are configured (regardless of validity)
- Critical for alerting on certificate problems
- Shows individual certificate accessibility and validity status

**Detailed Certificate Metrics:**
- Only exposed when at least one valid certificate exists
- Provide detailed information about certificate properties
- Useful for certificate analysis and detailed monitoring

#### Practical Examples:

**Scenario 1: No TLS Configuration**
```ini
# No tls_cert_file, metrics_cert_file, or server tls_cert_file configured
```
**Result:** Summary metrics exposed with zero values, no status or detailed metrics

**Scenario 2: Certificates Configured but File Missing**
```ini
[pgagroal]
tls_cert_file = /missing/certificate.crt
```
**Result:** Summary metrics show counts, status metric shows `status=0`, no detailed metrics

**Scenario 3: Valid Certificate Available**
```ini
[pgagroal]
tls_cert_file = /valid/certificate.crt
```
**Result:** All metrics exposed - summary shows counts, status shows `status=1`, detailed metrics provide certificate information
### Example Prometheus Queries

### Certificate Monitoring Configuration

TLS certificate metrics are automatically enabled when certificates are configured in `pgagroal.conf`:

```ini
[pgagroal]
# Main server certificate
tls_cert_file = /path/to/server.crt

# Metrics endpoint certificate  
metrics_cert_file = /path/to/metrics.crt

[primary]
# Database connection certificate
tls_cert_file = /path/to/client.crt
```

**Note:** Certificates are monitored regardless of whether TLS is enabled (`tls = on/off`), allowing you to track certificate health even when TLS is disabled.

### Example Prometheus Queries

Monitor certificates expiring within 30 days:
```promql
pgagroal_certificates_expiring_soon > 0
```

Calculate days until certificate expiration (only available for valid certificates):
```promql
(pgagroal_tls_certificate_expiration_seconds - time()) / 86400
```

Check certificate accessibility across all servers:
```promql
pgagroal_tls_certificate_status{} == 0
```

Alert on deprecated SHA1 signatures (only available for valid certificates):
```promql
pgagroal_tls_certificate_signature_algorithm{} == 4
```

Monitor certificate health ratio:
```promql
pgagroal_certificates_valid / pgagroal_certificates_total
```

---