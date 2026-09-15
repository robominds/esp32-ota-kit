# esp32-ota-kit — Design

Author: Mark Castelluccio <markacastelluccio@gmail.com>
Drafted with Claude Code (Anthropic Claude Opus 5). Extracted from the CrowPanel
Advance OTA demo (`robominds/Crowpanel-7.0-HMI-IPS-Display-OTADemo`, tags
`phase-1`, `phase-2`, and the later-validation follow-up), where every behaviour
below was verified on hardware.

## Goal

A PlatformIO library that gives any arduino-esp32 3.x application WiFi firmware
updates with bootloader rollback, without the application reimplementing the
hard parts:

- **Push:** `pio run -e <env>_ota -t upload` sends a build over WiFi (ArduinoOTA /
  espota).
- **Pull:** the device checks an HTTP manifest and installs a newer image with an
  MD5-verified, bounded download.
- **Rollback:** a new image is confirmed only after it has run normally for a
  while; a build that crashes first is reverted by the bootloader.
- **Server:** `tools/serve.py` writes a manifest for a PlatformIO build and serves
  it.

The library has no user interface. It reports events through an observer; each
application draws them its own way.

## Work breakdown

This spec covers sub-project 1. The others get their own spec, plan and hardware
verification, in this order:

1. **esp32-ota-kit library** (this document).
2. **OTA demo migrates onto the library.** Its `ui` becomes an observer; its
   `ota_push`, `rollback`, pull core, `lib/otacore` and `tools/ota_server` are
   removed. Every demo hardware scenario is re-run. This is the library's
   hardware verification; `v1.0.0` is tagged only after it passes.
3. **Temperature display (`robominds/Crowpanel-Advance-7.0-HMI-Display`) adopts
   the library.** `default_16MB.csv`, all secrets moved to `secrets.ini`, a
   full-screen update overlay, push + rollback only (pull disabled), one USB
   flash, then updates over WiFi.

## Decisions already made

| Decision | Choice |
| --- | --- |
| Library contents | Push, rollback, pull download, `otacore`, `serve.py` |
| Coupling to apps | `ota::Config` + `ota::Observer`; no LVGL, driver or app headers |
| Repository | `robominds/esp32-ota-kit`, **private**, MIT licence, local clone `~/projects/esp32-ota-kit` |
| serve.py distribution | Run from the consuming project's libdeps with `--project` |
| Validation timing | 30 s after the network first comes up, 90 s fallback, confirm before an update writes |

**Consequence of a private repository:** both consuming applications are public.
Once they depend on this library, anyone who clones them cannot build them
without access to the library. Accepted.

## Target and toolchain

| Item | Value |
| --- | --- |
| Framework | Arduino, arduino-esp32 **3.3.9** (pioarduino platform-espressif32 55.03.39), ESP-IDF 5.5.4 |
| MCU | Any ESP32 with two OTA app slots; compile-checked on `esp32-s3-devkitc-1` |
| Bootloader | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` (set in the arduino-esp32 `qio_opi` sdkconfig used by the CrowPanel apps) |
| Dependencies | ArduinoJson `^7.0.0`; bundled WiFi/Network, ArduinoOTA, Update, HTTPClient, Preferences, ESPmDNS |
| Partition table | The app's choice; must have `ota_0` and `ota_1` (e.g. `default_16MB.csv`) |

## Repository layout

```
library.json              name esp32-ota-kit, version 1.0.0, platforms espressif32,
                          frameworks arduino, dependencies bblanchon/ArduinoJson ^7.0.0,
                          export.exclude docs/ and test/ (not shipped to consumers)
src/ota.h                 public API (the only header applications include)
src/ota.cpp               begin/setNetworkUp/poll/requestCheck/requestInstall/busy,
                          and the verifyRollbackLater() override
