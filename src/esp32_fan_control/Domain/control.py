"""Immutable values exchanged between control and transport tasks."""

from dataclasses import dataclass
from enum import Enum

from esp32_fan_control.Domain.configuration import SmokerConfig
from esp32_fan_control.Domain.telemetry import TelemetrySnapshot


class ControlCommandKind(str, Enum):
    SET_SETPOINT = "set_setpoint"
    UPDATE_CONFIG = "update_config"
    TRIGGER_LID_PAUSE = "trigger_lid_pause"
    CANCEL_LID_PAUSE = "cancel_lid_pause"


@dataclass(frozen=True)
class ControlCommand:
    kind: ControlCommandKind
    setpoint_f: float = 225.0
    config: SmokerConfig | None = None
    request_id: int = 0
    expected_config_version: int | None = None


class PersistenceStatus(str, Enum):
    NOT_CONFIGURED = "not_configured"
    UNCHANGED = "unchanged"
    SAVED = "saved"
    FAILED = "failed"


@dataclass(frozen=True)
class ControlState:
    config: SmokerConfig
    telemetry: TelemetrySnapshot | None
    persistence: PersistenceStatus = PersistenceStatus.NOT_CONFIGURED
    last_command_id: int = 0
    last_command_accepted: bool = False
    config_version: int = 0
