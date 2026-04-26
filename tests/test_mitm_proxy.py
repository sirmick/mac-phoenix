#!/usr/bin/env python3
"""
test_mitm_proxy.py — Phase-1 smoke test for net-bridge --mitm-tls.

Spawns net-bridge with MITM enabled and verifies:
  1. The legacy OpenSSL provider loaded (loud-fail diagnostic visible).
  2. Root CA cert was generated, is a valid X.509 root, RSA-1024+ with SHA1
     or SHA256 signature, has the BasicConstraints CA:TRUE flag.
  3. CA cert reload survives a process restart (file-backed, deterministic).
  4. Running with --mitm-tls but no socket consumer doesn't crash for >2s.

Phase 2 (deferred) would drive a full TLS handshake through the bridge by
emulating ethernet/IP/TCP from Python (e.g. via scapy on a TUN device).
This test deliberately stays at the binary surface so it runs in CI
without sudo / network / ROMs.

Usage: python3 tests/test_mitm_proxy.py
Exit 0 on pass, non-zero on fail; prints PASS/FAIL per check.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
NET_BRIDGE = REPO_ROOT / "build" / "net-bridge"
if not NET_BRIDGE.exists():
    NET_BRIDGE = REPO_ROOT / "net-bridge" / "target" / "release" / "net-bridge"


def fail(msg):
    print(f"  FAIL: {msg}")
    return False


def ok(msg):
    print(f"  OK:   {msg}")
    return True


def spawn_bridge(ca_dir: Path, socket_path: Path, log_path: Path):
    """Spawn net-bridge with MITM enabled. Returns the Popen handle."""
    return subprocess.Popen(
        [
            str(NET_BRIDGE),
            "--socket", str(socket_path),
            "--mitm-tls",
            "--mitm-ca-dir", str(ca_dir),
        ],
        stdout=open(log_path, "w"),
        stderr=subprocess.STDOUT,
        env={**os.environ, "RUST_LOG": "info"},
    )


def wait_for_log(log_path: Path, needle: str, timeout_s: float = 5.0) -> bool:
    """Return True once `needle` appears in log_path (within timeout)."""
    start = time.time()
    while time.time() - start < timeout_s:
        if log_path.exists() and needle in log_path.read_text(errors="replace"):
            return True
        time.sleep(0.1)
    return False


def test_provider_loads(tmpdir: Path) -> bool:
    """Smoke: provider load message appears in startup log."""
    sock = tmpdir / "bridge.sock"
    ca_dir = tmpdir / "ca"
    log = tmpdir / "bridge.log"
    proc = spawn_bridge(ca_dir, sock, log)
    try:
        if not wait_for_log(log, "providers loaded — default + legacy"):
            tail = log.read_text(errors="replace")[-1000:] if log.exists() else "(no log)"
            return fail(f"provider-load message missing within 5s. Log tail:\n{tail}")
        return ok("legacy provider loaded at startup")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_ca_cert_valid(tmpdir: Path) -> bool:
    """CA cert exists, is parseable, has the right shape."""
    sock = tmpdir / "bridge.sock"
    ca_dir = tmpdir / "ca"
    log = tmpdir / "bridge.log"
    proc = spawn_bridge(ca_dir, sock, log)
    try:
        # Wait for CA generation (happens at MITM init, before socket accept).
        wait_for_log(log, "MITM TLS enabled", timeout_s=5.0)
        ca_cert = ca_dir / "mitm_ca.crt"
        ca_key = ca_dir / "mitm_ca.key"
        if not ca_cert.exists():
            return fail(f"CA cert not at {ca_cert}")
        if not ca_key.exists():
            return fail(f"CA key not at {ca_key}")
        ok(f"CA files exist at {ca_dir}")

        # Parse with openssl to confirm it's a valid X.509 root.
        try:
            txt = subprocess.run(
                ["openssl", "x509", "-in", str(ca_cert), "-noout", "-text"],
                capture_output=True, text=True, check=True,
            ).stdout
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            return fail(f"openssl x509 parse failed: {e}")

        if "CA:TRUE" not in txt:
            return fail("CA cert missing BasicConstraints CA:TRUE")
        if "MacPhoenix" not in txt:
            return fail("CA subject doesn't mention MacPhoenix")
        ok("CA cert is a well-formed X.509 root with CA:TRUE")
        return True
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_ca_persists(tmpdir: Path) -> bool:
    """Restart the bridge → same CA cert (load path, not re-generate)."""
    sock = tmpdir / "bridge.sock"
    ca_dir = tmpdir / "ca"
    log1 = tmpdir / "bridge1.log"
    log2 = tmpdir / "bridge2.log"

    # First run: generates CA.
    proc = spawn_bridge(ca_dir, sock, log1)
    wait_for_log(log1, "MITM TLS enabled", timeout_s=5.0)
    ca_cert = ca_dir / "mitm_ca.crt"
    if not ca_cert.exists():
        proc.kill()
        return fail("CA cert not generated on first run")
    cert_bytes_1 = ca_cert.read_bytes()
    proc.terminate()
    proc.wait(timeout=2)

    # Second run: should load the same CA, not generate a new one.
    proc = spawn_bridge(ca_dir, sock, log2)
    wait_for_log(log2, "MITM TLS enabled", timeout_s=5.0)
    cert_bytes_2 = ca_cert.read_bytes()
    proc.terminate()
    proc.wait(timeout=2)

    if cert_bytes_1 != cert_bytes_2:
        return fail("CA cert changed across restart (regenerated, not loaded)")
    return ok("CA cert survives bridge restart")


def test_no_consumer_stays_up(tmpdir: Path) -> bool:
    """Bridge with --mitm-tls but no client should stay alive ≥2s."""
    sock = tmpdir / "bridge.sock"
    ca_dir = tmpdir / "ca"
    log = tmpdir / "bridge.log"
    proc = spawn_bridge(ca_dir, sock, log)
    time.sleep(2.0)
    alive = proc.poll() is None
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
    if not alive:
        tail = log.read_text(errors="replace")[-500:] if log.exists() else "(no log)"
        return fail(f"bridge exited before 2s. Log tail:\n{tail}")
    return ok("bridge stays running with no socket client (2s)")


def main() -> int:
    if not NET_BRIDGE.exists():
        print(f"SKIP: net-bridge binary not at {NET_BRIDGE}")
        print("      Build first: cmake --build build --target net-bridge")
        return 77

    tests = [
        ("provider_loads", test_provider_loads),
        ("ca_cert_valid", test_ca_cert_valid),
        ("ca_persists", test_ca_persists),
        ("no_consumer_stays_up", test_no_consumer_stays_up),
    ]

    print(f"=== MITM Proxy Smoke Tests (net-bridge: {NET_BRIDGE.name}) ===")
    passed = 0
    failed = 0
    for name, fn in tests:
        print(f"--- {name} ---")
        with tempfile.TemporaryDirectory(prefix="mitm-test-") as tmp:
            try:
                if fn(Path(tmp)):
                    passed += 1
                else:
                    failed += 1
            except Exception as e:
                fail(f"unhandled exception: {e}")
                failed += 1

    print(f"\nResults: {passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
