"""Durable revision reservations across actual storage and runtime restarts."""
from dataclasses import asdict, replace
import json
from pathlib import Path
from unittest.mock import patch

import pytest

from esp32_fan_control.Adapters.control_channel_adapter import ThreadedControlChannel
from esp32_fan_control.Adapters.json_config_adapter import JsonConfigAdapter
from esp32_fan_control.Adapters.web_server_adapter import WebServerAdapter
from esp32_fan_control.Controller.cli import ConsoleActuator, SimulatedSensor
from esp32_fan_control.Domain.configuration import SmokerConfig
from esp32_fan_control.Domain.control import PersistenceStatus
from esp32_fan_control.Services.smoker_control_service import SmokerControlService


class StorageBoundary:
    def __init__(self, path):
        self.actual = JsonConfigAdapter(path)
        self.fail = False

    def load_config(self):
        return self.actual.load_config()

    def save_config(self, config):
        return False if self.fail else self.actual.save_config(config)


def runtime(storage, config=None):
    channel = ThreadedControlChannel()
    actuator = ConsoleActuator()
    service = SmokerControlService(SimulatedSensor(), actuator, actuator,
        config_storage=storage, control_channel=channel, config=config)
    service.initialize()
    return service, channel, WebServerAdapter(channel)


def apply(service, web, version, minimum):
    status, body = web._submit('/api/config', {
        'config_version': version, 'servo_min_pulse_us': minimum})
    if status == 202:
        service.execute_cycle(0)
    return status, body


@pytest.mark.parametrize('fail_latest_save', [False, True])
def test_old_form_cannot_overwrite_calibration_after_reboot(tmp_path, fail_latest_save):
    storage = StorageBoundary(tmp_path / 'config.json')
    service, channel, web = runtime(storage)
    old_form = web._config()
    assert 'next_config_version' not in old_form
    assert apply(service, web, old_form['config_version'], 700)[0] == 202
    assert channel.snapshot().last_command_accepted
    storage.fail = fail_latest_save
    assert apply(service, web, channel.snapshot().config_version, 800)[0] == 202
    before_restart = channel.snapshot()
    assert before_restart.config.servo_min_pulse_us == 800
    assert before_restart.persistence == (PersistenceStatus.FAILED if fail_latest_save else PersistenceStatus.SAVED)

    storage.fail = False
    restarted, after_channel, after_web = runtime(storage)
    expected_minimum = 700 if fail_latest_save else 800
    assert restarted.config.servo_min_pulse_us == expected_minimum
    for stale_version in (old_form['config_version'], before_restart.config_version):
        assert apply(restarted, after_web, stale_version, 1000)[0] == 409
        assert storage.load_config().servo_min_pulse_us == expected_minimum
    assert after_channel.snapshot().config_version > before_restart.config_version
    assert apply(restarted, after_web, after_channel.snapshot().config_version, 900)[0] == 202
    assert storage.load_config().servo_min_pulse_us == 900


def test_failed_reservation_rejects_change_then_recovers_without_stopping_control(tmp_path):
    storage = StorageBoundary(tmp_path / 'config.json')
    assert storage.save_config(SmokerConfig(servo_min_pulse_us=700, next_config_version=1024))
    storage.fail = True
    service, channel, web = runtime(storage)
    assert channel.snapshot().persistence == PersistenceStatus.FAILED
    assert apply(service, web, channel.snapshot().config_version, 800)[0] == 202
    state = channel.snapshot()
    assert not state.last_command_accepted
    assert state.config.servo_min_pulse_us == 700
    assert state.persistence == PersistenceStatus.FAILED
    assert state.telemetry.status == 'REGULATING'
    storage.fail = False
    assert apply(service, web, state.config_version, 800)[0] == 202
    assert channel.snapshot().last_command_accepted
    assert storage.load_config().servo_min_pulse_us == 800
    assert storage.load_config().next_config_version > channel.snapshot().config_version


def test_legacy_json_defaults_and_migrates_reservation(tmp_path):
    path = tmp_path / 'legacy.json'
    data = asdict(SmokerConfig(setpoint_f=275, servo_min_pulse_us=750, servo_inverted=True))
    data.pop('next_config_version')
    path.write_text(json.dumps(data))
    storage = JsonConfigAdapter(path)
    assert storage.load_config().next_config_version == 0
    first, first_channel, _ = runtime(storage)
    assert first.config.setpoint_f == 275 and first.config.servo_inverted
    assert storage.load_config().servo_min_pulse_us == 750
    assert json.loads(path.read_text())['next_config_version'] == 1024
    restarted, channel, _ = runtime(JsonConfigAdapter(path))
    assert channel.snapshot().config_version > first_channel.snapshot().config_version
    assert restarted.config.servo_inverted and restarted.config.servo_min_pulse_us == 750


def test_explicit_config_keeps_existing_durable_high_water_mark(tmp_path):
    storage = JsonConfigAdapter(tmp_path / 'explicit.json')
    assert storage.save_config(SmokerConfig(next_config_version=4096))
    service, channel, _ = runtime(storage, SmokerConfig(setpoint_f=275))
    assert channel.snapshot().config_version == 4096
    assert service.config.setpoint_f == 275
    assert storage.load_config().next_config_version == 5120


def test_reservation_exhaustion_never_wraps(tmp_path):
    storage = JsonConfigAdapter(tmp_path / 'exhausted.json')
    assert storage.save_config(SmokerConfig(next_config_version=0xFFFFFFFE))
    service, channel, web = runtime(storage)
    assert apply(service, web, channel.snapshot().config_version, 800)[0] == 202
    state = channel.snapshot()
    assert not state.last_command_accepted and state.persistence == PersistenceStatus.FAILED
    assert state.config_version == 0xFFFFFFFE
    assert storage.load_config().next_config_version == 0xFFFFFFFE
    assert state.config.servo_min_pulse_us == 1000


def test_next_block_is_reserved_before_current_one_is_exhausted(tmp_path):
    storage = JsonConfigAdapter(tmp_path / 'blocks.json')
    service, channel, _ = runtime(storage)
    for step in range(1024):
        assert service.update_config(replace(service.config, pid_kp=step % 10))
    service.execute_cycle(0)
    state = channel.snapshot()
    assert state.config_version == 1024
    assert storage.load_config().next_config_version > state.config_version
    _, next_channel, _ = runtime(storage)
    assert next_channel.snapshot().config_version > state.config_version


@pytest.mark.parametrize('invalid', [-1, 0x100000000, True, 1.5, '1'])
def test_invalid_durable_reservation_is_rejected(invalid):
    with pytest.raises(ValueError):
        SmokerConfig(next_config_version=invalid)


def test_transient_read_failure_cannot_overwrite_saved_calibration(tmp_path):
    path = tmp_path / 'unreadable.json'
    storage = JsonConfigAdapter(path)
    assert storage.save_config(SmokerConfig(servo_min_pulse_us=700, next_config_version=4096))
    previous = path.read_bytes()
    with patch.object(Path, 'open', side_effect=OSError('read unavailable')):
        service, channel, _ = runtime(storage)
        assert channel.snapshot().persistence == PersistenceStatus.FAILED
        assert not service.update_config(replace(service.config, servo_min_pulse_us=900))
    assert path.read_bytes() == previous
    fresh, fresh_channel, _ = runtime(JsonConfigAdapter(path))
    assert fresh.config.servo_min_pulse_us == 700
    assert fresh_channel.snapshot().config_version == 4096
