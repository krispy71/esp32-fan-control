# Private device access

The embedded dashboard uses HTTPS and a per-device administrator password. There is
no shared Wi-Fi password or default administrator password. Without valid provisioned
access files, the firmware keeps network access disabled; wired temperature control
does not require the dashboard.

## Provision a device

Install Python 3 and OpenSSL, then run from the repository root:

```bash
python3 tools/provision_device.py --output "$HOME/.local/share/esp32-smoker/device-a"
```

The command creates a private directory with files readable only by their owner:

- `device-access.json`: device SSID, Wi-Fi password, administrator password verifier,
  and allowed hostnames.
- `device-cert.pem`: the device's self-signed TLS certificate.
- `device-key.pem`: its private key.
- `operator-access.txt`: the operator's Wi-Fi and HTTPS credentials.

The provisioning command refuses to overwrite an existing directory and does not
print credentials. Keep `operator-access.txt` outside the repository and device flash.
The other three files are also ignored by Git; do not force-add any access files.

Default certificate names cover `192.168.4.1`, `localhost`, and `127.0.0.1`. Use repeated
`--host` options when provisioning if different names are needed. Import the generated
certificate into the trust store of each client you control before using the dashboard.
Check its fingerprint locally with:

```bash
openssl x509 -in "$HOME/.local/share/esp32-smoker/device-a/device-cert.pem" -noout -fingerprint -sha256
```

## Install on the ESP32

Copy only these files into the filesystem image:

```bash
device_dir="$HOME/.local/share/esp32-smoker/device-a"
cp "$device_dir/device-access.json" firmware/data/
cp "$device_dir/device-cert.pem" firmware/data/
cp "$device_dir/device-key.pem" firmware/data/
```

Build and upload the firmware and LittleFS image using the commands in
[firmware/README.md](../firmware/README.md). The filesystem includes private access
material, so treat its generated image as sensitive too. After upload, join the SSID
listed in the private operator file and visit `https://192.168.4.1`. The browser asks
for the administrator credentials. No credentials belong in URLs.

To rotate access, provision a new directory, replace all three device files together,
upload the new filesystem image, and trust the replacement certificate. Retain the
old private directory until the replacement is verified. Existing browser credentials
may need to be cleared before signing in again.

## Run the simulator

The simulator uses the same protected access directory, without copying it into the
repository:

```bash
uv run python -m esp32_fan_control.Controller.cli --web --access-dir "$HOME/.local/share/esp32-smoker/device-a"
```

Open the HTTPS URL reported by the simulator and authenticate using the private
operator file. The simulator defaults to a local loopback address.

## Configuration behavior

Control requests are authenticated and validated before entering the bounded control
queue. Acceptance into that queue is distinct from application by the control loop;
the API exposes the applied command and persistence result. Stale configuration
versions are rejected rather than silently overwriting a newer change.

MEATER tokens are write-only. Reading configuration reports whether a token is
configured, never its value. Leaving the token field unchanged preserves it. The
dashboard exposes explicit replacement/removal behavior. Servo calibration accepts
500–2500 microsecond limits with minimum below maximum, plus direction inversion.
The backend owns validation and applies calibration through the actuator port.

Hardware accuracy, mechanical closure, and physical fault shutdown still require the
bench procedure in [firmware/README.md](../firmware/README.md).
