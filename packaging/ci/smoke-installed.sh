#!/bin/sh
#
# smoke-installed.sh — post-install layout + sanity checks for a freshly
# installed mac-phoenix package. Used by both packaging/Dockerfile.deb
# (RUN this in the test stage) and the GitHub Actions release workflow
# (after `dpkg -i ../*.deb` on the runner). Single source of truth so
# the two paths can't drift.
#
# Exits 0 on success, non-zero with the failing assertion on error.
set -eux

test -x /usr/bin/mac-phoenix
test -x /usr/bin/net-bridge
test -f /usr/share/mac-phoenix/client/index.html
test -f /usr/share/mac-phoenix/client/client.js
test -f /usr/share/mac-phoenix/BridgeAgent.bin
test -f /usr/share/mac-phoenix/MacBrowser.bin
test -f /usr/share/mac-phoenix/MacBrowser.dsk
test -x /usr/share/mac-phoenix/provisioning/install_bridge_agent.sh
test -f /usr/share/applications/mac-phoenix.desktop

mac-phoenix --help | head -3
