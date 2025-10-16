\newpage

# Prometheus metrics

[**pgagroal**][pgagroal] has the following [Prometheus][prometheus] metrics.

## pgagroal_state

Provides the operational status of the pgagroal connection pooler service, indicating if it's running normally (1) or in graceful shutdown mode (2).

## pgagroal_pipeline_mode

Shows the current pipeline mode configuration of pgagroal, determining how connection pipelining is handled.

## pgagroal_server_error

Counts the total number of errors encountered per configured PostgreSQL server backend.

| Attribute | Description |
| :-------- | :---------- |
| name | The configured name/identifier for the PostgreSQL server. |
| state | Current state of the server (not_init, init, free, in_use, gracefully, etc.). |

## pgagroal_logging_info

Accumulates the total number of informational (INFO level) log messages produced by pgagroal since its last startup.

## pgagroal_logging_warn

Counts the total number of warning (WARN level) messages logged by pgagroal, potentially indicating recoverable issues.

## pgagroal_logging_error

Tallies the total number of error (ERROR level) messages from pgagroal, often signaling problems needing investigation.

## pgagroal_logging_fatal

Records the total count of fatal (FATAL level) errors encountered by pgagroal, usually indicating service termination.

## pgagroal_failed_servers

Reports the current number of PostgreSQL backend servers that are in a failed state and unavailable for connections.

## pgagroal_wait_time

Measures the current waiting time in seconds for clients waiting for available connections from the pool.

## pgagroal_query_count

Tracks the total cumulative number of SQL queries that have been processed through pgagroal since startup.

## pgagroal_connection_query_count

Counts the number of queries processed per individual connection slot in the connection pool.

| Attribute | Description |
| :-------- | :---------- |
| id | The connection slot identifier (0-based index). |
| user | The PostgreSQL username associated with this connection (empty if unassigned). |
| database | The PostgreSQL database name for this connection (empty if unassigned). |
| application_name | The application name reported by the client (empty if unassigned). |

## pgagroal_tx_count

Tracks the total cumulative number of database transactions that have been processed through pgagroal.

## pgagroal_active_connections

Shows the current number of connections in the pool that are actively being used by clients.

## pgagroal_total_connections

Reports the current total number of connections in the pool (active + idle + initializing).

## pgagroal_max_connections

Displays the maximum number of connections that can be maintained in the pgagroal connection pool (configured limit).

## pgagroal_connection

Provides detailed information about each individual connection slot in the pool.

| Attribute | Description | Values |
| :-------- | :---------- | :----- |
| id | The connection slot identifier (0-based index). | 0: Connection slot is available/idle., 1: Connection slot is in use. |
| user | The PostgreSQL username for this connection (empty if not assigned). | |
| database | The PostgreSQL database name for this connection (empty if not assigned). | |
| application_name | The application name for this connection (empty if not assigned). | |
| state | Current state of the connection (not_init, init, free, in_use, gracefully, flush, idle_check, max_connection_age, validation, remove). | |

## pgagroal_session_time_seconds

The session times

## pgagroal_connection_error

Counts the total number of connection errors encountered when attempting to establish or maintain database connections.

## pgagroal_connection_kill

Tracks the total number of connections that have been forcibly killed or terminated.

## pgagroal_connection_remove

Counts the total number of connections that have been removed from the pool (due to errors, expiration, etc.).

## pgagroal_connection_timeout

Records the total number of connection attempts that have timed out.

## pgagroal_connection_return

Tracks the total number of connections that have been successfully returned to the pool after use.

## pgagroal_connection_invalid

Counts the total number of connections that were determined to be invalid and discarded.

## pgagroal_connection_get

Records the total number of requests to obtain a connection from the pool.

## pgagroal_connection_idletimeout

Counts the total number of connections that were closed due to idle timeout.

## pgagroal_connection_max_connection_age

Tracks the total number of connections that were closed due to reaching maximum connection age.

## pgagroal_connection_flush

Counts the total number of connection flush operations performed.

## pgagroal_connection_success

Records the total number of successful connection establishments to PostgreSQL backends.

## pgagroal_auth_user_success

Tracks the total number of successful user authentication attempts.

## pgagroal_auth_user_bad_password

Counts the total number of authentication failures due to incorrect passwords.

## pgagroal_auth_user_error

Records the total number of authentication errors (other than bad passwords) encountered.

## pgagroal_client_wait

Shows the current number of clients waiting for an available connection from the pool.

## pgagroal_client_active

Reports the current number of active client connections being served by pgagroal.

## pgagroal_network_sent

Measures the total number of bytes sent from pgagroal to clients since startup.

## pgagroal_network_received

Measures the total number of bytes received by pgagroal from PostgreSQL servers since startup.

## pgagroal_client_sockets

Shows the current number of network sockets being used for client connections.

## pgagroal_self_sockets

Reports the current number of network sockets being used by pgagroal itself (management, metrics, etc.).

## pgagroal_connection_awaiting

Shows the current number of connections that are on hold/awaiting due to blocking timeout configuration.

## pgagroal_os_info

Displays the operating system version information of the host system running pgagroal.

| Attribute | Description |
| :-------- | :---------- |
| os | Operating system name (Linux, OpenBSD, FreeBSD, Darwin). |
| major | Major version number. |
| minor | Minor version number. |
| patch | Patch version number (0 for BSD systems, actual patch for Linux/macOS). |

## pgagroal_certificates_total

Reports the total number of TLS/SSL certificates configured for pgagroal (main, metrics, server certificates).

## pgagroal_certificates_accessible

Shows the number of configured TLS certificates that are accessible and readable by pgagroal.

## pgagroal_certificates_valid

Counts the number of TLS certificates that are currently valid (not expired, properly formatted).

## pgagroal_certificates_expired

Reports the number of TLS certificates that have already expired.

## pgagroal_certificates_expiring_soon

Shows the number of TLS certificates that will expire within the next 30 days.

## pgagroal_certificates_inaccessible

Counts the number of configured TLS certificate files that cannot be accessed or read.

## pgagroal_certificates_parse_errors

Reports the number of TLS certificates that could not be parsed due to formatting or corruption issues.


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