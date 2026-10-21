# Packaging

MacPhoenix builds as native `.deb` and `.rpm` packages, in Docker locally
or in GitHub Actions. Packages contain `mac-phoenix`, `net-bridge`, the
web UI and the committed guest binaries (BridgeAgent, MacBrowser); the
guest applications are never rebuilt in a package build
(`BUILD_BRIDGE_AGENT=OFF`, `BUILD_MAC_BROWSER=OFF`).

Build dependencies come from `debian/control` (`apt-get build-dep .`),
for local builds and CI alike. It doesn't list the Executor backend's
dependencies (Boost, Ruby, Bison, Perl), so packages are built without
`--backend executor`.

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

The UAE core needs `vm.mmap_min_addr=0` on the Docker host for the boot
test ([Install.md](Install.md#host-settings)).

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
