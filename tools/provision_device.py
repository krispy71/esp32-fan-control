#!/usr/bin/env python3
"""Create private, per-device HTTPS and Wi-Fi access material. Never prints secrets."""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import subprocess


def provision(destination: Path, hosts: list[str]) -> None:
    destination = destination.resolve()
    repository = Path(__file__).resolve().parents[1]
    if destination == repository or repository in destination.parents:
        raise ValueError("Provision credentials outside the repository")
    if destination.exists():
        raise ValueError("Choose a new directory; existing credentials will not be overwritten")
    if not hosts or len(hosts) > 8:
        raise ValueError("Provide between one and eight certificate hosts")
    sans = []
    for host in hosts:
        try:
            ipaddress.IPv4Address(host)
            sans.append(f"IP:{host}")
        except ValueError:
            if not re.fullmatch(r"[a-zA-Z0-9](?:[a-zA-Z0-9.-]{0,61}[a-zA-Z0-9])?", host):
                raise ValueError("Invalid certificate hostname")
            sans.append(f"DNS:{host}")
    old_umask = os.umask(0o077)
    try:
        destination.mkdir(parents=True, mode=0o700)
        password = secrets.token_urlsafe(32)
        ap_password = secrets.token_urlsafe(24)
        config = {
            "ap_ssid": f"Smoker-{secrets.token_hex(3)}",
            "ap_password": ap_password,
            "username": "admin",
            "password_sha256": hashlib.sha256(password.encode("ascii")).hexdigest(),
            "hosts": hosts,
        }
        (destination / "device-access.json").write_text(json.dumps(config), encoding="utf-8")
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256",
            "-nodes", "-sha256", "-days", "825", "-subj", "/CN=Smoker Controller",
            "-addext", "subjectAltName=" + ",".join(sans),
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-keyout", str(destination / "device-key.pem"),
            "-out", str(destination / "device-cert.pem"),
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        (destination / "operator-access.txt").write_text(
            f"Wi-Fi SSID: {config['ap_ssid']}\nWi-Fi password: {ap_password}\n"
            f"HTTPS username: admin\nHTTPS password: {password}\n"
            "Trust device-cert.pem on your client before opening the dashboard.\n"
            "Keep this file private. Do not upload it to the device or commit it.\n",
            encoding="utf-8",
        )
        for item in destination.iterdir():
            item.chmod(0o600)
    finally:
        os.umask(old_umask)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New private directory outside the repository")
    parser.add_argument("--host", action="append", dest="hosts", help="Certificate hostname/IP (repeatable)")
    args = parser.parse_args()
    provision(args.output, args.hosts or ["192.168.4.1", "localhost", "127.0.0.1"])
    print("Provisioning created. Credentials are in the private operator-access.txt file; they are not logged.")


if __name__ == "__main__":
    main()
