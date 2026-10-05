#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/smoker-hardware-tests.XXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread -DARDUINO \
    -I"$repo_root/firmware/test/arduino_spies" \
    "$repo_root/firmware/test/test_hardware.cpp" -o "$build_dir/hardware"
"$build_dir/hardware"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    "$repo_root/firmware/test/test_wireless_concurrency.cpp" -o "$build_dir/wireless"
"$build_dir/wireless"
# Compile the production entrypoint unchanged apart from replacing its HTTPS
# transport adapter include with a spy; all owned services/hardware remain real.
python3 - "$repo_root" "$build_dir/startup_main.cpp" <<'PY'
import pathlib, sys
root = pathlib.Path(sys.argv[1])
source = (root / 'firmware/src/Controller/main.cpp').read_text()
source = source.replace('#include "../Adapters/Network/WebServerAdapter.hpp"', '#include "NetworkBoundary.hpp"')
source = source.replace('#include "../', '#include "' + str(root / 'firmware/src') + '/')
pathlib.Path(sys.argv[2]).write_text(source)
PY
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread -DARDUINO \
    -DSTARTUP_ENTRYPOINT=\""$build_dir/startup_main.cpp"\" \
    -I"$repo_root/firmware/test/arduino_spies" -I"$repo_root/firmware/src" \
    "$repo_root/firmware/test/test_startup.cpp" -o "$build_dir/startup"
"$build_dir/startup"
