#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
web_scratch="$(mktemp -d)"
trap 'rm -rf "$web_scratch"' EXIT
cjson_include="${CJSON_INCLUDE_DIR:-$HOME/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32/include/json/cJSON}"
if [[ ! -f "$cjson_include/cJSON.h" ]]; then
  echo 'Missing cJSON headers; install the PlatformIO Arduino framework or set CJSON_INCLUDE_DIR.' >&2
  exit 1
fi
c++ -std=c++17 -Wall -Wextra -Werror -DARDUINO \
  -I "$repo_root/firmware/test/web_spies" -I "$repo_root/firmware/src" -I "$cjson_include" \
  "$repo_root/firmware/test/test_web_security.cpp" -l:libcjson.so.1 -l:libmbedcrypto.so.7 -o "$web_scratch/test_web"
"$web_scratch/test_web"
