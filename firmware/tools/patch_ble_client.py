"""Build-local ownership repair for Arduino ESP32 2.0.17 BLE discovery.

Never change the shared framework installation. Refuse unfamiliar vendor sources
so an SDK upgrade requires checking whether these repairs are still necessary.
"""

import hashlib
from pathlib import Path


UPSTREAM_SHA256 = "b03c1b9f94c3da6282d116eff77ed8d2e3f3c732e84501c6512533e2858b3a37"


def patch_source(original: bytes) -> str:
    if hashlib.sha256(original).hexdigest() != UPSTREAM_SHA256:
        raise RuntimeError("BLEClient.cpp differs from pinned Arduino 2.0.17; review the ownership patch before building")
    source = original.decode("utf-8")
    source = source.replace(
        "\tm_servicesMap.clear();\n\tm_haveServices = false;",
        "\tm_servicesMap.clear();\n\tm_servicesMapByInstID.clear();\n\tm_haveServices = false;",
        1,
    )
    source = source.replace(
        "\t\t\tBLEUUID uuid = BLEUUID(evtParam->search_res.srvc_id);",
        "\t\t\tBLEUUID uuid = BLEUUID(evtParam->search_res.srvc_id);\n"
        "\t\t\t// The UUID-keyed API exposes only the first instance.\n"
        "\t\t\t// Do not allocate an unowned duplicate service.\n"
        "\t\t\tif (m_servicesMap.count(uuid.toString())) break;",
        1,
    )
    return source


def install(build_env):
    framework = Path(build_env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    original = framework / "libraries/BLE/src/BLEClient.cpp"
    replacement = Path(build_env.subst("$BUILD_DIR")) / "patched_ble/BLEClient.cpp"
    source = patch_source(original.read_bytes())
    replacement.parent.mkdir(parents=True, exist_ok=True)
    if not replacement.exists() or replacement.read_text() != source:
        replacement.write_text(source)

    def use_owned_discovery(env, node):
        if Path(node.srcnode().get_abspath()).resolve() != original.resolve():
            return node
        return env.File(str(replacement))

    build_env.AddBuildMiddleware(use_owned_discovery, "*/BLE/src/BLEClient.cpp")


# SCons provides Import; ordinary imports expose patch_source for host verification.
try:
    Import
except NameError:
    pass
else:
    Import("env")
    install(env)