src/detail/rollback.h/.cpp slot info, NVS boot counter, confirmation timing, confirmBeforeUpdate
src/detail/push.h/.cpp    ArduinoOTA wiring -> Observer
src/detail/pull.h/.cpp    manifest check, bounded download -> Observer
src/otacore.h/.cpp        version parse/compare, manifest parse (pure C++17 + ArduinoJson)
test/test_otacore/        Unity host tests (24, carried over)
examples/basic/basic.cpp  minimal application: serial-only observer
tools/serve.py            manifest + HTTP server for pull OTA
platformio.ini            env native (host tests), env example (ESP32-S3 compile check)
README.md                 usage, observer contract, platformio.ini recipe, security limits
CHANGELOG.md, LICENSE (MIT)
docs/superpowers/specs/   this document
```

Everything under `src/detail/` is in namespace `ota::detail` and is not part of
the API.

## Public API (`src/ota.h`)

```cpp
namespace ota {

struct Config {
    const char* hostname;                  // mDNS and ArduinoOTA name, without ".local"
    const char* push_password;             // ArduinoOTA password; nullptr disables push
    uint16_t    push_port = 3232;
    const char* manifest_url = nullptr;    // http URL of manifest.json; nullptr disables pull
    const char* running_version;           // this build's "MAJOR.MINOR.PATCH"
    uint32_t    auto_check_delay_ms = 5000;          // after first network up; 0 = never
    uint32_t    validate_after_network_ms = 30000;
    uint32_t    validate_timeout_ms = 90000;         // from boot, without a network
};

enum class Source { Push, Pull };

struct SlotInfo {
    const char* label;   // "app0" / "app1"
    const char* state;   // "serial-flashed", "pending verify", "confirmed", ...
    uint32_t    boots;   // NVS boot counter, 0 if NVS is unavailable
};

struct CheckResult {
    enum Kind { UpToDate, Available, Failed } kind;
    const char* version;   // server version for UpToDate / Available, else nullptr
    size_t      size;      // image size for Available, else 0
    const char* message;   // failure text for Failed, else nullptr
};

class Observer {
public:
    virtual ~Observer() = default;
    virtual void onSlot(const SlotInfo&) {}             // after begin(), and when confirmed
    virtual void onPushListening(uint16_t port) {}
    virtual void onCheckStarted() {}
    virtual void onCheckResult(const CheckResult&) {}
    virtual void onTransferStarted(Source) {}
    virtual void onProgress(Source, uint8_t percent) {} // at most 10 per second, and at 100
    virtual void onRebooting(Source) {}                 // just before the restart
    virtual void onError(Source, const char* message) {}
};

void begin(const Config& config, Observer& observer);
void setNetworkUp(bool up);
void poll();
void requestCheck();
void requestInstall();
bool busy();

}  // namespace ota
```

### Contract for applications

- Call `begin()` once in `setup()` after the display is up, and before the
  backlight comes on if the NVS write could disturb the panel. `Config` strings
  must outlive the program (build-flag literals do).
- Call `setNetworkUp(WiFi connected?)` every loop (or on every change) and
  `poll()` every loop, from `loop()`, never from inside an LVGL callback or
  `lv_timer_handler()`.
- Call `begin()` before any other `ota::` function; calls made before it are
  ignored.
- `requestCheck()` and `requestInstall()` may be called from anywhere on the
  loop task, including LVGL event callbacks. They only queue work for `poll()`.
  Both are ignored while `busy()` is true or when pull is disabled
  (`manifest_url == nullptr`); `requestInstall()` is also ignored unless the
  last check returned `Available`.
- Observer methods run on the loop task, from inside `poll()` or `begin()`. They
  may update widgets and call `lv_timer_handler()` to repaint. They must not
  call back into `ota::` except `busy()`.
- String pointers passed to observer methods are valid only for the duration of
  the call.
- Transfers block `poll()` until they finish or fail; applications' other
  `loop()` work (MQTT, sensors) pauses for that time.
- The application owns WiFi. The library never calls `WiFi.begin()` or changes
  reconnection behaviour.

## Behaviour

### begin()
1. Increments the NVS boot counter in namespace `"ota"` (logs and continues with
   0 if NVS cannot be opened).
2. Reads the running partition label and its OTA state.
3. A serial-flashed image (state undefined) or an already valid image counts as
   confirmed; `PENDING_VERIFY` or `NEW` starts unconfirmed.
4. Logs `ota: running <label>, state <state>, boot #<n>` and calls `onSlot`.

