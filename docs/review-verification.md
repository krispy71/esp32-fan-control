# Review remediation verification

Feature: reliable wired K-type control and protected calibration.
Plan: [review remediation](plans/review-remediation.md).

Entry points: production firmware `setup()` and control/network tasks; the real
firmware HTTPS dispatch handler; the Python simulator CLI and its HTTPS dashboard.
Environment: Linux host, C++17, pinned PlatformIO ESP32 framework, Chromium.
External boundaries: SPI/PWM/GPIO/NVS and RTOS spies; real JSON/crypto libraries;
real local TLS sockets, filesystem, simulator process, browser, and accessibility engine.
Driver: `tools/verify.sh --firmware --browser`.

## Acceptance matrix

| Scenario and input | Expected state/output and failure behavior | Evidence | Result |
| --- | --- | --- | --- |
| Boot with persisted inverted calibration | Fan off before initialization; saved limits applied to first closure pulse; construction performs no platform I/O | `test_startup.cpp`, `test_hardware.cpp` | PASS |
| Demand from zero through forced airflow | Closed/off at zero; fan stays off below threshold; preserve configured minimum fan duty | `test_domain.cpp`, `test_control.cpp`, Python control tests | PASS |
| One MAX31855K pit probe, no food converter | Valid pit controls normally; missing optional food does not inhibit control | `test_control.cpp`, `test_hardware.cpp`, startup harness | PASS |
| Signed/boundary temperatures and converter faults | Correct decode; invalid pit clamps previously active fan and damper, including between PID cycles | `test_control.cpp`, hardware tests and Python tests | PASS |
| Lid pause/fault followed by recovery | No integration over inhibited time; resume with retained integral and valid timing | `test_control.cpp`, `test_control_contracts.py` | PASS |
| Copied probe name after source stack reuse | Telemetry and display retain owned strings | `test_control.cpp`; sanitizers | PASS |
| Concurrent sensor/display and wireless traffic | Complete SPI frame ownership and copied wireless state | `test_hardware.cpp`, `test_wireless_concurrency.cpp` | PASS |
| Bounded/stale control commands | Control owner applies queued command; full queue rejects; stale version preserves current settings | Control and HTTP tests | PASS |
| Missing/wrong auth, untrusted Host/Origin, private file path, malformed/oversize JSON | Requests rejected without settings mutation or token disclosure | `test_web_api.py`, production `test_web_security.cpp` | PASS |
| Valid calibration, invalid limits, token preservation/removal | Applied/saved feedback; invalid settings leave storage intact; token never echoed | Real HTTPS tests and `tools/test_browser.py` | PASS |
| Restart or filesystem/NVS save failure | Fresh instance loads settings; stale forms remain rejected after reboot; failed persistence distinguished from applied setting | Storage tests, HTTPS tests and browser process restart | PASS |
| Keyboard/mobile/light and dark themes | Reachable controls, no 360px horizontal overflow, no tested WCAG A/AA axe violations | Real Chromium/axe browser driver | PASS |
| Slow or blocked TLS handshake/read/write; failed connection setup | Fixed connection slots and absolute deadlines; TLS context and socket released exactly once | `test_https_transport.cpp`, real Python HTTPS deadline tests | PASS |
| Repeated BLE advertisements; change an incorrect probe MAC | Bounded candidate retention and fresh service discovery after reconnect | `test_ble_discovery.cpp` | PASS |
| 200 BLE discoveries with 64 instances of each of three service UUIDs | Exactly three owned services retained; both indexes cleared between rounds; zero live services after destruction | `test_ble_sdk_patch.py`, emitted Arduino source bodies | PASS |

## Original finding coverage

| Finding | Implemented correction |
| --- | --- |
| Fan/servo shared LEDC timer | Fan channel 0, servo channel 2; timer isolation and pulse assertions |
| Missing boot closure pulse | Explicit initialized state and calibrated first drive |
| Unserialized SPI and absent display MOSI | Injected shared bus; CS/transaction lifetime under one lock; MOSI23 enabled |
| Cross-task service mutation | Bounded typed command queue and copied state; exclusive control owner |
| NVS access in global construction | Side-effect-free construction and explicit initialization after platform startup |
| PID windup across suppression | Suspend integration/time history through fault and lid intervals |
| Dangling telemetry probe name | Domain-owned fixed-size string storage and copied snapshots |
| Shared credentials, unauthenticated commands, exposed tokens | Per-device provisioned HTTPS; authenticated routes; write-only token; offline CA key |
| Missing persisted servo calibration | Validated domain limits, actuator port, complete storage record and dashboard controls |
| Domain importing a Services port | Domain owns telemetry; port retains compatibility alias |
| MAX31856 advertised without driver | Selectable K-type driver, nonblocking conversion, fault/readiness/freshness checks |
| Weak fault tests and strict-build failure | Active-output regression/mutations, decoder boundaries, actual Arduino paths and warning-free host checks |

