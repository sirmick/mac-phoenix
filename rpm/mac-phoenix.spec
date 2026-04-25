#
# RPM spec for mac-phoenix.
#
# Build with the source tarball produced by tools/make-source-tarball.sh:
#   tools/make-source-tarball.sh
#   cp dist/mac-phoenix_2.0.0.tar.xz ~/rpmbuild/SOURCES/
#   rpmbuild -bb rpm/mac-phoenix.spec
#
# Cargo runs offline against the vendored crates bundled in the tarball
# (net-bridge/vendor/cargo/) — no network access at build time.
#
# OpenH264 lives in the fedora-cisco-openh264 repo on Fedora and is not
# pulled in by default. We disable H.264 here; the other codecs (VP9,
# WebP, Opus) plus PNG cover the browser UI fine.

Name:           mac-phoenix
Version:        2.0.0
Release:        1%{?dist}
Summary:        Classic Macintosh emulator with web-based UI

License:        GPL-2.0-or-later
URL:            https://github.com/sirmick/mac-phoenix
Source0:        %{name}_%{version}.tar.xz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig
BuildRequires:  openssl-devel
BuildRequires:  libvpx-devel
BuildRequires:  libwebp-devel
BuildRequires:  opus-devel
BuildRequires:  libyuv-devel
BuildRequires:  cargo
BuildRequires:  rust
BuildRequires:  desktop-file-utils

Recommends:     hfsutils

%description
MacPhoenix boots Mac OS 7.x / 8.x / 9.x in a window or in your browser.

Includes a 68k UAE interpreter (default), a Unicorn-engine-based 68k/PPC
backend for validation, and a KPX PowerPC translator. The browser UI streams
the framebuffer over WebRTC (VP9) or WebSocket (PNG / WebP) and routes mouse
and keyboard input back to the guest.

An optional Rust net-bridge provides Unix-socket NAT networking via smoltcp.

ROMs and disk images are not distributed.

%global debug_package %{nil}

%prep
%autosetup -n %{name}-%{version}

%build
export CARGO_NET_OFFLINE=true
export CARGO_HOME=%{_builddir}/%{name}-%{version}/.cargo-home
mkdir -p "$CARGO_HOME"

# uae_cpu / uae_jit pass -Wno-format to silence the noisy upstream UAE
# format warnings; that disables -Wformat-security too, and Fedora's default
# -Werror=format-security then errors out ("ignored without -Wformat").
# Strip just that one flag — keep the rest of the hardening (PIE, fortify,
# stack-protector, bindnow). Mirrors debian/rules.
CFLAGS=$(echo "%{optflags}" | sed 's/-Werror=format-security//g')
CXXFLAGS=$(echo "%{optflags}" | sed 's/-Werror=format-security//g')
export CFLAGS CXXFLAGS

%cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTS=OFF \
    -DBUILD_NET_BRIDGE=ON \
    -DBUILD_BRIDGE_AGENT=ON \
    -DENABLE_H264=OFF
%cmake_build

%install
%cmake_install

# CMake install drops LICENSE under /usr/share/doc/mac-phoenix/, which is
# fine on Debian but conflicts with Fedora's idiomatic /usr/share/licenses/
# (handled by %license below). Drop the duplicate.
rm -f %{buildroot}%{_docdir}/%{name}/LICENSE

# Validate the .desktop file
desktop-file-validate %{buildroot}%{_datadir}/applications/mac-phoenix.desktop

%files
%license LICENSE
%doc README.md CLAUDE.md
%{_bindir}/mac-phoenix
%{_bindir}/net-bridge
%{_datadir}/mac-phoenix/
%{_datadir}/applications/mac-phoenix.desktop

%changelog
* Sat Apr 25 2026 Mick <sirmick@gmail.com> - 2.0.0-1
- Initial RPM packaging.
