# Homebrew tap

Standalone formula for installing MacPhoenix on macOS via Homebrew.

Status (checked 2026-10-21): a draft (`92c75d19`, 2026-04-25), never
published. MacPhoenix has not been built on macOS as far as git history
shows: CI (`.github/workflows/build.yml`) builds Linux packages only,
there is no tap repo, and there is no release or git tag yet, so only
`--HEAD` installs are possible. The formula's dependencies follow
`CMakeLists.txt` and `debian/control`: Qt 6 (>= 6.4, Core and Network),
OpenSSL (for libdatachannel), libyuv (required), OpenH264, libvpx,
libwebp and Opus (optional codecs), and Rust for `net-bridge`. It
builds with the defaults otherwise, except `-DBUILD_TESTS=OFF` and
`-DBUILD_BRIDGE_AGENT=OFF` (no Retro68; the committed `BridgeAgent.bin`
and `MacBrowser.bin` are installed instead). The Executor backend needs
nothing more (its generated sources are committed). Version 1.0.0 comes from `project()`
in `CMakeLists.txt`.

## Publishing the tap

Homebrew taps are separate GitHub repos with the naming convention
`<owner>/homebrew-<name>`. Create one and copy the formula in:

```bash
gh repo create sirmick/homebrew-mac-phoenix --public
git clone git@github.com:sirmick/homebrew-mac-phoenix.git ~/src/homebrew-mac-phoenix
cp -r packaging/homebrew/Formula ~/src/homebrew-mac-phoenix/
cd ~/src/homebrew-mac-phoenix
git add Formula && git commit -m "mac-phoenix: initial formula" && git push
```

Users then install with:

```bash
brew tap sirmick/mac-phoenix
brew install --HEAD mac-phoenix     # while only `head` is wired up
brew install mac-phoenix            # once a tagged release exists
```

## Testing locally on macOS (no tap repo needed yet)

The formula can be installed straight from a file path:

```bash
brew install --build-from-source --HEAD packaging/homebrew/Formula/mac-phoenix.rb
mac-phoenix --help
brew test mac-phoenix
brew uninstall mac-phoenix
```

`brew audit` runs the same lint checks the Homebrew core repo would:

```bash
brew audit --strict --new-formula packaging/homebrew/Formula/mac-phoenix.rb
```

## Testing on a clean macOS via GitHub Actions

You don't need a Mac to validate the formula — `macos-latest` runners are
free for public repos. Drop this at `.github/workflows/homebrew-test.yml`:

```yaml
name: homebrew-test
on:
  push:
    paths: [packaging/homebrew/**]
  workflow_dispatch:

jobs:
  brew-install:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - run: brew audit --strict --new-formula packaging/homebrew/Formula/mac-phoenix.rb
      - run: brew install --build-from-source --HEAD packaging/homebrew/Formula/mac-phoenix.rb
      - run: brew test mac-phoenix
```

Push to a branch that touches `packaging/homebrew/**`, watch the run, fix
anything `brew audit` complains about. Once it goes green, push the tap
repo and announce the install command in the README.

## Cutting a release with a stable URL

Once the formula works against `--HEAD`, switch to a versioned tarball:

```bash
# 1. Build the source tarball (vendored crates included)
tools/make-source-tarball.sh

# 2. Create a GitHub release and upload the tarball
gh release create v1.0.0 dist/mac-phoenix_1.0.0.tar.xz \
    --title "MacPhoenix 1.0.0" --notes "..."

# 3. Get the sha256
shasum -a 256 dist/mac-phoenix_1.0.0.tar.xz

# 4. Replace the `head do ... end` block in the formula with:
#       url    "https://github.com/sirmick/mac-phoenix/releases/download/v1.0.0/mac-phoenix_1.0.0.tar.xz"
#       sha256 "<the sha from step 3>"
```

The tarball carries the vendored `subprojects/` (there are no git
submodules any more) and net-bridge's vendored cargo crates, so Homebrew
builds offline once it's downloaded.
