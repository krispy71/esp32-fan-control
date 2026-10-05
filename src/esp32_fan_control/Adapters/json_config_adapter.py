"""Private atomic JSON configuration storage, including calibrated actuator limits."""
from __future__ import annotations

from dataclasses import asdict
import json
import logging
import os
from pathlib import Path
import tempfile

from esp32_fan_control.Domain.configuration import MeatProbeMode, SmokerConfig
from esp32_fan_control.Services.Ports.config_storage_port import ConfigStoragePort

logger = logging.getLogger(__name__)


class JsonConfigAdapter(ConfigStoragePort):
    def __init__(self, config_file: Path | str = "smoker_config.json") -> None:
        self._path = Path(config_file).resolve()
        self._write_blocked = False

    @property
    def path(self) -> Path:
        return self._path

    def load_config(self) -> SmokerConfig | None:
        # A missing first-use file permits reservation; unread/corrupt existing
        # data must never be replaced by startup defaults after a read failure.
        self._write_blocked = True
        try:
            if not self._path.is_file():
                self._write_blocked = False
                return None
            with self._path.open("rb") as source:
                raw = source.read(4097)
            if len(raw) > 4096:
                raise ValueError("Oversized configuration")
            data = json.loads(raw)
            if not isinstance(data, dict):
                raise ValueError("Invalid configuration")
            if "meat_probe_mode" in data:
                data["meat_probe_mode"] = MeatProbeMode(data["meat_probe_mode"])
            # Legacy JSON has no high-water mark; the domain defaults it to zero,
            # and initialization durably reserves revisions before mutations.
            config = SmokerConfig(**data)
            self._write_blocked = False
            return config
        except (OSError, ValueError, TypeError, RecursionError):
            logger.warning("Stored configuration unavailable or invalid; using safe defaults")
            return None

    def save_config(self, config: SmokerConfig) -> bool:
        if self._write_blocked:
            return False
        temporary = None
        try:
            data = asdict(config)
            data["meat_probe_mode"] = config.meat_probe_mode.value
            raw = json.dumps(data, allow_nan=False, indent=2)
            self._path.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=self._path.parent,
                                             prefix=".smoker-", delete=False) as output:
                temporary = Path(output.name)
                os.chmod(temporary, 0o600)
                output.write(raw)
                output.flush()
                os.fsync(output.fileno())
            temporary.replace(self._path)
            directory = os.open(self._path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
            return True
        except (OSError, ValueError, TypeError):
            logger.error("Configuration could not be persisted")
            return False
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
