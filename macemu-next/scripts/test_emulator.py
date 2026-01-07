#!/usr/bin/env python3
"""
Test framework for macemu-next web interface and emulator.

Usage:
    ./scripts/test_emulator.py [--timeout SECONDS] [--rom PATH]

This script:
1. Starts the macemu-next emulator process
2. Waits for the web server to be ready
3. Tests the API endpoints (status, config load/save, start emulator)
4. Monitors logs for errors and execution statistics
5. Reports results and shuts down cleanly
"""

import subprocess
import time
import requests
import argparse
import sys
import os
import signal
import json
from pathlib import Path


class MacEmuTester:
    def __init__(self, rom_path, timeout=10, backend="unicorn"):
        self.rom_path = rom_path
        self.timeout = timeout
        self.backend = backend
        self.process = None
        self.base_url = "http://localhost:8000"
        self.logs = []

    def start_emulator(self):
        """Start the macemu-next process."""
        print(f"[TEST] Starting emulator (backend={self.backend}, timeout={self.timeout}s)...")

        env = os.environ.copy()
        env["EMULATOR_TIMEOUT"] = str(self.timeout)
        env["CPU_BACKEND"] = self.backend

        build_path = Path(__file__).parent.parent / "build" / "macemu-next"

        self.process = subprocess.Popen(
            [str(build_path), self.rom_path],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1  # Line buffered
        )

        print(f"[TEST] Process started (PID: {self.process.pid})")

    def wait_for_webserver(self, max_wait=10):
        """Wait for the HTTP server to be ready."""
        print(f"[TEST] Waiting for web server to start (max {max_wait}s)...")

        start_time = time.time()
        while time.time() - start_time < max_wait:
            try:
                response = requests.get(f"{self.base_url}/api/status", timeout=1)
                if response.status_code == 200:
                    print("[TEST] ✓ Web server is ready")
                    return True
            except requests.exceptions.RequestException:
                pass
            time.sleep(0.5)

        print("[TEST] ✗ Web server failed to start")
        return False

    def test_api_status(self):
        """Test the /api/status endpoint."""
        print("[TEST] Testing /api/status endpoint...")
        try:
            response = requests.get(f"{self.base_url}/api/status", timeout=2)
            if response.status_code == 200:
                status = response.json()
                print(f"[TEST] ✓ Status API responded: {json.dumps(status, indent=2)}")
                return True, status
            else:
                print(f"[TEST] ✗ Status API returned {response.status_code}")
                return False, None
        except Exception as e:
            print(f"[TEST] ✗ Status API error: {e}")
            return False, None

    def test_api_config_get(self):
        """Test GET /api/config endpoint."""
        print("[TEST] Testing GET /api/config endpoint...")
        try:
            response = requests.get(f"{self.base_url}/api/config", timeout=2)
            if response.status_code == 200:
                config = response.json()
                print(f"[TEST] ✓ Config GET responded: {json.dumps(config, indent=2)}")
                return True, config
            else:
                print(f"[TEST] ✗ Config GET returned {response.status_code}")
                return False, None
        except Exception as e:
            print(f"[TEST] ✗ Config GET error: {e}")
            return False, None

    def test_api_config_post(self, config_updates):
        """Test POST /api/config endpoint."""
        print(f"[TEST] Testing POST /api/config with updates: {config_updates}")
        try:
            response = requests.post(
                f"{self.base_url}/api/config",
                json=config_updates,
                timeout=2
            )
            if response.status_code == 200:
                result = response.json()
                print(f"[TEST] ✓ Config POST succeeded: {result}")
                return True, result
            else:
                print(f"[TEST] ✗ Config POST returned {response.status_code}: {response.text}")
                return False, None
        except Exception as e:
            print(f"[TEST] ✗ Config POST error: {e}")
            return False, None

    def test_api_start(self):
        """Test POST /api/emulator/start endpoint."""
        print("[TEST] Testing POST /api/emulator/start endpoint...")
        try:
            response = requests.post(f"{self.base_url}/api/emulator/start", timeout=2)
            if response.status_code == 200:
                result = response.json()
                print(f"[TEST] ✓ Start API succeeded: {result}")
                return True, result
            else:
                print(f"[TEST] ✗ Start API returned {response.status_code}: {response.text}")
                return False, None
        except Exception as e:
            print(f"[TEST] ✗ Start API error: {e}")
            return False, None

    def collect_logs(self):
        """Read available output from the emulator process."""
        if not self.process:
            return

        try:
            while True:
                line = self.process.stdout.readline()
                if not line:
                    break
                self.logs.append(line.rstrip())
        except:
            pass

    def analyze_logs(self):
        """Analyze collected logs for important information."""
        print("\n[TEST] === Log Analysis ===")

        # Collect any remaining logs
        self.collect_logs()

        # Extract key statistics
        blocks_executed = 0
        instructions_executed = 0
        errors = []
        warnings = []

        for line in self.logs:
            if "Total blocks executed:" in line:
                try:
                    blocks_executed = int(line.split(":")[-1].strip())
                except:
                    pass
            elif "Total instructions:" in line:
                try:
                    instructions_executed = int(line.split(":")[-1].strip())
                except:
                    pass
            elif "ERROR" in line or "error" in line.lower():
                errors.append(line)
            elif "WARNING" in line or "warning" in line.lower():
                warnings.append(line)

        print(f"[TEST] Blocks executed: {blocks_executed}")
        print(f"[TEST] Instructions executed: {instructions_executed}")
        print(f"[TEST] Errors found: {len(errors)}")
        print(f"[TEST] Warnings found: {len(warnings)}")

        if errors:
            print("\n[TEST] === Errors ===")
            for error in errors[:10]:  # Show first 10
                print(f"  {error}")

        if warnings:
            print("\n[TEST] === Warnings (first 5) ===")
            for warning in warnings[:5]:
                print(f"  {warning}")

        return {
            "blocks_executed": blocks_executed,
            "instructions_executed": instructions_executed,
            "errors": len(errors),
            "warnings": len(warnings)
        }

    def shutdown(self):
        """Gracefully shut down the emulator."""
        print("\n[TEST] Shutting down emulator...")
        if self.process:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=5)
                print("[TEST] ✓ Process terminated cleanly")
            except subprocess.TimeoutExpired:
                print("[TEST] ! Process didn't respond to SIGINT, killing...")
                self.process.kill()
                self.process.wait()

    def save_logs(self, filepath):
        """Save all collected logs to a file."""
        with open(filepath, 'w') as f:
            f.write('\n'.join(self.logs))
        print(f"[TEST] Logs saved to {filepath}")

    def run_full_test(self):
        """Run the complete test suite."""
        print("=" * 60)
        print("macemu-next Test Framework")
        print("=" * 60)

        results = {
            "webserver_ready": False,
            "api_status": False,
            "api_config_get": False,
            "api_config_post": False,
            "api_start": False,
            "execution_stats": {}
        }

        try:
            # Start emulator
            self.start_emulator()

            # Wait for web server
            results["webserver_ready"] = self.wait_for_webserver()

            if results["webserver_ready"]:
                # Test API endpoints
                results["api_status"], _ = self.test_api_status()
                results["api_config_get"], config = self.test_api_config_get()

                # Try modifying config (example: change display settings)
                if config:
                    test_updates = {
                        "display": {
                            "width": 800,
                            "height": 600
                        }
                    }
                    results["api_config_post"], _ = self.test_api_config_post(test_updates)

                # Try starting emulator
                results["api_start"], _ = self.test_api_start()

                # Wait a bit for execution
                print(f"\n[TEST] Waiting {self.timeout}s for emulator to run...")
                time.sleep(self.timeout + 1)

            # Collect and analyze logs
            results["execution_stats"] = self.analyze_logs()

        finally:
            self.shutdown()

        # Print summary
        print("\n" + "=" * 60)
        print("TEST SUMMARY")
        print("=" * 60)
        print(f"Web server ready:     {'✓' if results['webserver_ready'] else '✗'}")
        print(f"API /status:          {'✓' if results['api_status'] else '✗'}")
        print(f"API GET /config:      {'✓' if results['api_config_get'] else '✗'}")
        print(f"API POST /config:     {'✓' if results['api_config_post'] else '✗'}")
        print(f"API POST /start:      {'✓' if results['api_start'] else '✗'}")
        print(f"\nBlocks executed:      {results['execution_stats'].get('blocks_executed', 0)}")
        print(f"Instructions:         {results['execution_stats'].get('instructions_executed', 0)}")
        print(f"Errors:               {results['execution_stats'].get('errors', 0)}")
        print(f"Warnings:             {results['execution_stats'].get('warnings', 0)}")

        # Return exit code
        all_passed = (
            results["webserver_ready"] and
            results["api_status"] and
            results["execution_stats"].get("errors", 0) == 0
        )

        return 0 if all_passed else 1


def main():
    parser = argparse.ArgumentParser(description="Test macemu-next emulator")
    parser.add_argument(
        "--rom",
        default=os.path.expanduser("~/quadra.rom"),
        help="Path to ROM file (default: ~/quadra.rom)"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=5,
        help="Emulator timeout in seconds (default: 5)"
    )
    parser.add_argument(
        "--backend",
        choices=["unicorn", "uae"],
        default="unicorn",
        help="CPU backend to use (default: unicorn)"
    )
    parser.add_argument(
        "--save-logs",
        metavar="FILE",
        help="Save logs to file"
    )

    args = parser.parse_args()

    # Check ROM exists
    if not os.path.exists(args.rom):
        print(f"ERROR: ROM file not found: {args.rom}")
        return 1

    # Run tests
    tester = MacEmuTester(args.rom, timeout=args.timeout, backend=args.backend)
    exit_code = tester.run_full_test()

    # Save logs if requested
    if args.save_logs:
        tester.save_logs(args.save_logs)

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
