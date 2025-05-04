#!/bin/bash
#
# Copyright (C) 2025 The pgagroal community
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list
# of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this
# list of conditions and the following disclaimer in the documentation and/or other
# materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may
# be used to endorse or promote products derived from this software without specific
# prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
# TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

set -euo pipefail

# Go to the root of the project (script is in test/, go one level up)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

# Detect container engine: Docker or Podman
if command -v docker &> /dev/null; then
  CONTAINER_ENGINE="docker"
elif command -v podman &> /dev/null; then
  CONTAINER_ENGINE="podman"
else
  echo "Neither Docker nor Podman is installed. Please install one to proceed."
  exit 1
fi

# Variables
IMAGE_NAME="pgagroal-test"
CONTAINER_NAME="pgagroal_test_container"
DOCKERFILE="./test/Dockerfile.testsuite"
LOG_DIR="./build/log"
COVERAGE_DIR="./build/coverage"

# Clean up any old container
$CONTAINER_ENGINE rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

# Build the image
$CONTAINER_ENGINE build -f "$DOCKERFILE" -t "$IMAGE_NAME" .

# Run it, generate & invoke the in-container script
$CONTAINER_ENGINE run --name "$CONTAINER_NAME" "$IMAGE_NAME" bash -c '
set -e
cd /pgagroal/build/
./testsuite.sh
./testsuite.sh run-configs
echo "Generating coverage reports…"
mkdir -p coverage
find . -name "*.gcda" -o -name "*.gcno"
gcovr -r /pgagroal/src --object-directory . --html --html-details -o coverage/index.html
gcovr -r /pgagroal/src --object-directory . > coverage/summary.txt
gcovr -r /pgagroal/src --object-directory . --xml -o coverage/coverage.xml
echo "Coverage in /pgagroal/build/coverage"
'

# Copy back logs and coverage
mkdir -p "$LOG_DIR" "$COVERAGE_DIR"
$CONTAINER_ENGINE cp "$CONTAINER_NAME:/pgagroal/build/log/." "$LOG_DIR"
$CONTAINER_ENGINE cp "$CONTAINER_NAME:/pgagroal/build/coverage/." "$COVERAGE_DIR"

echo " Fixing file ownership for host user"
if ! sudo chown -R "$(id -u):$(id -g)" "$LOG_DIR" "$COVERAGE_DIR"; then
  echo " Could not change ownership. You might need to clean manually."
fi

echo "Logs -->  $LOG_DIR"
echo "Coverage --> $COVERAGE_DIR"