### setNetworkUp(up)
- On the first transition to `true` since boot:
  - if `push_password` is set: configures ArduinoOTA (port, hostname, password,
    reboot on success), starts it (which starts mDNS), logs
    `ota: push listening on <host>.local:<port>`, calls `onPushListening`;
  - if unconfirmed: records the time and logs
    `ota: network up, confirming in <n> s`;
  - if `manifest_url` is set and `auto_check_delay_ms > 0`: schedules a check.
- Later transitions do not restart the confirmation timer and do not re-begin
  ArduinoOTA.
- When the network is down, a check or install fails fast with `no network`.

### poll()
In order, every call:
1. **Confirmation:** if unconfirmed and (network first came up at least
   `validate_after_network_ms` ago, or `millis()` is at least
   `validate_timeout_ms`), marks the image valid, logs
   `ota: image confirmed (<esp_err>)`, calls `onSlot`.
2. **Push:** `ArduinoOTA.handle()`. A transfer blocks here.
3. **Queued install**, if any (runs instead of step 4 this call).
4. **Queued or scheduled check**, if due.

### Push transfer (ArduinoOTA callbacks)
- `onStart`: confirm the running image first (see below), `onTransferStarted(Push)`.
- `onProgress`: throttled to 10 per second plus the final 100 %, `onProgress(Push, %)`.
- `onEnd`: `onRebooting(Push)`, 500 ms for the frame to reach the panel; ArduinoOTA restarts.
- `onError`: `onError(Push, text)` with `auth failed`, `begin failed (image too large?)`,
  `connect failed`, `receive failed`, `end failed (bad image)`.
- ArduinoOTA calls `onStart` only after authentication succeeded, so a wrong
  password never confirms anything.

### Pull check
`onCheckStarted`; then HTTP GET `manifest_url` (5 s connect and read timeouts).
Failures report `onCheckResult{Failed, message}` and log `ota: check failed: <message>`:
`no network`, `bad manifest_url`, `HTTP <code>` or the HTTPClient error string,
`manifest too large` (body over 1024 bytes), `manifest: ...` from `otacore`,
`running_version is not MAJOR.MINOR.PATCH`. Otherwise compares versions:
server newer -> `Available{version, size}` and the manifest is kept for install;
equal or older -> `UpToDate{version}`.

### Pull install
Only if the last check returned `Available`. `onTransferStarted(Pull)`; GET the
image URL; Content-Length must equal the manifest size; confirm the running image
first; `Update.begin(size)`, `Update.setMD5(md5)`; read the body in 4 KB chunks
into `Update.write()`, giving up after **10 s without data** or when the
connection closes; `Update.end()`. Progress as for push. Failures call
`onError(Pull, message)` with `no network`, `bad image url`, `HTTP <code>`,
`size differs from manifest`, `connection lost`, `download stalled`, the Update
library's text (e.g. `MD5 Check Failed`), `flash write failed`,
`image not accepted`; any begun update is aborted, nothing is installed, no
reboot, and a new check is required before another install. On success:
`onRebooting(Pull)`, 1 s, `ESP.restart()`.

`Update.writeStream()` is not used: with the server gone it retries a 5 s read
300 times (about 25 minutes blocked), and its first-byte check peeks before the
body arrives.

### Confirm before an update
Before any transfer writes flash (push `onStart`; pull just before
`Update.begin()`), an unconfirmed running image is marked valid and
`ota: image confirmed before update (<esp_err>)` is logged, followed by
`onSlot`. The transfer overwrites the other slot, which holds the last confirmed
image; without this, a failing update inside the confirmation window would leave
nothing valid to roll back to.

### Rollback override
`extern "C" bool verifyRollbackLater() { return true; }` lives in `src/ota.cpp`,
the same translation unit as `ota::begin()`. PlatformIO links libraries as
archives; an override in an object file nothing else references would never be
linked, the core's weak default would validate every image right after
`setup()`, and rollback would silently stop working.

