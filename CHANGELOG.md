# Changelog

## 1.0.0 — 2026-09-15

Initial release, extracted from the CrowPanel Advance 7.0-HMI OTA demo.

- Push OTA through ArduinoOTA for PlatformIO's espota upload.
- Rollback contract: confirm 30 s after the network first comes up (90 s
  fallback), and confirm the running image before an update writes the other
  slot.
- Pull OTA: manifest check, MD5-verified download that gives up after 10 s
  without data.
- `otacore` version and manifest parsing, host-tested.
- `tools/serve.py` with `--project` for consuming projects.
- `ota::Config` + `ota::Observer` API; no user interface in the library.