Review follow-ups additionally preserve minimum blower speed on configuration updates,
refresh the servo timeout clock after acquisition, reject bad converter register readback,
bound slow HTTPS requests, keep malformed provisioning from stopping simulator
control, and reserve configuration revisions durably across reboots and failed saves.
Additional transport/browser assertions cover successful setpoint, pause/resume,
changing telemetry, binary TLS forwarding, and partial/failed response writes.
BLE discovery uses one explicit scanner owner and bounded advertisement retention;
reconnects refresh the SDK's cached services. Failed HTTPS setup leaves socket closure
to the SDK accept caller, while established sessions close their own socket.
An SDK-source-checked build patch repairs Arduino's duplicate-service allocation leak
and stale secondary index without modifying the installed framework. Reused HTTPS
slots also exercise allocation/handshake failure followed by a healthy connection.

## Physical gates

Software evidence covers the control and transport behavior of the remediation.
It does not qualify the full physical [project North Star](north-star.md).
No serial ESP32 device was attached during this work.

- Gate 1: timer allocation and pulses verified at the driver boundary; actual 25 kHz
  fan PWM, 50 Hz servo signal, RJ45 wiring, jitter, power integrity and movement need
  bench measurements.
- Gate 2: demand mapping and command ordering verified in software; actual aperture
  and blower response remain unmeasured.
- Gate 3: suppression and recovery verified in software; ±2°F smoker regulation is
  untested.
- Gate 4: every decoded pit fault clamps active outputs in the same control cycle.
  The boundary model verifies eight MAX31856 disconnect phases with clamp
  79–181 ms after disconnect; MAX31855 fault checking also runs between PID cycles.
  Actual unplug-to-shutdown latency, including electrical conversion behavior and
  task scheduling, still needs a device timing test.
- Gate 5: real simulator HTTPS/browser and fresh-instance storage are tested; actual
  ESP32 Wi-Fi/TLS behavior and power-cycle persistence need bench confirmation.

The optional MEATER cloud adapter now requires a supplied trusted CA and station
connectivity. The shipped composition leaves it disabled; a stored token alone does
not establish connectivity. Wired control remains independent of cloud access.

## Completion audit

Domain: owns calibration invariants, PID suppression and value-owned telemetry/control
messages; imports no Ports or hardware dependencies.

Services: explicit startup and one control owner; calibration, fault shutdown,
configuration application and command consumption depend only on Ports.

Ports: narrow sensor, actuator, storage, telemetry and bounded command/state contracts;
no vendor types cross inward.

Repositories: no new repository abstraction; existing configuration storage remains
sufficient.

Adapters: own PWM/SPI/NVS/RTOS/crypto/network behavior and external failure translation.

Controller: constructs ownership after Arduino startup, wires dependencies and schedules
tasks. Network tasks consume copied state and do not mutate service state.

Tests: production control regressions, hardware/startup spies, mutation checks, real
TLS API and browser drivers, strict compiles, and both ESP32 converter builds.

Architecture notes: Domain → Services/Ports stays shallow with inward dependencies;
no global mutable service dependencies or service locator. The user approved retaining
the embedded dashboard as the documented exception to the Next.js client requirement.

## Review and execution results

Software verification on October 4, 2026, at code commit `0165497`:

- `tools/verify.sh --firmware --browser`: exit 0; 120 Python tests, all 13 C++
  domain suites, control regressions, strict native entrypoint, hardware/startup and
  wireless tests, production HTTPS handler/transport tests, both ESP32 builds, and
  the complete real simulator/browser flow passed.
- AddressSanitizer and UndefinedBehaviorSanitizer: domain/control suites passed.
- Targeted mutation checks rejected regressions in calibrated first pulse, baseline
  scalar NVS migration, fault shutdown/timing, MAX31856 bus mode/readback, setpoint,
  pause/resume, telemetry updates, TLS send/read forwarding, reused socket ownership,
  BLE peer-service refresh, duplicate allocation, and secondary-index cleanup.
- The default build uses 64,208 bytes of RAM and 1,835,841 bytes of flash;
  MAX31856 uses 64,320 bytes and 1,836,761 bytes. Both fit the existing partition
  (19.6% RAM, 70.0–70.1% application flash). No device was flashed.

Independent Correctness, Architecture, Test, and Security reviewers reported no
remaining findings through `0165497`, each with high confidence. Findings were
repaired and re-reviewed, including the final socket-reuse and vendor BLE ownership
regressions. Coordinator's consolidated code-review result: **PASS**.

North Star criteria: all software scenarios above PASS. Physical Gates 1–5 remain
NOT TESTED as qualified in the physical-gates section. The remaining work is bench
qualification; no code-review findings are intentionally deferred.

Final verification result: **PARTIALLY VERIFIED** against the full project North Star,
with all remediation software scenarios passing and physical qualification pending.
