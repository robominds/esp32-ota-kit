# esp32-ota-kit

WiFi firmware updates for arduino-esp32 3.x applications, with bootloader
rollback. Extracted from the CrowPanel Advance 7.0-HMI OTA demo, where every
behaviour was verified on hardware.

- **Push:** `pio run -e <env>_ota -t upload` sends a build over WiFi (ArduinoOTA
  and PlatformIO's espota).
- **Pull:** the device checks an HTTP manifest and installs a newer image with
  an MD5-verified download that gives up after 10 s without data.
- **Rollback:** a new image is confirmed only after it has run normally for a
  while; a build that crashes first is reverted by the bootloader.
- **Server:** `tools/serve.py` writes the manifest for a PlatformIO build and
  serves it.

The library draws nothing. It reports through an `ota::Observer`, and your
application decides what the screen shows. It never touches WiFi setup.

## Requirements

- arduino-esp32 **3.x** (tested on 3.3.9 through pioarduino platform-espressif32
  55.03.39).
- A bootloader with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (the arduino-esp32
  `qio_opi` build has it).
- A partition table with two OTA app slots, `ota_0` and `ota_1`, for example
  `default_16MB.csv`. Changing the partition table needs one USB flash.

## Add it to a project

The repository is private: your machine needs SSH access to
`github.com/robominds/esp32-ota-kit`.

```ini
[env:app]
board_build.partitions    = default_16MB.csv
board_upload.maximum_size = 6553600          ; one slot, so the size check is honest
custom_fw_version         = 1.0.0
lib_deps =
    git+ssh://git@github.com/robominds/esp32-ota-kit.git#v1.0.0
build_flags =
    -DFW_VERSION='"${this.custom_fw_version}"'
    -DOTA_HOSTNAME='${secrets.device_host}'
    -DOTA_PASSWORD='${secrets.ota_password}'

[env:app_ota]
extends         = env:app
upload_protocol = espota
upload_port     = ${secrets.device_host}.local
upload_flags    = --auth=${secrets.ota_password}
```

`secrets.ini` (gitignored, pulled in with `extra_configs = secrets.ini`) holds
double-quoted values, for example `ota_password = "a-long-password"`.
PlatformIO keeps the quotes, so the flags above add only the shell's single
quotes. Values must not contain `"`, `'`, `\`, `$`, `` ` `` or `;`.

## Use it

```cpp
#include <WiFi.h>
#include <ota.h>

class ScreenObserver : public ota::Observer {
    void onProgress(ota::Source, uint8_t percent) override { /* draw, repaint */ }
    void onError(ota::Source, const char* message) override { /* show it */ }
    // ...override only what you need; every method defaults to doing nothing
};
ScreenObserver observer;

void setup() {
    // ...display up first...
    ota::Config config;
    config.hostname        = OTA_HOSTNAME;
    config.push_password   = OTA_PASSWORD;
    config.running_version = FW_VERSION;
    // config.manifest_url = "http://192.168.1.10:8000/manifest.json";  // enables pull
    ota::begin(config, observer);
    // ...then start WiFi...
}

void loop() {
    ota::setNetworkUp(WiFi.status() == WL_CONNECTED);
    ota::poll();   // from loop(), never from inside an LVGL callback
    // ...the rest of your loop...
}
```

`examples/basic/basic.cpp` is a complete, serial-only application.

## Contract

- Call `ota::begin()` once in `setup()`, before any other `ota::` call. The
  `Config` strings must outlive the program.
- Call `ota::setNetworkUp()` and `ota::poll()` every loop, from `loop()`.
- `ota::requestCheck()` and `ota::requestInstall()` may be called from button
  callbacks (including LVGL event callbacks). They only queue work for
  `poll()`, which is what lets progress repaint during an install.
- Observer methods run on the loop task inside `begin()` or `poll()`. They may
  update widgets and call `lv_timer_handler()`. They must not call back into
  `ota::` except `busy()`. Strings passed to them are valid only during the
  call.
- Transfers block `poll()` until they finish or fail. The rest of `loop()`
  (MQTT, sensors) pauses for that time.

## Pull updates

1. Bump `custom_fw_version` and build: `pio run -e app`.
2. `python3 .pio/libdeps/app/esp32-ota-kit/tools/serve.py --project . --env app`
   (add `--host <ip>` if the computer has several interfaces). It prints the
   manifest URL; the firmware on the device must have been built with that URL
   in `Config::manifest_url`.
3. The device checks automatically `auto_check_delay_ms` after the network first
   comes up, or when you call `ota::requestCheck()`. Call `ota::requestInstall()`
   after `onCheckResult` reports `Available`.

## Confirmation and rollback

An image installed by push or pull boots in *pending verify*. It is confirmed
`validate_after_network_ms` (30 s) after the network first comes up, or
`validate_timeout_ms` (90 s) after boot without a network. A build that crashes
or hangs before then is reverted by the bootloader on the next boot.

If an update starts before the running image is confirmed, the running image is
confirmed first, because the update overwrites the other slot, which holds the
last confirmed version.

The same window has a cost: power-cycling the device within about 40 s of an
update also reverts a good image.

## Security limits

- `firmware.bin` contains the OTA password and your WiFi credentials as plain
  strings. While `serve.py` runs, anyone on the network can download the image
  and read them, which also defeats the push password. Stop the server when you
  are done.
- Pull uses plain HTTP, and the MD5 comes from the same server as the image. It
  catches a corrupted download, not a malicious one.
- HTTPS or signed images would close both gaps; they are out of scope.

## Troubleshooting

| Symptom | Cause / fix |
| --- | --- |
| espota `No response from device`; device logs `Receive Failed` | espota has the device connect back to the computer, and the firewall blocks PlatformIO's Python (`~/.platformio/penv/bin/python`). Allow it to accept incoming connections. |
| Device reports `check failed: connection refused` while `serve.py` runs | The firewall blocks the Python running `serve.py`, or `manifest_url` has the wrong address. |
| `install failed: MD5 Check Failed` | `firmware.bin` changed after the manifest was written. Restart `serve.py`. |
| `install failed: connection lost` / `download stalled` | The server stopped or the network dropped during the download. Nothing was installed. |
| Device reverts to the old version | The new build crashed before it was confirmed. Check the serial log for the panic. |
| No ` T verifyRollbackLater` in your firmware's symbol table | The library was not linked. Include `ota.h` and call `ota::begin()`. |
| Two push errors for one failed upload (`connect failed`, then `end failed (bad image)`) | ArduinoOTA itself reports both when the device cannot connect back to the computer. Treat the first as the cause; it is usually the firewall (see the first row). |

## Logging

Every library line on `Serial` starts with `ota: `, for example
`ota: network up, confirming in 30 s` and `ota: image confirmed (ESP_OK)`. These
strings stay stable within a major version.

## Developing the library

- `pio test -e native` runs the host tests (`otacore`, timing helpers).
- `pio run -e example` compiles `examples/basic` for an ESP32-S3 with the
  library linked as an archive.

## Authorship

Mark Castelluccio <markacastelluccio@gmail.com>. Written with assistance from
Claude Code (Anthropic Claude Opus 5). Licensed MIT.
