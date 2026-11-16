# Version control

## Edit `version_cfg.txt`, nothing else

The image version is defined by `version_cfg.txt` in the project root. `version.h` is a
build artifact (already listed in `.gitignore`); a hand edit is overwritten by the next
build.

## Where the version ends up

| Carrier | Content | Produced by |
| --- | --- | --- |
| `version.h` | `VERSION_MAJOR/MINOR/REVISION/BUILDNUM`, `VERSION_GCID`, `COMMIT`, `CUSTOMER_NAME` / `CN_1..CN_8`, build time, builder | `tools/version_gen.sh` |
| Output file name | `dashboard_<mode>_MP-v<major>.<minor>.<revision>.<build>-<gcid>-<md5>.bin` (e.g. `dashboard_src_bank0_MP-v1.0.0.0-c2e4a1b7-<md5>.bin`) | `md5_generate.sh` in the after-build step, reading `version.h` |
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
  numbering schemes (device on 3.14.2.4, new image 1.0.0.0) make sure the new number is
  larger, or the device will refuse the upgrade.

## Both build paths run the generator

| Build | When | Entry point |
| --- | --- | --- |
| Keil MDK | Before Build, on every Build / Rebuild | `mdk/project.uvprojx` → User → Run #1 |
| west / cmake | During the cmake configure step | `gcc/CMakeLists.txt` |

`version_cfg.txt` is registered as a CMake configure dependency, so a plain `west build`
re-configures and regenerates after it changes; uVision runs it on every build. Both read
the same config file and therefore produce the same number.

The cmake path calls the script through `bash` and pins its working directory to the
project root. If no `bash` is found on the machine, the configure step only prints a
warning and keeps the existing `version.h` — the build then carries the previous version,
so install Git Bash (or run the build from a shell that has `bash`) on Windows.

## Fields

| Field | Meaning |
| --- | --- |
| `VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_REVISION` | Major / minor / revision. Left empty, they are taken from the git tag (`<prefix>-vX.Y.Z`); with no tag they become `0`, so set them explicitly |
| `VERSION_BUILDNUM` | Build counter. `auto` (or empty) = commits since the last tag, `0` when the repository has no tag; a literal number; or the `BUILD_NUMBER` environment variable, which always wins. Above 2048 the build fails; above 255 it only warns, because the MP header field is one byte |
| `CUSTOMER_NAME` | Up to 8 characters, encoded character by character into `CN_1..CN_8`, padded with `#`. Defaults to `sdk-v1` when unset |
| `SYNC_MP_INI` | When `1`, `mdk/mp.ini` gets the same version in its `Version=` field. On in this project, otherwise OTA never sees an upgrade |
| `SDK_BASELINE` / `SDK_BASELINE_COMMIT` | Informational only: which SDK release and commit this project was derived from |

## Common tasks

Bump the revision (1.0.0.0 → 1.0.1.0):

```ini
VERSION_MAJOR    = 1
VERSION_MINOR    = 0
VERSION_REVISION = 1
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

## Identifying the running image

There is no UART version banner: no source file in this project includes `version.h`, so
the number exists only in the image file name and in the MP header. To check what a device
runs, read the `Version=` field with the upgrade tool, or compare the MP header of the
image that was flashed.

If you want a build banner, include the generated header explicitly in `main.c` — it sits
in the project root, so `#include "../version.h"` needs no extra include path in either
build system — and print `VERSION_BUILD_STR` / `COMMIT` during start-up.

## Behaviour

- When no version field changed, `version.h` is left untouched and keeps its build stamp,
  so an unchanged version does not trigger a recompile.
- In a source drop without git history, `VERSION_GCID` / `COMMIT` fall back to
  `0x00000000` / `000000000000`; the build proceeds and the version number is unaffected.
- The git tag is only a fallback for empty version fields. This project carries no release
  tags, so an empty field silently becomes `0` — keep all three numbers in `version_cfg.txt`.
- To drive the version from tags instead, name them `<prefix>-v<major>.<minor>.<revision>`
  (for example `dashboard-v1.0.0`); every commit after a tag then becomes `VERSION_BUILDNUM`.
