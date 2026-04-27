// Emit a BUILD_DATE env var so main.rs can report when the binary was
// compiled (matches the pattern the Retro68 BridgeAgent uses with
// __DATE__ __TIME__). Re-runs whenever anything in src/ changes so the
// stamp stays fresh.
//
// Linker flags for the vendored OpenSSL 1.0.2u live in CMakeLists.txt
// (it sets OPENSSL_DIR + OPENSSL_STATIC env on the cargo invocation;
// the openssl-sys crate's build.rs picks them up).

use std::process::Command;

fn main() {
    // Format: "Apr 24 2026 11:47:03" — same shape as C's __DATE__ __TIME__
    // concatenated, so downstream code that parses either source is happy.
    let out = Command::new("date")
        .arg("+%b %d %Y %H:%M:%S")
        .output()
        .ok()
        .and_then(|o| String::from_utf8(o.stdout).ok())
        .unwrap_or_else(|| "unknown".to_string());
    let date = out.trim();
    println!("cargo:rustc-env=NET_BRIDGE_BUILD_DATE={}", date);

    // Force a rebuild if any source changes (otherwise the date sticks
    // to whenever cargo last chose to re-run this script).
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=Cargo.toml");
}