### Trade-offs (documented in the README)
- Power-cycling within about 40 s of an update reverts a good image.
- The image contains `push_password` and the application's WiFi credentials as
  plain strings; anyone who can download it (for example from `serve.py`) can
  read them.
- Pull uses plain HTTP; the MD5 comes from the same server, so it detects
  corruption, not tampering.
- Flash writes disturb RGB panels that scan out of PSRAM (tearing during a
  transfer); the reboot restores the picture.

## otacore

Unchanged from the demo: strict `MAJOR.MINOR.PATCH` parse (no leading zeros,
components up to 1,000,000), `compareVersions`, `parseManifest` requiring
`version`, `url`, `size` (> 0, integer), `md5` (32 hex, lowercased), with the
error strings `manifest: not json`, `manifest: missing <field>`,
`manifest: bad version|url|size|md5`. Namespace stays `otacore`.

## tools/serve.py

As in the demo, plus:
- `--project <dir>` (default: current directory): the PlatformIO project whose
  `platformio.ini` and `.pio/build/<env>/firmware.bin` are served.
- `--env` default `advance_70`, `--port` 8000, `--host` auto-detected.
- Prints the security warning that the image contains credentials.
- Exits with a clear message (no traceback) when the port is in use or no LAN
  address can be found.

Documented invocation from a consuming project:
`python3 .pio/libdeps/<env>/esp32-ota-kit/tools/serve.py --project .`
(with a Python the macOS firewall allows to accept connections).

## Consuming the library

Recipe for an application's `platformio.ini` (in the README):

```ini
[env:app]
board_build.partitions = default_16MB.csv
board_upload.maximum_size = 6553600
lib_deps =
    git+ssh://git@github.com/robominds/esp32-ota-kit.git#v1.0.0
build_flags =
    -DOTA_HOSTNAME='${secrets.device_host}'
    -DOTA_PASSWORD='${secrets.ota_password}'

[env:app_ota]
extends         = env:app
upload_protocol = espota
upload_port     = ${secrets.device_host}.local
upload_flags    = --auth=${secrets.ota_password}
```

The private-repository `lib_deps` form is verified in the first implementation
task; if PlatformIO rejects `git+ssh://`, the fallback is
`https://github.com/robominds/esp32-ota-kit.git#v1.0.0` with `gh auth
setup-git` providing credentials. During development the demo uses
`symlink://../../esp32-ota-kit`.

## Logging

All library log lines go to `Serial` with the prefix `ota:`. The strings above
are part of the hardware verification and are kept stable within a major
version.

## Testing

1. **Host:** `pio test -e native` in the library repository: the 24 `otacore`
   tests.
2. **Compile:** `pio run -e example` builds `examples/basic` for
   `esp32-s3-devkitc-1`; `xtensa-esp32s3-elf-nm` on its `firmware.elf` must show
   exactly one ` T verifyRollbackLater`.
3. **serve.py:** a scripted local run against the example build checks the
   manifest fields, `Content-Length`, `Cache-Control: no-store`, `--project`,
   and the port-in-use message.
4. **Hardware:** sub-project 2 re-runs the demo's full scenario list on the
   CrowPanel Advance: push; wrong password; aborting build rolled back; six-reset
   WiFi test; pull up to date / available / install / bad MD5 / server down /
   server killed mid-download; crash 5 s after WiFi rolled back; update inside
   the confirmation window. `v1.0.0` is tagged only after all pass.

## Release

- Semantic versioning; `library.json` version and a `vX.Y.Z` git tag move
  together; `CHANGELOG.md` records each release.
- The private GitHub repository is created when the first implementation commit
  is ready to push.
- Source headers end with
  `// Author: Mark Castelluccio <markacastelluccio@gmail.com>` and
  `// Written with assistance from Claude Code (Anthropic Claude Opus 5).`
  Commits end with `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.

## Out of scope

HTTPS, signed images and secure boot; persisting application data across
reboots; WiFi management; any user interface; migrating either application's
secrets or UI (sub-projects 2 and 3).
