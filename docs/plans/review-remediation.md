# Review remediation: reliable wired-probe control

## North Star

Resolve the October 4 repository review findings while preserving autonomous, offline
ESP32 control with at least one wired K-type pit probe. Trace startup/PWM behavior to
project Gates 1–2, PID recovery to Gate 3, decoded probe faults to Gate 4, and persisted
calibration, safe telemetry, and authenticated controls to Gate 5.

Expected behavior: boot commands calibrated damper closure and fan-off; independent
PWM timers and serialized SPI frames preserve hardware operation; one control task
owns service state; suppressed intervals do not accumulate PID error; configuration
loads after platform startup; copied telemetry outlives its source; web controls require
device-specific credentials and never disclose cloud tokens; servo calibration can be
validated, applied, and persisted. MAX31855 remains the default converter; implement
selectable MAX31856 K-type support rather than claiming an absent driver.

Concepts: owned telemetry snapshot, damper calibration, typed control command, copied
control state, bounded command channel, per-device access credential.

Invariants: inward dependencies, authoritative wired pit source, one-sample fault clamp,
bounded cross-task communication, no network dependency in control, no secret in API
responses or ordinary logs, no direct hardware types crossing application ports.

Constraints: ESP32 Arduino/PlatformIO, 5V hardware/3.3V SPI, existing flash partition,
one optional food converter, existing Python simulator. Physical PWM, probe accuracy,
and smoker +/-2F qualification require a bench; host evidence will not claim them.

Non-goals: CAD redesign, deployment/flashing, cloud product expansion, unrelated UI
redesign. The user explicitly approved retaining the embedded HTML/JavaScript
dashboard as an exception to the prescribed Next.js stack on October 4.

## Architecture plan

Domain: reuse PID, Temperature, SmokerConfig, and DisplayView; add validated damper
calibration and owned telemetry/control messages. Domain must not import Services.
Services: reuse SmokerControlService; explicit initialization, suppression recovery,
calibration application, and command consumption/publication at the control boundary. A service-owned fast
pit-health check inhibits outputs between PID cycles; healthy checks never advance PID.
Ports: reuse sensor/storage/actuator ports; add the related damper calibration operation
and a bounded control-command/state exchange port. No vendor types cross it.
Repositories: none; existing configuration storage port remains sufficient.
Adapters: isolated SPI owner; corrected actuators, MAX converters, NVS, bounded control
exchange; authenticated web boundary with structured parsing and redacted outputs.
The pinned Arduino BLE discovery implementation receives a build-local ownership
repair, checked against the vendor source checksum and executable allocation tests.
No shared SDK files are modified; the application spine remains unchanged.
Controller: construct application ownership after Arduino initialization; wire ports and
schedule control/network tasks, without mutable namespace-level service dependencies.
Client UI: authenticated requests and calibration form in the existing embedded assets.
Tests: production C++ domain/service regression, Arduino driver spies and startup
harness, real simulator HTTP/browser tests, strict host compile and ESP32 build.

## Steps and ownership

1. **Core and application contracts** (independent; blocks integration of steps 2–3).
   Own firmware Domain, Services, test_domain.cpp, a new test_control.cpp, and Python
   Domain/Services plus their tests. Move telemetry value ownership into Domain;
   retain a Ports alias if needed for callers. Add command/state contracts, explicit
   initialize(), calibration, and paused-PID recovery. Tests must kill the previously
   surviving shutdown/range/fault/sign mutations. Step Review: Correctness, Test,
   Architecture.
2. **Hardware and composition** (implements against step 1 contracts).
   Own actuator, sensor, storage, display and runtime adapters; Controller/main.cpp;
   PlatformIO; Arduino-spy tests. Fix timers, first pulse, SPI including MOSI, NVS
   persistence, and display host warning. Implement selectable MAX31856. Compose
   the bounded command channel and place servo housekeeping in the owning task.
   Step Review: Correctness, Test, Architecture.
3. **Authenticated control and calibration UI** (depends on step 1).
   Own firmware network adapter, Python web/config adapters and CLI, dashboard assets,
   HTTP/security/browser tests. No direct cross-task service references. Device AP
   credentials must be provisioned rather than committed; authentication and origin
   checks cover mutations; tokens are write-only. Step Review: Correctness, Test,
   Security, plus Architecture if introducing shared mechanisms.
4. **Integration and verification** (depends on all steps).
   Integrate branches, reconcile seams, update docs and run independent Correctness,
   Architecture, Test, Security review. Resolve new blocking findings and verify all
   software acceptance scenarios; retain explicit bench-only limitations.

Parallel steps use separate worktrees/branches and dedicated scratch paths. The
coordinator owns this plan, final documentation, review scheduling and integration.

## Shared contracts (owned by step 1)

- `Domain::DamperCalibration`: `min_pulse_us`, `max_pulse_us`, `inverted`, validation
  within 500–2500 us with min < max.
- `SmokerConfig`: `servo_min_pulse_us`, `servo_max_pulse_us`, `servo_inverted`.
- `IDamperActuatorPort::configure(const Domain::DamperCalibration&)`.
- Domain-owned `TelemetrySnapshot`, with owned fixed-size strings, accessible through
  the existing Ports name for compatibility.
- `Domain::ControlCommand`: typed setpoint/configuration/lid-pause operations.
- `Domain::ControlState`: copied configuration plus telemetry and persistence status.
- `Services::Ports::IControlChannel`: bounded submit/receive, publish/snapshot by value.
- `SmokerControlService::initialize()` after platform initialization; optional injected
  control channel consumed before each cycle and published afterward. Web never calls
  the mutable service directly. Detailed signatures are to be frozen by step 1 before
  downstream integration.

## Verification plan

North Star behaviors: calibrated safe boot, correct PWM mapping, uninterrupted wired
pit acquisition, fault shutdown, PID recovery, persistent settings, authenticated UI.
Entry points: actual firmware setup/control task via an Arduino boundary harness;
real simulator HTTP/CLI and browser.
External dependencies: GPIO/PWM/SPI/NVS, clock, network, radio/cloud.
Mocks: hardware register/function spies; injected fault frames and device timing.
Fakes: stateful NVS and SPI peripheral boundaries; no fake internal service/client API.
Real integrations: C++ compiler, Arduino framework build, filesystem configuration,
local HTTP server and browser against that server.
Test data: healthy/negative/boundary/out-of-range raw frames, each short/open fault,
missing food probe, active outputs followed by pit failure, 180-second suppression,
valid/invalid calibration, correct/missing/wrong auth, cross-origin requests.
Scenarios: every software criterion above must have executable assertions and negative
cases. Persistence must cross a fresh instance; telemetry must survive stack reuse;
SPI must reject overlapping frame ownership; network failure must not stop control.
Driver: repository test scripts/harnesses with clear exit status and repeatable setup.
Evidence: exact commands/results and acceptance matrix in the completion report.

## Status

Software implementation complete. Independent integration review: PASS, no remaining
findings. All executable software acceptance scenarios pass, including both ESP32
builds and the real simulator/browser flow. Physical Gates 1–5 remain bench work;
full-project verification is PARTIALLY VERIFIED. See the
[completion and verification report](../review-verification.md).
