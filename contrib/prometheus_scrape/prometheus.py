#!/usr/bin/env python3

import argparse
import requests
import re
import os
import sys
from typing import Dict, List, Tuple, Optional

def parse_extra_info(file_path: str) -> Dict[str, Dict[str, str]]:
    """Parse the extra info file and return metric descriptions and details."""
    metrics = {}
    current_metric = None
    current_description = []
    current_details = []
    
    with open(file_path, 'r') as f:
        for line in f:
            line = line.rstrip()
            
            if re.match(r'^pgagroal_[a-zA-Z0-9_]+', line):
                # Save previous metric if exists
                if current_metric:
                    metrics[current_metric] = {
                        'description': '\n'.join(current_description),
                        'details': '\n'.join(current_details)
                    }
                
                # Start new metric
                current_metric = line
                current_description = []
                current_details = []
                
            elif line.startswith('+ '):
                current_description.append(line[2:])  # Remove '+ ' prefix
                
            elif line.startswith('* '):
                current_details.append(line)
    
    # Save last metric
    if current_metric:
        metrics[current_metric] = {
            'description': '\n'.join(current_description),
            'details': '\n'.join(current_details)
        }
    
    return metrics

def extract_attributes_and_values_from_details(details: str) -> Tuple[List[Tuple[str, str]], List[Tuple[str, str]]]:
    """Extract attributes and values separately from details."""
    attributes = []
    values = []
    
    for line in details.split('\n'):
        if line.startswith('* '):
            match = re.match(r'^\*\s*([^:]+):\s*(.*)', line)
            if match:
                key = match.group(1).strip()
                desc = match.group(2).strip()
                
                # Check if this is a value definition (numeric key like "1", "0", etc.)
                if re.match(r'^\d+(\.\d+)?$', key):
                    values.append((key, desc))
                else:
                    # This is an attribute (label/dimension)
                    attributes.append((key, desc))
    
    return attributes, values

def find_best_example(metric_name: str, metrics_content: str) -> Optional[str]:
    """Find the best example line for a metric."""
    lines = metrics_content.split('\n')
    best_example = None
    best_priority = 0
    
    for line in lines:
        if line.startswith(metric_name):
            priority = 0
            
            # Prioritize lines with labels
            if '{' in line:
                priority = 2
            else:
                priority = 1
                
            if priority > best_priority:
                best_example = line
                best_priority = priority
    
    return best_example

def generate_manual_metric(metric: Dict, use_h2: bool = False) -> str:
    """Generate manual format for a single metric with attribute table."""
    description = metric['extra'].get('description', '').strip()
    if description:
        cleaned_desc = ' '.join(description.replace('\n', ' ').split())
    else:
        cleaned_desc = metric['help']
    
    if use_h2:
        output = f"## {metric['name']}\n\n"
    else:
        output = f"**{metric['name']}**\n\n"
    output += f"{cleaned_desc}\n\n"
    
    # Extract attributes and values from details
    attributes, values = extract_attributes_and_values_from_details(metric['extra'].get('details', ''))
    
    if attributes:
        if values:
            # 3-column table with values
            output += "| Attribute | Description | Values |\n"
            output += "| :-------- | :---------- | :----- |\n"
            
            # Put values in the first attribute row
            values_str = ", ".join([f"{val}: {desc}" for val, desc in values])
            for i, (key, desc) in enumerate(attributes):
                if i == 0:
                    output += f"| {key} | {desc} | {values_str} |\n"
                else:
                    output += f"| {key} | {desc} | |\n"
        else:
            # 2-column table without values
            output += "| Attribute | Description |\n"
            output += "| :-------- | :---------- |\n"
            
            for key, desc in attributes:
                output += f"| {key} | {desc} |\n"
        
        output += "\n"
    
    return output

def generate_markdown_metric(name: str, help_text: str, metric_type: str, 
                           extra_info: Dict[str, str], example: Optional[str]) -> str:
    """Generate markdown for a single metric."""
    md = f"### {name}\n\n"
    md += f"{help_text} Type is {metric_type}.\n\n"
    
    # Use extra description if available, otherwise use help text
    description = extra_info.get('description', '').strip()
    if description:
        # Clean up description formatting
        cleaned_desc = ' '.join(description.replace('\n', ' ').split())
        md += f"{cleaned_desc}\n\n"
    else:
        md += f"{help_text}\n\n"
    
    # Add attributes table if available
    attributes, values = extract_attributes_and_values_from_details(extra_info.get('details', ''))
    if attributes:
        md += "**Attributes:**\n\n"
        md += "| Attribute | Value |\n"
        md += "| :-------- | :---- |\n"
        for key, value in attributes:
            md += f"| {key} | {value} |\n"
        md += "\n"
    
    # Add example
    md += "**Example:**\n\n"
    if example:
        md += f"```\n{example}\n```\n\n"
    else:
        md += "(No example available)\n\n"
    
    md += "---\n\n"
    return md

