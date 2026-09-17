#!/usr/bin/env python3
"""Build both halves with the prepared CORNE_BUILD_ENV; publish only on success.

The runtime contains the copied ZMK v0.3 app and its Zephyr 3.5.0 dependencies.
This command builds current local config/OLED sources; it does not run west update.
If dependency pins change, prepare a matching runtime before using this command.
"""

import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    repo = Path(__file__).resolve().parents[1]
    # Preserve /tmp spelling: Zephyr/CMake on macOS normalizes /private/tmp to it.
    runtime = Path(os.path.abspath(os.path.expanduser(
        os.environ.get("CORNE_BUILD_ENV", "/tmp/corne-oled-build"))))
    required = ["venv/bin/cmake", "venv/bin/ninja", "venv/bin/python",
                "app/CMakeLists.txt", "zephyr/CMakeLists.txt", "sdk/sdk_version",
                "dependencies.json"]
    missing = [str(runtime / name) for name in required if not (runtime / name).is_file()]
    if missing:
        raise ValueError("Prepared build environment is missing: " + ", ".join(missing)
                         + ". Set CORNE_BUILD_ENV to its existing location.")

    modules = [runtime / item["path"]
               for item in json.loads((runtime / "dependencies.json").read_text())]
    modules.append(repo / ".zmk/modules/zmk-nice-oled")
    for module in modules:
        if not module.is_dir():
            raise ValueError(f"Required module is missing: {module}")

    env = dict(os.environ, PATH=str(runtime / "venv/bin") + os.pathsep + os.environ["PATH"],
               ZEPHYR_BASE=str(runtime / "zephyr"),
               ZEPHYR_SDK_INSTALL_DIR=str(runtime / "sdk"),
               ZEPHYR_TOOLCHAIN_VARIANT="zephyr", XDG_CACHE_HOME=str(runtime / "cache"))
    cmake = str(runtime / "venv/bin/cmake")
    print("Using prepared ZMK v0.3 / Zephyr 3.5.0 dependencies; no west update.", flush=True)
    images = {}
    for side in ("left", "right"):
        build = runtime / f"build-final-{side}"
        print(f"Building {side} firmware…", flush=True)
        subprocess.run([
            cmake, "-GNinja", "-S", str(runtime / "app"), "-B", str(build),
            f"-DZephyr_DIR={runtime}/zephyr/share/zephyr-package/cmake",
            "-DBOARD=nice_nano_v2", f"-DSHIELD=corne_{side} nice_oled",
            f"-DZMK_CONFIG={repo}/config", f"-DZMK_EXTRA_MODULES={repo}",
            "-DZEPHYR_MODULES=" + ";".join(map(str, modules)),
            f"-DZEPHYR_BASE={runtime}/zephyr",
            f"-DPython3_EXECUTABLE={runtime}/venv/bin/python",
            f"-DPYTHON_EXECUTABLE={runtime}/venv/bin/python",
        ], env=env, check=True)
        uf2 = build / "zephyr/zmk.uf2"
        # UF2 is a Ninja output: removing it forces relinking and fresh conversion.
        uf2.unlink(missing_ok=True)
        subprocess.run([cmake, "--build", str(build), "--parallel", "6"],
                       env=env, check=True)
        data = uf2.read_bytes()
        if not data or len(data) % 512 or any(
            struct.unpack_from("<II", data, offset) != (0x0A324655, 0x9E5D5157)
            or struct.unpack_from("<I", data, offset + 508)[0] != 0x0AB16F30
            for offset in range(0, len(data), 512)
        ):
            raise ValueError(f"Build did not produce a valid UF2: {uf2}")
        images[side] = data

    latest = repo / "latest"
    latest.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".publish-", dir=latest) as staging:
        for side, data in images.items():
            (Path(staging) / f"{side}.uf2").write_bytes(data)
        for side in images:
            (Path(staging) / f"{side}.uf2").replace(latest / f"{side}.uf2")
    print(f"Built both halves; updated {latest}/left.uf2 and {latest}/right.uf2.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Local firmware build failed: {error}")
