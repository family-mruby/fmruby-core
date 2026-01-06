#!/bin/bash

# Load environment variables from .env file
SCRIPT_DIR=$(cd $(dirname $0); pwd)
if [ -f "$SCRIPT_DIR/../.env" ]; then
    source "$SCRIPT_DIR/../.env"
fi

IDF_VER=${ESP_IDF_VERSION:-v5.5.1}

docker build \
  --build-arg IDF_VER=$IDF_VER \
  --build-arg USER_ID=$(id -u) \
  --build-arg GROUP_ID=$(id -g) \
  -t esp32_build_container:$IDF_VER \
  .