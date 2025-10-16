# pgagroal Prometheus Metrics Documentation Generator

This directory contains tools for generating comprehensive documentation for pgagroal Prometheus metrics.

## Files

- `prometheus.py` - Main documentation generator script
- `extra.info` - Detailed metric descriptions and attribute information
- `README.md` - This file

## Usage

The script fetches live metrics from a running pgagroal instance and combines them with detailed descriptions from `extra.info` to generate documentation in multiple formats.

### Basic Usage

```bash
# Generate all formats (markdown + HTML + TOC)
python3 prometheus.py 5000 extra.info

# Generate only markdown with table of contents
python3 prometheus.py 5000 extra.info --md --toc

# Generate manual format (suitable for documentation websites)
python3 prometheus.py 5000 extra.info --manual

# Generate only HTML
python3 prometheus.py 5000 extra.info --html
```

### Command Line Options

- `port` - Port number where pgagroal metrics endpoint is running
- `extra_info_file` - Path to the extra.info file
- `--manual` - Generate markdown in manual format with `\newpage` and bold metric names
- `--toc` - Include table of contents in output
- `--md` - Generate detailed markdown output (prometheus.md)
- `--html` - Generate HTML output (prometheus.html)

### Output Files

- `prometheus.md` - Markdown documentation
- `prometheus.html` - HTML documentation with styling

## Requirements

- Python 3.6+
- `requests` library (`pip install requests`)
- Running pgagroal instance with metrics enabled

## Example

```bash
# Start pgagroal with metrics on port 5000
# Then generate documentation:
cd contrib/prometheus_scrape
python3 prometheus.py 5000 extra.info --md --html --toc
```

This will create both `prometheus.md` and `prometheus.html` with complete metric documentation including descriptions, attributes, examples, and a table of contents.