"""Application entry point and simulation runner."""

from __future__ import annotations

import sys
import time
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
        print(
            f"[{snapshot.timestamp_s:.1f}s] Pit: {snapshot.pit_temp_f}°F | Set: {snapshot.setpoint_f}°F | "
            f"Demand: {snapshot.demand_pct:.1f}% | Damper: {snapshot.damper_position_pct:.1f}% | "
            f"Blower: {snapshot.blower_speed_pct:.1f}% | State: {snapshot.status}"
        )


def main() -> None:
    """Run interactive simulation."""
    print("ESP32 Smoker Fan & Damper Controller — Simulation Mode")
    sensor = SimulatedSensor(start_pit_f=200.0)
    actuator = ConsoleActuator()
    telemetry = ConsoleTelemetry()

    service = SmokerControlService(
        sensor_port=sensor,
        damper_port=actuator,
        blower_port=actuator,
        telemetry_port=telemetry,
        target_setpoint_f=225.0,
    )

    t = 0.0
    for _ in range(5):
        service.execute_cycle(t)
        t += 1.0


if __name__ == "__main__":
    main()
