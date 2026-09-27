"""Application entry point and simulation runner."""

from __future__ import annotations

import argparse
import sys
import time
from esp32_fan_control.Adapters.web_server_adapter import WebServerAdapter
from esp32_fan_control.Domain.temperature import SensorFault, SensorRole, TemperatureReading
from esp32_fan_control.Services.Ports.actuator_ports import BlowerActuatorPort, DamperActuatorPort
from esp32_fan_control.Services.Ports.sensor_port import TemperatureSensorPort
from esp32_fan_control.Services.Ports.telemetry_port import TelemetryPublisherPort, TelemetrySnapshot
from esp32_fan_control.Services.smoker_control_service import SmokerControlService


class ConsoleActuator(DamperActuatorPort, BlowerActuatorPort):
    def __init__(self) -> None:
        self.damper_pct: float = 0.0
        self.blower_pct: float = 0.0

    def set_position(self, position_pct: float) -> None:
        self.damper_pct = position_pct

    def set_speed(self, speed_pct: float) -> None:
        self.blower_pct = speed_pct


class SimulatedSensor(TemperatureSensorPort):
    def __init__(self, start_pit_f: float = 200.0, start_food_f: float = 65.0) -> None:
        self.pit_f = start_pit_f
        self.food_f = start_food_f
        self.fault = SensorFault.OK

    def read_temperature(self, role: SensorRole) -> TemperatureReading:
        now = time.time()
        if role == SensorRole.PIT:
            return TemperatureReading.from_fahrenheit(self.pit_f, role, now, fault=self.fault)
        return TemperatureReading.from_fahrenheit(self.food_f, role, now, fault=SensorFault.OK)


class ConsoleTelemetry(TelemetryPublisherPort):
    def publish(self, snapshot: TelemetrySnapshot) -> None:
        pit_str = f"{snapshot.pit_temp_f:.1f}°F" if snapshot.pit_temp_f is not None else "FAULT"
        meat_str = f"{snapshot.meat_temp_f:.1f}°F" if snapshot.meat_temp_f is not None else "--.-°F"
        print(
            f"[{snapshot.timestamp_s:.1f}s] Pit: {pit_str} | Set: {snapshot.setpoint_f:.1f}°F | "
            f"Meat: {meat_str} | Damper: {snapshot.damper_position_pct:.0f}% | "
            f"Fan: {snapshot.blower_speed_pct:.0f}% | {snapshot.status}"
        )


def main() -> None:
    """Run interactive simulation or web-enabled server."""
    parser = argparse.ArgumentParser(description="ESP32 Smoker Controller Simulation & Web Server")
    parser.add_argument("--web", action="store_true", help="Start local web dashboard")
    parser.add_argument("--host", default="127.0.0.1", help="Web server host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Web server port (default: 8080)")
    parser.add_argument("--setpoint", type=float, default=225.0, help="Initial target setpoint °F")
    parser.add_argument("--cycles", type=int, default=0, help="Number of cycles to run (0 = infinite)")
    args = parser.parse_args()

    print("=======================================================")
    print("  ESP32 Smoker Fan & Damper Controller — Python Runner")
    print("=======================================================")

    sensor = SimulatedSensor(start_pit_f=185.0, start_food_f=68.0)
    actuator = ConsoleActuator()
    telemetry = ConsoleTelemetry()

    service = SmokerControlService(
        sensor_port=sensor,
        damper_port=actuator,
        blower_port=actuator,
        telemetry_port=telemetry,
        target_setpoint_f=args.setpoint,
    )

    web_server = None
    if args.web:
        web_server = WebServerAdapter(service=service, host=args.host, port=args.port)
        web_server.start(background=True)
        print(f"[Web] Dashboard live at: http://{args.host}:{web_server.port}")
        print("[Web] Serving real-time telemetry, controls, and canvas trend graph.")

    print("\nStarting control loop (Press Ctrl+C to exit)...")
    t = 0.0
    cycles_run = 0
    try:
        while True:
            # Simple physics simulation:
            # - Heat loss to ambient (70°F)
            # - Heat gain proportional to damper opening + forced fan draft
            ambient_f = 70.0
            cooling = (sensor.pit_f - ambient_f) * 0.015
            heating = (actuator.damper_pct * 0.03) + (actuator.blower_pct * 0.08)
            sensor.pit_f = max(ambient_f, sensor.pit_f - cooling + heating)

            # Meat warms slowly towards pit temp
            if sensor.food_f < sensor.pit_f:
                sensor.food_f += (sensor.pit_f - sensor.food_f) * 0.001

            service.execute_cycle(t)
            t += 1.0
            cycles_run += 1

            if not args.web and args.cycles == 0 and cycles_run >= 5:
                break
            if args.cycles > 0 and cycles_run >= args.cycles:
                break

            time.sleep(1.0 if args.web else 0.01)
    except KeyboardInterrupt:
        print("\nStopping controller...")
    finally:
        if web_server:
            web_server.stop()
            print("[Web] Server stopped.")


if __name__ == "__main__":
    main()