def generate_html_metric(name: str, help_text: str, metric_type: str,
                        extra_info: Dict[str, str], example: Optional[str]) -> str:
    """Generate HTML for a single metric."""
    html = f'<h3 id="{name}">{name}</h3>\n'
    html += f"<p>{help_text} Type is {metric_type}.</p>\n"
    
    # Use extra description if available
    description = extra_info.get('description', '').strip()
    if description:
        cleaned_desc = ' '.join(description.replace('\n', ' ').split())
        html += f"<p>{cleaned_desc}</p>\n"
    else:
        html += f"<p>{help_text}</p>\n"
    
    # Add attributes table if available
    attributes, values = extract_attributes_and_values_from_details(extra_info.get('details', ''))
    if attributes:
        html += "<p><strong>Attributes:</strong></p>\n"
        html += "<table>\n<thead><tr><th>Attribute</th><th>Value</th></tr></thead>\n<tbody>\n"
        for key, value in attributes:
            html += f"  <tr><td>{key}</td><td>{value}</td></tr>\n"
        html += "</tbody>\n</table>\n"
    
    # Add example
    html += "<p><strong>Example:</strong></p>\n"
    if example:
        html += f"<pre>{example}</pre>\n"
    else:
        html += "<p>(No example available)</p>\n"
    
    html += "\n"
    return html

def generate_manual_documentation(metrics: List[Dict]):
    """Generate manual documentation file."""
    with open('prometheus.md', 'w') as f:
        f.write("\\newpage\n\n")
        f.write("# Prometheus metrics\n\n")
        f.write("[**pgagroal**][pgagroal] has the following [Prometheus][prometheus] built-in metrics.\n\n")
        for metric in metrics:
            f.write(generate_manual_metric(metric, use_h2=True))

def generate_markdown_documentation(output_file: str, metrics: List[Dict], include_toc: bool):
    """Generate markdown documentation."""
    with open(output_file, 'w') as f:
        # Generate regular detailed format
        f.write("# Prometheus Metrics Documentation\n\n")
        f.write("This document contains all available metrics from the pgagroal system.\n\n")
        
        if include_toc:
            f.write("## Table of Contents\n\n")
            for metric in metrics:
                f.write(f"- [{metric['name']}](#{metric['name']})\n")
            
            f.write(f"\n**Total metrics: {len(metrics)}**\n\n")
            f.write("---\n\n")
        
        # All metrics
        f.write("## pgagroal Metrics\n\n")
        for metric in metrics:
            f.write(generate_markdown_metric(
                metric['name'], metric['help'], metric['type'], 
                metric['extra'], metric['example']
            ))

