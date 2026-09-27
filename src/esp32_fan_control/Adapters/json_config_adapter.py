"""JSON file storage adapter for non-volatile configuration persistence."""

from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any
from esp32_fan_control.Domain.configuration import SmokerConfig
from esp32_fan_control.Services.Ports.config_storage_port import ConfigStoragePort

logger = logging.getLogger(__name__)


class JsonConfigAdapter(ConfigStoragePort):
    """
    Persists smoker configuration to a local JSON file.
    Uses atomic writes to ensure data integrity during unexpected shutdowns.
    """

    def __init__(self, config_file: Path | str = "smoker_config.json") -> None:
        self._path = Path(config_file).resolve()

    @property
    def path(self) -> Path:
        return self._path

    def load_config(self) -> SmokerConfig | None:
        """Load stored configuration from file, or return None if missing or corrupt."""
        if not self._path.is_file():
            return None

        try:
            raw_text = self._path.read_text(encoding="utf-8")
            data = json.loads(raw_text)
            return SmokerConfig(
                setpoint_f=float(data.get("setpoint_f", 225.0)),
                pid_kp=float(data.get("pid_kp", 3.0)),
                pid_ki=float(data.get("pid_ki", 0.02)),
                pid_kd=float(data.get("pid_kd", 15.0)),
                airflow_threshold_pct=float(data.get("airflow_threshold_pct", 40.0)),
                lid_drop_threshold_deg=float(data.get("lid_drop_threshold_deg", 15.0)),
                lid_pause_duration_s=float(data.get("lid_pause_duration_s", 180.0)),
            )
        except Exception as err:
            logger.warning("Failed to load configuration from %s: %s", self._path, err)
            return None

    def save_config(self, config: SmokerConfig) -> bool:
        """Atomically persist configuration to file."""
        try:
            data: dict[str, Any] = {
                "setpoint_f": config.setpoint_f,
                "pid_kp": config.pid_kp,
                "pid_ki": config.pid_ki,
                "pid_kd": config.pid_kd,
                "airflow_threshold_pct": config.airflow_threshold_pct,
                "lid_drop_threshold_deg": config.lid_drop_threshold_deg,
                "lid_pause_duration_s": config.lid_pause_duration_s,
            }
            raw_json = json.dumps(data, indent=2)

            self._path.parent.mkdir(parents=True, exist_ok=True)
            tmp_path = self._path.with_suffix(".tmp")
            tmp_path.write_text(raw_json, encoding="utf-8")
            tmp_path.replace(self._path)
            return True
        except Exception as err:
            logger.error("Failed to save configuration to %s: %s", self._path, err)
            return False
