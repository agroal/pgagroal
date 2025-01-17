# Grafana dashboard for pgagroal

## Getting Started

### Step1: Configure the Prometheus

1. Open the prometheus metric port like `8000` in `pgagroal.conf`.

2. Add the pgagroal instance in Prometheus configuration file `prometheus.yml` like follows:
    ```
    ...
    scrape_configs:
    - job_name: 'pgagroal'
        static_configs:
        - targets: ['localhost:8000']
    ...
    ```

### Step2: Start the Grafana

1. Integrate the Prometheus instance as a data source into Grafana.
2. Open Grafana web page, click <kbd>+</kbd> -> <kbd>Import</kbd>.

3. Upload JSON file `contrib/grafana/dashboard.json` with <kbd>Upload JSON file</kbd>, then change the options if necessary.

4. Click <kbd>Import</kbd>. Let's start using it!