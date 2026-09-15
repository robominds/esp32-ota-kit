#!/usr/bin/env python3
"""Serve a PlatformIO build for esp32-ota-kit pull updates.

Reads custom_fw_version from the project's platformio.ini, hashes
.pio/build/<env>/firmware.bin, writes <build>/ota/manifest.json next to a copy
of the image, and serves that directory over plain HTTP. Re-run after every
`pio run`; it does not watch.

From a project that uses the library:
    python3 .pio/libdeps/<env>/esp32-ota-kit/tools/serve.py --project . --env <env>

Run it with a Python the operating system's firewall allows to accept incoming
connections; the device connects in to --port.

Author: Mark Castelluccio <markacastelluccio@gmail.com>
Written with assistance from Claude Code (Anthropic Claude Opus 5).
"""

import argparse
import configparser
import errno
import hashlib
import json
import pathlib
import shutil
import socket
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


def lan_ip() -> str:
    """This computer's LAN address, found by asking the kernel which interface
    would route to a public address. No packet is sent."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        sys.exit("could not find this computer's LAN address; pass --host <ip>")
    finally:
        s.close()


def read_version(project: pathlib.Path, env: str) -> str:
    ini = project / "platformio.ini"
    if not ini.is_file():
        sys.exit(f"{ini} not found; pass --project <PlatformIO project directory>")
    cfg = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
    cfg.read(ini)
    section = f"env:{env}"
    if not cfg.has_option(section, "custom_fw_version"):
        sys.exit(f"{ini} [{section}] has no custom_fw_version")
    return cfg.get(section, "custom_fw_version").strip()


class NoCacheHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stdout.write("%s - %s\n" % (self.client_address[0], fmt % args))
        sys.stdout.flush()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--project", default=".", help="PlatformIO project directory (default: .)")
    ap.add_argument("--env", default="advance_70")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--host", default=None, help="IP the device should fetch from (default: auto)")
    args = ap.parse_args()

    project = pathlib.Path(args.project).resolve()
    build = project / ".pio" / "build" / args.env
    src = build / "firmware.bin"
    if not src.is_file():
        sys.exit(f"{src} not found; run `pio run -e {args.env}` in {project} first")

    version = read_version(project, args.env)
    data = src.read_bytes()
    host = args.host or lan_ip()

    out = build / "ota"
    out.mkdir(exist_ok=True)
    shutil.copyfile(src, out / "firmware.bin")
    manifest = {
        "version": version,
        "url": f"http://{host}:{args.port}/firmware.bin",
        "size": len(data),
        "md5": hashlib.md5(data).hexdigest(),
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    handler = partial(NoCacheHandler, directory=str(out))
    try:
        httpd = ThreadingHTTPServer(("0.0.0.0", args.port), handler)
    except OSError as e:
        if e.errno == errno.EADDRINUSE:
            sys.exit(f"port {args.port} is already in use; stop the other server or pass --port")
        raise

    print(f"Serving {version} ({len(data)} bytes, md5 {manifest['md5']})")
    print(f"Manifest: http://{host}:{args.port}/manifest.json")
    print("Ctrl+C to stop.")
    print("Warning: firmware.bin contains any passwords compiled into it;")
    print("anyone on this network can download it while this server runs.")

    with httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nstopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
