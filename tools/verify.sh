#!/usr/bin/env bash
# Run repeatable software checks without modifying source or flashing hardware.
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"
firmware=false
browser=false
for option in "$@"; do
    case "$option" in
        --firmware) firmware=true ;;
        --browser) browser=true ;;
        *) printf 'Usage: %s [--firmware] [--browser]\n' "$0" >&2; exit 2 ;;
    esac
done
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/smoker-verification.XXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT

uv run --no-project --with pytest python -m pytest -q -p no:cacheprovider
for suite in domain control; do
    "${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread \
        -I firmware/src "firmware/test/test_${suite}.cpp" -o "$build_dir/$suite"
    "$build_dir/$suite"
done
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I firmware/src firmware/src/Controller/main.cpp -o "$build_dir/controller"
"$build_dir/controller"
firmware/test/run_hardware_tests.sh
firmware/test/run_web_tests.sh

if "$firmware"; then
    uv run --no-project --with platformio pio run -d firmware \
        -e esp32dev -e esp32dev-max31856
fi
if "$browser"; then
    uv run --no-project --with playwright==1.58.0 python tools/test_browser.py
fi
printf 'All requested software verification checks passed. Physical bench tests are separate.\n'
