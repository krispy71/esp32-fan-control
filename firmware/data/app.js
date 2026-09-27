/**
 * ESP32 Smoker Controller — Web Dashboard Client Application
 * Zero external dependencies: offline-first, real-time telemetry streaming,
 * HTML5 canvas trend graphing, and dual-actuator airflow visualization.
 */

(function () {
  'use strict';

  // --- Configuration & State ---
  const MAX_HISTORY_POINTS = 300; // 5 minutes at 1Hz
  const telemetryHistory = [];
  let isLidPaused = false;
  let connectionState = 'connecting'; // 'connected' | 'connecting' | 'disconnected'
  let pollingInterval = null;
  let eventSource = null;

  // --- DOM Elements ---
  const elConnectionBadge = document.getElementById('connection-badge');
  const elSystemState = document.getElementById('system-state');
  const elPitTemp = document.getElementById('pit-temp');
  const elPitDelta = document.getElementById('pit-delta');
  const elCurrentSetpoint = document.getElementById('current-setpoint');
  const elPitStatusDot = document.getElementById('pit-status-dot');
  const elMeatTemp = document.getElementById('meat-temp');
  const elMeatStatus = document.getElementById('meat-status');
  const elFoodStatusDot = document.getElementById('food-status-dot');
  const elFoodSourceBadge = document.getElementById('food-source-badge');
  const elFoodBatteryWrap = document.getElementById('food-battery-wrap');
  const elFoodBattery = document.getElementById('food-battery');
  const elAirflowModeBadge = document.getElementById('airflow-mode-badge');
  const elDamperVal = document.getElementById('damper-val');
  const elDamperBar = document.getElementById('damper-bar');
  const elDamperProgress = document.getElementById('damper-progress');
  const elBlowerVal = document.getElementById('blower-val');
  const elBlowerBar = document.getElementById('blower-bar');
  const elBlowerProgress = document.getElementById('blower-progress');
  const elFanRpmStatus = document.getElementById('fan-rpm-status');
  const elSetpointSlider = document.getElementById('setpoint-slider');
  const elSetpointInput = document.getElementById('setpoint-input');
  const elLidPauseBtn = document.getElementById('lid-pause-btn');
  const elFooterUptime = document.getElementById('footer-uptime');
  const canvas = document.getElementById('trendChart');
  const ctx = canvas ? canvas.getContext('2d') : null;

  // --- Formatting Helpers ---
  function formatF(val) {
    if (val === null || val === undefined || isNaN(val)) return '---.-';
    return Number(val).toFixed(1);
  }

  function formatUptime(secondsOrMs) {
    const totalSec = secondsOrMs > 100000 ? Math.floor(secondsOrMs / 1000) : Math.floor(secondsOrMs);
    const hrs = Math.floor(totalSec / 3600);
    const mins = Math.floor((totalSec % 3600) / 60);
    const secs = totalSec % 60;
    return `Uptime: ${String(hrs).padStart(2, '0')}:${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
  }

  // --- Connection State UI ---
  function setConnectionStatus(status) {
    connectionState = status;
    if (!elConnectionBadge) return;

    elConnectionBadge.className = 'badge';
    if (status === 'connected') {
      elConnectionBadge.classList.add('badge-online');
      elConnectionBadge.textContent = 'Connected';
    } else if (status === 'connecting') {
      elConnectionBadge.classList.add('badge-warning');
      elConnectionBadge.textContent = 'Connecting...';
    } else {
      elConnectionBadge.classList.add('badge-danger');
      elConnectionBadge.textContent = 'Offline';
    }
  }

  // --- Real-Time Telemetry Processing ---
  function handleTelemetry(snapshot) {
    setConnectionStatus('connected');

    const pitValid = snapshot.is_pit_valid !== false && snapshot.pit_temp_f !== null && snapshot.pit_temp_f !== undefined;
    const meatValid = snapshot.is_meat_valid !== false && snapshot.meat_temp_f !== null && snapshot.meat_temp_f !== undefined;
    const pitTemp = pitValid ? Number(snapshot.pit_temp_f) : null;
    const meatTemp = meatValid ? Number(snapshot.meat_temp_f) : null;
    const setpoint = Number(snapshot.setpoint_f || 225.0);
    const damperPct = Math.round(Number(snapshot.damper_position_pct || 0));
    const blowerPct = Math.round(Number(snapshot.blower_speed_pct || 0));
    const demandPct = Math.round(Number(snapshot.demand_pct || 0));
    const lidOpen = Boolean(snapshot.lid_open);
    const statusMsg = snapshot.status || 'ACTIVE';

    // 1. Pit Readout
    if (elPitTemp) elPitTemp.textContent = formatF(pitTemp);
    if (elCurrentSetpoint) elCurrentSetpoint.innerHTML = `${formatF(setpoint)}&deg;F`;
    if (elPitDelta) {
      if (pitValid) {
        const delta = pitTemp - setpoint;
        const sign = delta >= 0 ? '+' : '';
        elPitDelta.innerHTML = `${sign}${formatF(delta)}&deg;F`;
        elPitDelta.style.color = Math.abs(delta) <= 3.0 ? '#34d399' : (delta > 0 ? '#f87171' : '#60a5fa');
      } else {
        elPitDelta.innerHTML = '--.-&deg;F';
        elPitDelta.style.color = '#94a3b8';
      }
    }
    if (elPitStatusDot) {
      elPitStatusDot.className = pitValid ? 'status-dot dot-ok' : 'status-dot dot-fault';
      elPitStatusDot.title = pitValid ? 'Pit Sensor Healthy' : 'Pit Sensor Fault / Disconnected';
    }

    // 2. Meat Readout
    if (elMeatTemp) elMeatTemp.textContent = formatF(meatTemp);
    if (elMeatStatus) elMeatStatus.textContent = meatValid ? 'Monitoring' : 'Probe Disconnected';
    if (elFoodStatusDot) {
      elFoodStatusDot.className = meatValid ? 'status-dot dot-ok' : 'status-dot dot-fault';
      elFoodStatusDot.title = meatValid ? 'Food Probe Connected' : 'Food Probe Disconnected';
    }
    if (elFoodSourceBadge) {
      if (snapshot.is_meat_wireless) {
        elFoodSourceBadge.textContent = snapshot.meat_probe_name || 'BLE';
        elFoodSourceBadge.style.display = 'inline-flex';
      } else {
        elFoodSourceBadge.style.display = 'none';
      }
    }
    if (elFoodBatteryWrap && elFoodBattery) {
      if (snapshot.is_meat_wireless && snapshot.meat_battery_pct !== null && snapshot.meat_battery_pct !== undefined && snapshot.meat_battery_pct >= 0) {
        elFoodBattery.textContent = `${snapshot.meat_battery_pct}%`;
        elFoodBatteryWrap.style.display = 'block';
      } else {
        elFoodBatteryWrap.style.display = 'none';
      }
    }

    // 3. System State & Lid Status
    if (elSystemState) {
      elSystemState.textContent = statusMsg.toUpperCase();
      elSystemState.className = 'badge ' + (
        lidOpen ? 'badge-warning' :
        !pitValid ? 'badge-danger' :
        statusMsg === 'REGULATING' ? 'badge-state' : 'badge-state'
      );
    }

    isLidPaused = lidOpen;
    if (elLidPauseBtn) {
      if (lidOpen) {
        elLidPauseBtn.textContent = 'Resume Airflow (Lid Open)';
        elLidPauseBtn.className = 'btn btn-danger';
      } else {
        elLidPauseBtn.textContent = 'Pause for Lid Opening';
        elLidPauseBtn.className = 'btn btn-warning';
      }
    }

    // 4. Actuator Visualizers
    if (elDamperVal) elDamperVal.textContent = `${damperPct}%`;
    if (elDamperBar) elDamperBar.style.width = `${Math.min(100, Math.max(0, damperPct))}%`;
    if (elDamperProgress) elDamperProgress.setAttribute('aria-valuenow', damperPct);

    if (elBlowerVal) elBlowerVal.textContent = `${blowerPct}%`;
    if (elBlowerBar) elBlowerBar.style.width = `${Math.min(100, Math.max(0, blowerPct))}%`;
    if (elBlowerProgress) elBlowerProgress.setAttribute('aria-valuenow', blowerPct);

    if (elFanRpmStatus) {
      elFanRpmStatus.textContent = blowerPct > 0 ? `RUNNING (${blowerPct}%)` : 'OFF';
      elFanRpmStatus.style.color = blowerPct > 0 ? '#38bdf8' : '#94a3b8';
    }

    // Airflow Mode Badge
    if (elAirflowModeBadge) {
      if (lidOpen) {
        elAirflowModeBadge.textContent = 'Lid Suppression (0%)';
        elAirflowModeBadge.style.background = 'rgba(239, 68, 68, 0.2)';
        elAirflowModeBadge.style.color = '#ef4444';
      } else if (!pitValid) {
        elAirflowModeBadge.textContent = 'Fail-Safe Clamped';
        elAirflowModeBadge.style.background = 'rgba(239, 68, 68, 0.2)';
        elAirflowModeBadge.style.color = '#ef4444';
      } else if (demandPct <= 40) {
        elAirflowModeBadge.textContent = 'Natural Draft (Damper)';
        elAirflowModeBadge.style.background = 'rgba(249, 115, 22, 0.2)';
        elAirflowModeBadge.style.color = '#f97316';
      } else {
        elAirflowModeBadge.textContent = 'Forced Draft Boost (Fan)';
        elAirflowModeBadge.style.background = 'rgba(56, 189, 248, 0.2)';
        elAirflowModeBadge.style.color = '#38bdf8';
      }
    }

    // 5. Uptime & Time History
    const timeVal = snapshot.timestamp_ms !== undefined ? snapshot.timestamp_ms : (snapshot.timestamp_s * 1000);
    if (elFooterUptime) {
      elFooterUptime.textContent = formatUptime(timeVal);
    }

    // 6. Record Trend History Point
    telemetryHistory.push({
      time: timeVal,
      pit: pitTemp,
      setpoint: setpoint,
      meat: meatTemp
    });
    if (telemetryHistory.length > MAX_HISTORY_POINTS) {
      telemetryHistory.shift();
    }

    // Redraw Trend Canvas
    renderTrendChart();
  }

  // --- HTML5 Canvas Trend Graph Renderer ---
  function renderTrendChart() {
    if (!canvas || !ctx) return;

    // Handle high-DPI displays
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    const width = rect.width || 800;
    const height = rect.height || 240;

    if (canvas.width !== Math.floor(width * dpr) || canvas.height !== Math.floor(height * dpr)) {
      canvas.width = Math.floor(width * dpr);
      canvas.height = Math.floor(height * dpr);
    }

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, width, height);

    // Padding
    const padLeft = 46;
    const padRight = 16;
    const padTop = 16;
    const padBottom = 26;
    const chartW = width - padLeft - padRight;
    const chartH = height - padTop - padBottom;

    if (telemetryHistory.length === 0) {
      ctx.fillStyle = '#64748b';
      ctx.font = '13px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Waiting for temperature telemetry...', width / 2, height / 2);
      ctx.restore();
      return;
    }

    // Calculate Y range
    let minY = 150;
    let maxY = 300;
    for (const pt of telemetryHistory) {
      if (pt.pit !== null) {
        if (pt.pit < minY) minY = Math.floor(pt.pit - 10);
        if (pt.pit > maxY) maxY = Math.ceil(pt.pit + 10);
      }
      if (pt.setpoint !== null) {
        if (pt.setpoint < minY) minY = Math.floor(pt.setpoint - 10);
        if (pt.setpoint > maxY) maxY = Math.ceil(pt.setpoint + 10);
      }
      if (pt.meat !== null) {
        if (pt.meat < minY) minY = Math.floor(pt.meat - 10);
        if (pt.meat > maxY) maxY = Math.ceil(pt.meat + 10);
      }
    }
    // Round to nice 25° increments
    minY = Math.floor(minY / 25) * 25;
    maxY = Math.ceil(maxY / 25) * 25;
    if (maxY - minY < 50) maxY = minY + 50;

    const yRange = maxY - minY;

    // Draw horizontal grid lines & labels
    const step = yRange <= 100 ? 25 : 50;
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';

    for (let yVal = minY; yVal <= maxY; yVal += step) {
      const yPos = padTop + chartH - ((yVal - minY) / yRange) * chartH;
      // Grid line
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.07)';
      ctx.lineWidth = 1;
      ctx.setLineDash([]);
      ctx.beginPath();
      ctx.moveTo(padLeft, yPos);
      ctx.lineTo(width - padRight, yPos);
      ctx.stroke();

      // Label
      ctx.fillStyle = '#64748b';
      ctx.fillText(`${yVal}°`, padLeft - 8, yPos);
    }

    // Helper to map index to X coord
    const numPoints = telemetryHistory.length;
    function getX(idx) {
      if (numPoints <= 1) return padLeft;
      return padLeft + (idx / (numPoints - 1)) * chartW;
    }
    function getY(tempVal) {
      return padTop + chartH - ((tempVal - minY) / yRange) * chartH;
    }

    // 1. Draw Setpoint Line (Dashed Amber)
    ctx.strokeStyle = '#fbbf24';
    ctx.lineWidth = 1.5;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    let started = false;
    for (let i = 0; i < numPoints; ++i) {
      const sp = telemetryHistory[i].setpoint;
      if (sp !== null) {
        const x = getX(i);
        const y = getY(sp);
        if (!started) {
          ctx.moveTo(x, y);
          started = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
    }
    ctx.stroke();

    // 2. Draw Meat Temperature Line (Crimson Red)
    ctx.strokeStyle = '#ef4444';
    ctx.lineWidth = 2.0;
    ctx.setLineDash([]);
    ctx.beginPath();
    started = false;
    for (let i = 0; i < numPoints; ++i) {
      const mt = telemetryHistory[i].meat;
      if (mt !== null) {
        const x = getX(i);
        const y = getY(mt);
        if (!started) {
          ctx.moveTo(x, y);
          started = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
    }
    ctx.stroke();

    // 3. Draw Chamber Pit Temperature Line (Vibrant Flame Orange)
    ctx.strokeStyle = '#f97316';
    ctx.lineWidth = 2.5;
    ctx.setLineDash([]);
    ctx.beginPath();
    started = false;
    for (let i = 0; i < numPoints; ++i) {
      const pt = telemetryHistory[i].pit;
      if (pt !== null) {
        const x = getX(i);
        const y = getY(pt);
        if (!started) {
          ctx.moveTo(x, y);
          started = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
    }
    ctx.stroke();

    // Draw bottom time axis line
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.15)';
    ctx.lineWidth = 1;
    ctx.setLineDash([]);
    ctx.beginPath();
    ctx.moveTo(padLeft, padTop + chartH);
    ctx.lineTo(width - padRight, padTop + chartH);
    ctx.stroke();

    // Time indicators
    ctx.fillStyle = '#64748b';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText('-5m', padLeft, height - 8);
    ctx.textAlign = 'center';
    ctx.fillText('-2.5m', padLeft + chartW / 2, height - 8);
    ctx.textAlign = 'right';
    ctx.fillText('now', width - padRight, height - 8);

    ctx.restore();
  }

  // --- Transport Communication: SSE, WS, & Fallback Polling ---
  function startStreaming() {
    // 1. Try Server-Sent Events (SSE) first
    if (window.EventSource) {
      try {
        eventSource = new EventSource('/api/events');
        eventSource.onmessage = function (event) {
          try {
            const data = JSON.parse(event.data);
            handleTelemetry(data);
          } catch (e) {
            console.error('Failed to parse SSE JSON:', e);
          }
        };

        eventSource.onerror = function () {
          setConnectionStatus('connecting');
          // If SSE disconnects or server does not support it, trigger polling
          startPolling();
        };

        eventSource.onopen = function () {
          setConnectionStatus('connected');
          stopPolling();
        };
        return;
      } catch (e) {
        console.warn('SSE creation failed, falling back to HTTP polling:', e);
      }
    }

    // 2. Fallback to 1Hz HTTP Polling
    startPolling();
  }

  function fetchTelemetryOnce() {
    fetch('/api/telemetry')
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
      })
      .then((data) => {
        handleTelemetry(data);
      })
      .catch((err) => {
        setConnectionStatus('disconnected');
        console.warn('Telemetry polling error:', err);
      });
  }

  function startPolling() {
    if (!pollingInterval) {
      fetchTelemetryOnce();
      pollingInterval = setInterval(fetchTelemetryOnce, 1000);
    }
  }

  function stopPolling() {
    if (pollingInterval) {
      clearInterval(pollingInterval);
      pollingInterval = null;
    }
  }

  // --- User Control Actions ---
  window.syncSliderToInput = function (val) {
    if (elSetpointInput) elSetpointInput.value = val;
  };

  window.syncInputToSlider = function (val) {
    if (elSetpointSlider) elSetpointSlider.value = val;
  };

  window.setPreset = function (temp) {
    if (elSetpointSlider) elSetpointSlider.value = temp;
    if (elSetpointInput) elSetpointInput.value = temp;
    window.submitSetpoint();
  };

  window.submitSetpoint = function () {
    const val = parseFloat(elSetpointInput ? elSetpointInput.value : elSetpointSlider.value);
    if (isNaN(val) || val < 100 || val > 450) {
      alert('Target temperature must be between 100°F and 450°F.');
      return;
    }

    fetch('/api/setpoint', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ setpoint: val })
    })
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json().catch(() => ({}));
      })
      .then(() => {
        if (elCurrentSetpoint) elCurrentSetpoint.innerHTML = `${formatF(val)}&deg;F`;
      })
      .catch((err) => {
        console.error('Failed to submit setpoint:', err);
        alert('Could not update setpoint: ' + err.message);
      });
  };

  window.toggleLidPause = function () {
    fetch('/api/lid-pause', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: isLidPaused ? 'resume' : 'pause' })
    })
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json().catch(() => ({}));
      })
      .then(() => {
        fetchTelemetryOnce();
      })
      .catch((err) => {
        console.error('Failed to toggle lid pause:', err);
      });
  };

  function fetchConfig() {
    fetch('/api/config')
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
      })
      .then((cfg) => {
        const elKp = document.getElementById('cfg-kp');
        const elKi = document.getElementById('cfg-ki');
        const elKd = document.getElementById('cfg-kd');
        const elThresh = document.getElementById('cfg-threshold');
        if (elKp && cfg.pid_kp !== undefined) elKp.value = cfg.pid_kp;
        if (elKi && cfg.pid_ki !== undefined) elKi.value = cfg.pid_ki;
        if (elKd && cfg.pid_kd !== undefined) elKd.value = cfg.pid_kd;
        if (elThresh && cfg.airflow_threshold_pct !== undefined) elThresh.value = cfg.airflow_threshold_pct;
      })
      .catch((err) => {
        console.warn('Could not load config:', err);
      });
  }

  window.submitTuningConfig = function () {
    const elKp = document.getElementById('cfg-kp');
    const elKi = document.getElementById('cfg-ki');
    const elKd = document.getElementById('cfg-kd');
    const elThresh = document.getElementById('cfg-threshold');
    const msg = document.getElementById('save-msg');

    const kp = parseFloat(elKp ? elKp.value : 3.0);
    const ki = parseFloat(elKi ? elKi.value : 0.02);
    const kd = parseFloat(elKd ? elKd.value : 15.0);
    const thresh = parseFloat(elThresh ? elThresh.value : 40.0);

    fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        pid_kp: kp,
        pid_ki: ki,
        pid_kd: kd,
        airflow_threshold_pct: thresh
      })
    })
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        return res.json();
      })
      .then(() => {
        if (msg) {
          msg.textContent = 'Saved to NVS flash!';
          setTimeout(() => { msg.textContent = ''; }, 3000);
        }
      })
      .catch((err) => {
        alert('Failed to save config: ' + err.message);
      });
  };

  // Resize listener for Canvas responsiveness
  window.addEventListener('resize', function () {
    renderTrendChart();
  });

  // Start telemetry loop on DOM readiness
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () {
      startStreaming();
      fetchConfig();
    });
  } else {
    startStreaming();
    fetchConfig();
  }
})();
