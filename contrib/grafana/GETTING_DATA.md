# Getting Data for Grafana Dashboards

This guide explains how to enable pgagroal metrics so your Grafana dashboards display data.

## Step 1: Enable Metrics in pgagroal

### Option A: Edit Configuration File

1. **Locate your `pgagroal.conf` file** (usually at `/etc/pgagroal/pgagroal.conf` or specified with `-c` flag)

2. **Add or update the `metrics` setting** in the `[pgagroal]` section:
   ```ini
   [pgagroal]
   host = 0.0.0.0           # Use 0.0.0.0 to allow Docker containers to reach metrics
   port = 2345
   metrics = 5001           # Enable metrics on port 5001
   # ... other settings
   ```

   > **Note**: If you use `host = localhost`, the metrics endpoint will only be accessible from the local machine and Docker containers will not be able to reach it. Use `host = 0.0.0.0` or your machine's IP address to allow external access.

3. **Restart pgagroal** to apply the changes:
   ```bash
   # If running as a service
   sudo systemctl restart pgagroal
   
   # Or if running manually, stop and restart
   pgagroal-cli shutdown
   pgagroal -c /path/to/pgagroal.conf -a /path/to/pgagroal_hba.conf
   ```

### Option B: Use CLI (Runtime Configuration)

You can set the metrics port using the CLI:

```bash
pgagroal-cli conf set metrics 5001
```

> **Note**: This updates the configuration but requires a restart of pgagroal to take effect.

## Step 2: Verify That Metrics Are Available

Test that metrics are being exposed:

```bash
# Check if metrics endpoint is accessible
curl http://localhost:5001/metrics

# You should see output like:
# pgagroal_state 1
# pgagroal_pipeline_mode 1
# pgagroal_active_connections 5
# pgagroal_total_connections 10
# pgagroal_max_connections 100
# ... many more metrics
```

If you see metrics output, pgagroal is correctly exposing metrics.

## Step 3: Configure Prometheus to Scrape Metrics

### If using the provided docker-compose.yml:

1. **Edit `prometheus.yml`** in the `contrib/grafana` directory:
   ```yaml
   global:
     scrape_interval: 15s

   scrape_configs:
     - job_name: "pgagroal"
       metrics_path: "/metrics"
       static_configs:
         - targets: ["host.docker.internal:5001"]  # Adjust if pgagroal is on different host/port
   ```

2. **If pgagroal is running outside Docker (on the host machine):**
   - Use `host.docker.internal:5001` (works on Docker Desktop)
   - Or use your machine's IP address: `192.168.x.x:5001`
   - Or use `172.17.0.1:5001` (default Docker bridge gateway)
   - **Important**: Ensure pgagroal is listening on that interface (not just `localhost`)

3. **If pgagroal is running within Docker:**
   - Use the container name or service name: `pgagroal:5001`
   - Make sure both containers are on the same Docker network

4. **Restart Prometheus** (if using docker-compose):
   ```bash
   docker compose restart prometheus
   ```

### Verify That Prometheus Is Scraping

1. **Open Prometheus UI**: http://localhost:9090

2. **Check Targets**:
   - Go to Status → Targets
   - Verify `pgagroal` target is shown as "UP" (green)

3. **Query a metric**:
   - Go to Graph tab
   - Enter: `pgagroal_state`
   - Click "Execute"
   - You should see a value (1 if running)

## Step 4: Verify Grafana Can Access Data

1. **Open Grafana**: http://localhost:3000
   - **Login**: **Username**: `admin`, **Password**: `admin` (default credentials)

2. **Check Data Source** (already pre-configured if using docker-compose):
   - Go to Configuration → Data Sources
   - Click on **Prometheus**
   - Click "Save & Test"
   - Should show "Data source is working"

3. **View Dashboards**:
   - Go to Dashboards
   - Open "pgagroal Overview" (or any dashboard)
   - Panels should now show data instead of "No data"

## Troubleshooting

### No metrics available

**Problem**: `curl http://localhost:5001/metrics` returns connection refused or 404

**Solutions** (check in this order):
1. Verify `metrics = 5001` is in `pgagroal.conf` (not commented out)
2. Verify pgagroal is running: `ps aux | grep pgagroal`
3. Check if metrics port is open: `netstat -tlnp | grep 5001` or `ss -tlnp | grep 5001`
4. Verify pgagroal is listening on the correct interface (use `0.0.0.0` for Docker access)
5. Check pgagroal logs for errors
6. Restart pgagroal if configuration was changed

### Prometheus can't scrape

**Problem**: Prometheus target is shown as "DOWN"

**Solutions**:
- Verify pgagroal is listening on the correct interface and port
- Verify metrics endpoint is accessible from Prometheus container:
  ```bash
  docker compose exec prometheus wget -O- http://host.docker.internal:5001/metrics
  ```
  > **Note**: This only works if pgagroal is listening on port 5001 and bound to an accessible interface (not `localhost`).
- Check network connectivity between Prometheus and pgagroal
- Verify the target URL in `prometheus.yml` is correct
- Check Prometheus logs: `docker compose logs prometheus`

### Grafana shows "No data"

**Problem**: Dashboards load but panels show "No data"

**Solutions**:
- Verify Prometheus has data: Query `pgagroal_state` in Prometheus UI
- Check Grafana data source is configured correctly
- Verify time range in Grafana (top right) - try "Last 6 hours"
- Check if metrics exist: Query `{__name__=~"pgagroal.*"}` in Prometheus
- Ensure pgagroal has active connections (some metrics only appear with activity)

### Some panels are empty

**Problem**: Some specific panels have no data

**Solutions**:
- Query rate panels require active database queries
- Per-database panels require connections to specific databases
- Authentication panels require authentication attempts
- Check that the specific metric exists: Query it directly in Prometheus

## Advanced: Metrics Caching

To improve performance, you can enable metrics caching in pgagroal:

```ini
[pgagroal]
metrics = 5001
metrics_cache_max_age = 30S    # Cache for 30 seconds
metrics_cache_max_size = 1M    # Max 1MB cache
```

This reduces load on pgagroal when Prometheus scrapes frequently.

## Next Steps

Once metrics are flowing:
- Explore the dashboards to see connection pool status, performance, and authentication
- Set up alerts in Grafana based on metrics (e.g., connection saturation, authentication failures)
- Customize dashboards for your specific needs