def generate_html_documentation(output_file: str, metrics: List[Dict], include_toc: bool):
    """Generate HTML documentation."""
    with open(output_file, 'w') as f:
        # HTML header
        f.write("""<html>
  <head>
    <title>pgagroal Prometheus Metrics Documentation</title>
    <style>
      body { font-family: sans-serif; line-height: 1.4; padding: 15px; }
      h1 { border-bottom: 2px solid #ccc; padding-bottom: 5px;}
      h2 { border-bottom: 1px solid #eee; padding-bottom: 5px; margin-top: 25px;}
      h3 { border-bottom: 1px solid #f0f0f0; padding-bottom: 3px; margin-top: 20px;}
      table { border-collapse: collapse; width: 100%; margin: 15px 0; border: 1px solid #ccc; }
      th, td { border: 1px solid #ddd; padding: 6px; text-align: left; vertical-align: top; word-wrap: break-word; }
      th { background-color: #f9f9f9; font-weight: bold; }
      pre { background-color: #f8f8f8; padding: 8px; border: 1px solid #ddd; border-radius: 3px; overflow-x: auto; white-space: pre-wrap; word-wrap: break-word; }
      ul { margin-top: 5px; padding-left: 25px; }
      li { margin-bottom: 4px; }
      .toc { margin-bottom: 20px; padding: 10px; background-color: #fdfdfd; border: 1px solid #eee;}
      .toc h2 { border: none; margin-top: 0;}
      .toc ul { list-style-type: none; padding-left: 0; }
      .toc li a { text-decoration: none; }
      .toc li a:hover { text-decoration: underline; }
    </style>
  </head>
  <body>
  <h1>pgagroal Prometheus Metrics Documentation</h1>
  <p>This document contains all available metrics from the pgagroal connection pooler system.</p>
""")
        
        if include_toc:
            f.write('<div class="toc">\n')
            f.write('<h2>Table of Contents</h2>\n')
            f.write('<ul>\n')
            for metric in metrics:
                f.write(f'  <li><a href="#{metric["name"]}">{metric["name"]}</a></li>\n')
            f.write('</ul>\n')
            
            f.write(f'<p>Total metrics: {len(metrics)}</p>\n')
            f.write('</div>\n')
        
        # All metrics
        f.write('<h2>pgagroal Metrics</h2>\n')
        for metric in metrics:
            f.write(generate_html_metric(
                metric['name'], metric['help'], metric['type'],
                metric['extra'], metric['example']
            ))
        
        f.write('  </body>\n</html>\n')

def main():
    parser = argparse.ArgumentParser(description='Generate pgagroal Prometheus metrics documentation')
    parser.add_argument('port', type=int, help='Port to fetch metrics from')
    parser.add_argument('extra_info_file', help='Extra info file path')
    parser.add_argument('--manual', action='store_true', help='Generate markdown in manual format (\\newpage, bold metrics)')
    parser.add_argument('--toc', action='store_true', help='Generate table of contents')
    parser.add_argument('--md', action='store_true', help='Generate detailed markdown output')
    parser.add_argument('--html', action='store_true', help='Generate HTML output')
    
    args = parser.parse_args()
    
    # Default behavior if no output options specified
    if not any([args.manual, args.toc, args.md, args.html]):
        args.md = True
        args.html = True
        args.toc = True
    
    # Fetch metrics
    try:
        response = requests.get(f'http://localhost:{args.port}/metrics', timeout=10)
        response.raise_for_status()
        metrics_content = response.text
    except requests.RequestException as e:
        print(f"Error: Failed to fetch metrics from http://localhost:{args.port}/metrics: {e}")
        sys.exit(1)
    
    if not metrics_content.strip():
        print("Error: Fetched metrics content is empty")
        sys.exit(1)
    
    # Parse extra info
    if not os.path.exists(args.extra_info_file):
        print(f"Error: Extra info file '{args.extra_info_file}' not found.")
        sys.exit(1)
    
    extra_info = parse_extra_info(args.extra_info_file)
    
    # Process metrics sequentially
    lines = metrics_content.split('\n')
    metrics_data = []
    
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        
        if re.match(r'^#\s*HELP', line):
            parts = line.split(' ', 2)
            if len(parts) >= 3:
                metric_name = parts[1]
                help_text = parts[2]
                
                # Look for TYPE line
                metric_type = "unknown"
                if i + 1 < len(lines) and re.match(rf'^#\s*TYPE\s+{re.escape(metric_name)}', lines[i + 1]):
                    type_parts = lines[i + 1].split(' ', 2)
                    if len(type_parts) >= 3:
                        metric_type = type_parts[2]
                
                # Find best example
                example = find_best_example(metric_name, metrics_content)
                
                # Get extra info
                extra = extra_info.get(metric_name, {'description': '', 'details': ''})
                
                metric_data = {
                    'name': metric_name,
                    'help': help_text,
                    'type': metric_type,
                    'extra': extra,
                    'example': example
                }
                
                metrics_data.append(metric_data)
        
        i += 1
    
    # Generate outputs
    if args.manual:
        generate_manual_documentation(metrics_data)
        print("Manual documentation generated: prometheus.md")
    else:
        if args.md:
            generate_markdown_documentation("prometheus.md", metrics_data, args.toc)
            print("Markdown documentation generated: prometheus.md")
        
        if args.html:
            generate_html_documentation("prometheus.html", metrics_data, args.toc)
            print("HTML documentation generated: prometheus.html")
    
    # Print summary
    print(f"Total metrics documented: {len(metrics_data)}")

if __name__ == '__main__':
    main()