# Testing Grafana Dashboards

This guide explains how to test the Grafana dashboards for pgagroal.

## Prerequisites

- Docker 20.10 or higher (includes Docker Compose V2 plugin)
- A running pgagroal instance with metrics enabled (port 5001 by default)
- Web browser

> **Note**: If you do not have metrics enabled yet, see [GETTING_DATA.md](GETTING_DATA.md) for setup instructions.

## Quick Start

1. **Start the Grafana and Prometheus stack**:
   ```bash
   cd contrib/grafana
   docker compose up -d
   ```

2. **Wait for services to start** (about 10–20 seconds):
   ```bash
   docker compose ps
   ```

3. **Access Grafana**:
   - Open http://localhost:3000
   - Login with `admin`/`admin`
   - The dashboards are provisioned automatically

4. **Access Prometheus** (optional):
   - Open http://localhost:9090
   - Verify that the pgagroal target is shown as "UP"

## Testing Checklist

### Dashboard Loading
- [ ] All three dashboards appear in the Grafana dashboard list:
  - pgagroal Overview
  - pgagroal Pool Details
  - pgagroal Performance
- [ ] Dashboards load without errors
- [ ] No JSON parsing errors in browser console

### Panel Functionality
- [ ] All panels render correctly
- [ ] Time series graphs display data (if metrics are available)
- [ ] Stat panels show correct values
- [ ] Gauge panels display utilization correctly
- [ ] Pie charts render properly
- [ ] Bar gauge (session time histogram) displays correctly

### Overview Dashboard
- [ ] Status panel shows UP/DOWN/GRACEFUL correctly
- [ ] Pipeline mode shows Performance/Session/Transaction
- [ ] Connection utilization gauge reflects actual usage
- [ ] Connection state graph shows all three metrics
- [ ] Historical trend graphs populate with data
- [ ] Alert panels show green/yellow/red states correctly
- [ ] Logging panel shows log level rates (ops/s)

### Pool Details Dashboard
- [ ] Authentication success/failure counts are accurate
- [ ] Authentication pie chart shows correct distribution
- [ ] "Total" stat panels show increasing counters
- [ ] "Rate" graphs show current activity (ops/s)
- [ ] Threshold lines (Yellow/Red) are visible on graphs
- [ ] Leak Risk indicator functions correctly
- [ ] Connections by database shows per-database data
- [ ] Server errors panels update correctly

### Performance Dashboard
- [ ] Query and transaction totals are accurate
- [ ] Query/transaction rates calculate correctly
- [ ] Per-database query rate shows breakdown
- [ ] Network traffic totals and rates update
- [ ] Socket usage graph shows all three socket types

### Data Source
- [ ] Prometheus data source is configured correctly
- [ ] Metrics queries execute without errors
- [ ] Data appears in panels (if pgagroal is running and has data)

## Troubleshooting

For detailed troubleshooting steps, see [GETTING_DATA.md](GETTING_DATA.md#troubleshooting).

### Quick Checks

| Problem | Solution |
|---------|----------|
| Dashboards not appearing | Check `docker compose logs grafana` |
| No data in panels | Verify pgagroal metrics: `curl http://localhost:5001/metrics` |
| Prometheus target DOWN | Check `prometheus.yml` target configuration |
| Utilization gauge shows 0 | Verify `pgagroal_max_connections` is non-zero |

## Stopping the Stack

```bash
docker compose down
```

To remove volumes as well:
```bash
docker compose down -v
```
