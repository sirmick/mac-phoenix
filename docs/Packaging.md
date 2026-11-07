# Packaging

MacPhoenix builds as native `.deb` and `.rpm` packages, in Docker locally
or in GitHub Actions. Packages contain `mac-phoenix`, `net-bridge`, the
web UI and the committed guest binaries (BridgeAgent, MacBrowser); the
guest applications are never rebuilt in a package build
(`BUILD_BRIDGE_AGENT=OFF`, `BUILD_MAC_BROWSER=OFF`).

Build dependencies come from `debian/control` (`apt-get build-dep .`),
for local builds and CI alike. The Executor backend needs nothing beyond
the C++ compiler (its generated sources are committed), so packages
include `--backend executor`. Executor's own programs (the desktop
application, the standalone server) are not in the packages: below.

## Locally: `packaging/run_matrix.sh`

Needs Docker (the legacy builder; your user in the `docker` group). It makes
the source tarball (`tools/make-source-tarball.sh`, Rust crates vendored),
then builds and installs the package in a container per target and, if a
ROM and disk image are at hand, boot-tests it: `mac-phoenix --backend uae
--no-webserver --network none` must log "Desktop ready".

```bash
packaging/run_matrix.sh                              # every target, with the boot test
MACEMU_SKIP_BOOT=1 packaging/run_matrix.sh           # packages only
MACEMU_TARGETS=ubuntu-24.04 packaging/run_matrix.sh  # one target
```

| Variable | Default | |
|---|---|---|
| `MACEMU_TARGETS` | `ubuntu-24.04 debian-12 fedora-40` | targets (`fedora:*` builds an `.rpm`, the rest `.deb`) |
| `MACEMU_ROM` | `$HOME/quadra.rom` | ROM for the boot test |
| `MACEMU_DISK` | `$HOME/storage/images/7.5.5.img` | disk image for the boot test |
| `MACEMU_TIMEOUT` | 60 | boot-test timeout, seconds |
| `MACEMU_SKIP_BOOT` | unset | `1`: no boot test |
| `MACEMU_KEEP_IMAGES` | unset | `1`: keep the `mac-phoenix-pkg:*` images |

Output:

```
dist/
  mac-phoenix_<version>.tar.xz          source tarball
  packages/<target>/*.deb, *.rpm
  <target>.log, <target>.boot.log       build and boot-test logs
```

`packaging/start-sandbox.sh` / `delete-sandbox.sh` run an Ubuntu 24.04
container to try the built packages by hand. `packaging/ci/smoke-installed.sh`
checks an installed package's layout; `Dockerfile.deb` and CI run it.

## CI: `.github/workflows/build.yml`

On pushes and pull requests to `main`: Ubuntu 24.04 and Debian 12, amd64
and arm64, build the `.deb` and smoke-test it; the packages are workflow
artifacts. On a `v*` tag the same builds and the source tarball go to a
GitHub Release.

## A development container

`packaging/Dockerfile.dev` is an Ubuntu 24.04 build environment with every
dependency, rustup, and optionally Retro68 (`--build-arg BUILD_RETRO68=1`)
for rebuilding the guest applications:

```bash
docker build -f packaging/Dockerfile.dev -t mac-phoenix:dev .
docker run --rm -it -v "$PWD":/src -v ~/storage:/storage:ro -p 11000:11000 mac-phoenix:dev
```

## Homebrew

`packaging/homebrew/` has a formula draft, never published and not built
on macOS yet: [packaging/homebrew/README.md](../packaging/homebrew/README.md).

## Executor's programs

Built and packaged by hand, not in CI:

| What | Command | Details |
|---|---|---|
| Linux AppImage of `executor-desktop` (`Executor-x86_64.AppImage`) | `cmake --build build --target executor-desktop-appimage` or `tools/desktop/appimage.sh [BUILD [OUT]]`; `--test APPIMAGE` runs it in `debian:stable` | [desktop README](../executor/frontends/desktop/README.md), "Packaging" |
| Windows zip (`executor-windows-VERSION.zip`: `executor-desktop.exe`, its DLLs, `executor-standalone.exe`) | `tools/windows/build.sh && tools/windows/package.sh`; `tools/windows/smoke.sh ZIP DIR` under Wine; `tools/windows/symbolize.sh` for crash reports | the same; the zip's README is `tools/windows/README.txt` |
| `executor-standalone` for six OS/arch targets, `SHA256SUMS`, `README.txt` | `make -C executor/server dist WASM_BUILD=$PWD/build-wasm [VERSION=V]` | [Standalone.md](executor/Standalone.md), "A release" |

None of them is signed (what signing would take: Standalone.md).
