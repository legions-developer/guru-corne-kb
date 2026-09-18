# Latest firmware

- **`left.uf2`** — left / central, `nice_nano_v2`, `corne_left nice_oled`.
- **`right.uf2`** — right / peripheral, `nice_nano_v2`, `corne_right nice_oled`.

On BASE, the former semicolon key types a double quote (`"`) without holding Shift.

Both screens now show their own battery, the selected Bluetooth profile, and
L/B/R layer circles (top to bottom) with the active layer filled. LOWER has an
inverted-T arrow cluster: Up above Left / Down / Right. Its eleven unused positions
and the ten blank letter positions on RAISE produce no input. WPM, Bongo Cat, and
custom remote waking are removed;
ZMK's normal wake/idle blanking remains in place.
Flash **both** files for matching layer/profile synchronization. See
[the layout and behavior notes](../oled/README.md).

Double-reset the half you want to flash, then copy its matching UF2 onto its
bootloader drive. These files are built locally; no commit, push, or GitHub
Actions run is needed. Generated UF2 files are ignored by Git.

Rebuild both halves from the repository root:

```sh
python3 scripts/build-local.py
```

The command refreshes both files only after both builds succeed. If a build
fails, the existing files remain the last successful build and do not include
the failed changes.

The local build uses the already prepared Zephyr SDK, Python environment, and
dependencies in `/tmp/corne-oled-build`. This environment is temporary: if the
OS removes it, restore the build environment before rebuilding. Set
`CORNE_BUILD_ENV` to use an equivalent prepared environment at another path.
The build command does not install or download dependencies.
The prepared runtime currently uses ZMK v0.3 and Zephyr 3.5.0. If dependency
revisions in `config/west.yml` change, prepare a matching runtime first.
