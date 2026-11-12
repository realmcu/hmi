# Version control

## Edit `version_cfg.txt`, nothing else

The image version is defined by `version_cfg.txt` in the project root. `version.h` is a
build artifact (already listed in `.gitignore`); a hand edit is overwritten by the next
build.

## Where the version ends up

| Carrier | Content | Produced by |
| --- | --- | --- |
| `version.h` | `VERSION_MAJOR/MINOR/REVISION/BUILDNUM`, `VERSION_GCID`, `COMMIT`, build time, builder | `tools/version_gen.sh` |
| Output file name | `dashboard_bank0-v3.14.2.4-cee6624a-<md5>.bin` | `md5_generate.sh` in the after-build step, reading `version.h` |
| `Version=` in `mdk/mp.ini` | Written into the 512-byte MP header, compared by the upgrade tool | Synced by the script when `SYNC_MP_INI = 1` (the default here) |

Note that no source file includes `version.h`: it only affects output naming and build
bookkeeping. What the device reports and what the upgrade tool validates is the MP header
`Version`.

**An OTA upgrade is gated on the MP header `Version` only.** The upgrade tool compares the
version in the new image's MP header against the one running on the device; `version.h`
plays no part. So editing `version_cfg.txt` without syncing `mp.ini` leaves OTA convinced
nothing changed and no upgrade is offered. `SYNC_MP_INI` is therefore `1` by default here:
the build writes the same number into `mdk/mp.ini`, which is how the file name and the
device report stop disagreeing.

Two consequences:

- `mdk/mp.ini` is tracked, so it shows up in `git status` whenever the version changes.
  Commit it together with the release.
- OTA typically requires the new version to be at least the running one. When switching
  numbering schemes (device on 1.0.0.0, new image 3.14.2.4) make sure the new number is
  larger, or the device will refuse the upgrade.

## Both build paths run the generator

| Build | When | Entry point |
| --- | --- | --- |
| Keil MDK | Before Build, on every Build / Rebuild | `mdk/project.uvprojx` → User → Run #1 |
| west / cmake | During the cmake configure step | `gcc/CMakeLists.txt` |

`version_cfg.txt` is registered as a CMake configure dependency, so a plain `west build`
re-configures and regenerates after it changes; uVision runs it on every build. Both read
the same config file and therefore produce the same number.

## Fields

| Field | Meaning |
| --- | --- |
| `VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_REVISION` | Major / minor / revision. Leave empty to inherit from the git tag (`<prefix>-vX.Y.Z`) |
| `VERSION_BUILDNUM` | Build counter. `auto` = commits since the last tag; a literal number; or the `BUILD_NUMBER` environment variable. Capped at 4095, exceeding it fails the build |
| `CUSTOMER_NAME` | Up to 8 characters, encoded character by character into `CN_1..CN_8`, padded with `#` |
| `SYNC_MP_INI` | When `1`, `mdk/mp.ini` gets the same version in its `Version=` field. On by default here, otherwise OTA never sees an upgrade |
| `SDK_BASELINE` | Informational only: which SDK release this project was derived from |

## Common tasks

Bump the revision (3.14.2 → 3.14.3):

```ini
VERSION_MAJOR    = 3
VERSION_MINOR    = 14
VERSION_REVISION = 3
VERSION_BUILDNUM = auto
```

Drive the build counter from CI:

```bash
BUILD_NUMBER=137 west build
```

Set the customer name (goes into the MP header):

```ini
CUSTOMER_NAME    = acme
```

## Boot log

`src/application/main.c` prints the build version at the top of `main()`, so the first
lines of the UART log show which image is actually running:

```
APP version 3.14.2.4 commit cee6624ace78
```

The value is baked in at compile time and matches what the same build wrote into the MP
header. `version.h` is included as `../version.h`, so no include path has to be configured
in either build system.

## Behaviour

- When no version field changed, `version.h` is left untouched and keeps its build stamp,
  so an unchanged version does not trigger a recompile.
- In a source drop without git history, `VERSION_GCID` / `COMMIT` fall back to
  `0x00000000` / `000000000000`; the build proceeds and the version number is unaffected.
- To keep driving the version from git tags instead, name them `<prefix>-v<major>.<minor>.<revision>`
  (for example `dashboard-v3.14.2`); every commit after a tag increments `VERSION_BUILDNUM`.
