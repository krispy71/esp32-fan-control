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
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread -DARDUINO \
    -I"$repo_root/firmware/test/arduino_spies" \
    "$repo_root/firmware/test/test_ble_discovery.cpp" -o "$build_dir/discovery"
"$build_dir/discovery"
python3 "$repo_root/firmware/test/test_ble_sdk_patch.py"
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
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pthread -DARDUINO -DSMOKER_MAX31856 \
    -DSTARTUP_ENTRYPOINT=\""$build_dir/startup_main.cpp"\" \
    -I"$repo_root/firmware/test/arduino_spies" -I"$repo_root/firmware/src" \
    "$repo_root/firmware/test/test_startup_max31856.cpp" -o "$build_dir/startup_max31856"
# Just after a control sample, either side of conversion completion, and just
# before the next sample: physical conversion latency is observable, not hidden.
for disconnect_ms in 1001 1079 1080 1081 1169 1170 1171 1999; do
    "$build_dir/startup_max31856" "$disconnect_ms"
done
