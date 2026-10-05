#!/usr/bin/env python3
"""Exercise the actual CLI simulator over authenticated HTTPS with Chromium + axe.

Run: uv run --with playwright==1.58.0 python tools/test_browser.py
The first run installs pinned axe-core into a temporary test directory (never a CDN).
"""
from __future__ import annotations

import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import time

from playwright.sync_api import sync_playwright, expect
from provision_device import provision

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="smoker-browser-") as temporary:
        directory = Path(temporary)
        access = directory / "access"
        provision(access, ["127.0.0.1", "localhost"])
        password = next(line.split(": ", 1)[1] for line in (access / "operator-access.txt").read_text().splitlines() if line.startswith("HTTPS password:"))
        context = ssl.create_default_context(cafile=str(access / "device-ca.pem"))
        authorization = "Basic " + base64.b64encode(f"admin:{password}".encode()).decode()
        # Trust exactly this test certificate's key in Chromium, not arbitrary TLS servers.
        public = subprocess.check_output(["openssl", "x509", "-in", str(access / "device-cert.pem"), "-pubkey", "-noout"])
        der = subprocess.run(["openssl", "pkey", "-pubin", "-outform", "DER"], input=public, check=True, capture_output=True).stdout
        spki = base64.b64encode(hashlib.sha256(der).digest()).decode()
        axe = os.environ.get("AXE_CORE_PATH")
        if not axe:
            subprocess.run(["npm", "install", "--prefix", str(directory / "axe"), "--ignore-scripts", "--no-audit", "--no-fund", "axe-core@4.11.1"], check=True, capture_output=True, timeout=60)
            axe = str(directory / "axe/node_modules/axe-core/axe.min.js")
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
        url = f"https://127.0.0.1:{port}"
        config = directory / "settings.json"
        logs = directory / "simulator.log"
        def start():
            output = logs.open("ab")
            process = subprocess.Popen([sys.executable, "-u", "-m", "esp32_fan_control.Controller.cli", "--web", "--access-dir", str(access), "--port", str(port), "--config-file", str(config)], cwd=ROOT, stdout=output, stderr=output, env={**os.environ, "PYTHONPATH": str(ROOT / "src")})
            output.close()
            for _ in range(100):
                if process.poll() is not None:
                    raise AssertionError("Simulator exited before serving HTTPS")
                try:
                    connection = http.client.HTTPSConnection("127.0.0.1", port, context=context, timeout=1)
                    connection.request("GET", "/api/config", headers={"Authorization": authorization})
                    response = connection.getresponse()
                    response.read()
                    connection.close()
                    if response.status == 200:
                        return process
                except OSError:
                    pass
                time.sleep(0.1)
            process.terminate()
            process.wait(timeout=5)
            raise AssertionError("Simulator did not become ready")
        process = start()
        try:
            with sync_playwright() as playwright:
                browser = playwright.chromium.launch(headless=True, args=[f"--ignore-certificate-errors-spki-list={spki}"])
                browser_context = browser.new_context(http_credentials={"username": "admin", "password": password}, viewport={"width": 1280, "height": 1100}, color_scheme="dark")
                page = browser_context.new_page()
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.goto(url)
                expect(page.locator("#connection-badge")).to_have_text("Connected")
                expect(page.locator("#storage-status-badge")).to_have_text("Storage ready")
                page.locator("#cfg-servo-min").fill("850")
                page.locator("#cfg-servo-max").fill("2150")
                page.locator("#cfg-servo-inverted").check()
                # Keyboard reaches and operates the existing submit control.
                button = page.get_by_role("button", name="Save to Flash Storage")
                button.focus()
                page.keyboard.press("Enter")
                expect(page.locator("#save-msg")).to_have_text("Applied and saved.", timeout=10000)
                assert json.loads(config.read_text())["servo_min_pulse_us"] == 850
                # Invalid relational limits are rejected by the actual backend.
                page.locator("#cfg-servo-min").fill("2400")
                button.click()
                expect(page.locator("#save-msg")).to_contain_text("Invalid request", timeout=10000)
                assert json.loads(config.read_text())["servo_min_pulse_us"] == 850
                page.locator("#cfg-servo-min").fill("850")
                # Write-only credential UI preserves unchanged values on subsequent saves.
                page.locator("#cfg-meat-mode").select_option("3")
                page.locator("#cfg-meater-token").fill("browser-test-placeholder")
                button.click()
                expect(page.locator("#save-msg")).to_have_text("Applied and saved.", timeout=10000)
                expect(page.locator("#cfg-meater-token")).to_have_value("")
                expect(page.locator("#token-status")).to_contain_text("A token is configured")
                page.locator("#cfg-servo-max").fill("2200")
                button.click()
                expect(page.locator("#save-msg")).to_have_text("Applied and saved.", timeout=10000)
                assert json.loads(config.read_text())["meater_cloud_token"] == "browser-test-placeholder"
                page.locator("#cfg-clear-token").check()
                button.click()
                expect(page.locator("#save-msg")).to_have_text("Applied and saved.", timeout=10000)
                expect(page.locator("#token-status")).to_have_text("No token configured.")
                # Force a real filesystem save failure; the UI must distinguish applied from saved.
                backup = directory / "saved-settings.json"
                config.rename(backup)
                config.mkdir()
                try:
                    page.locator("#cfg-servo-max").fill("2300")
                    button.click()
                    expect(page.locator("#save-msg")).to_contain_text("Applied, but could not save", timeout=10000)
                finally:
                    config.rmdir()
                    backup.rename(config)
                page.locator("#cfg-servo-max").fill("2200")
                button.click()
                expect(page.locator("#save-msg")).to_have_text("Applied and saved.", timeout=10000)
                # Check both themes and keyboard focus with the complete real page.
                page.add_script_tag(path=axe)
                for theme in ("dark", "light"):
                    if page.locator("html").get_attribute("data-theme") != theme:
                        page.locator("#theme-toggle").click()
                        page.wait_for_timeout(200)  # Let the existing 150 ms color transition settle.
                    results = page.evaluate("async () => (await axe.run(document, {runOnly: {type: 'tag', values: ['wcag2a', 'wcag2aa', 'wcag21aa']}})).violations")
                    assert not results, [(theme, item["id"], [(node["target"], node["failureSummary"]) for node in item["nodes"]]) for item in results]
                page.set_viewport_size({"width": 360, "height": 900})
                assert page.evaluate("document.documentElement.scrollWidth <= window.innerWidth")
                page.locator("#cfg-servo-min").focus()
                page.keyboard.press("Tab")
                expect(page.locator("#cfg-servo-max")).to_be_focused()
                assert not errors, errors
                # Restart the real CLI and prove calibration survives a fresh process.
                process.terminate(); process.wait(timeout=5)
                process = start()
                page.reload()
                expect(page.locator("#cfg-servo-min")).to_have_value("850")
                expect(page.locator("#cfg-servo-max")).to_have_value("2200")
                expect(page.locator("#cfg-servo-inverted")).to_be_checked()
                browser.close()
        finally:
            process.terminate()
            process.wait(timeout=5)
        print("HTTPS browser verification passed: calibration, rejection, token secrecy, restart, save failure, keyboard, 360px, axe dark/light.")


if __name__ == "__main__":
    main()
