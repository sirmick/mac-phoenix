// Build-time scaffolding for net-bridge:
//   1. NET_BRIDGE_BUILD_DATE env var so main.rs can report when the binary
//      was compiled (matches the pattern the Retro68 BridgeAgent uses with
//      __DATE__ __TIME__).
//   2. Linker flags for the vendored wolfSSL static lib (built by
//      tools/build-wolfssl.sh, location given by WOLFSSL_DIR env var).
//      No fallback to a system-installed wolfSSL — distros generally don't
//      build it with --enable-sslv3, which is the whole point of vendoring.

use std::env;
use std::process::Command;

fn main() {
    // ---- BUILD_DATE -----------------------------------------------------
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

    // ---- vendored wolfSSL ----------------------------------------------
    // CMake-driven builds export WOLFSSL_DIR to the cargo invocation; for
    // standalone `cargo build` the dev sets it manually after running
    // tools/build-wolfssl.sh.
    println!("cargo:rerun-if-env-changed=WOLFSSL_DIR");
    if let Ok(dir) = env::var("WOLFSSL_DIR") {
        println!("cargo:rustc-link-search=native={}/lib", dir);
        println!("cargo:rustc-link-lib=static=wolfssl");
        // wolfSSL itself only needs libm + libpthread when built with our
        // configure flags; libc is implied. No OpenSSL dependency from the
        // wolfSSL side (we're not building wolfProvider).
        println!("cargo:rustc-link-lib=m");
        println!("cargo:rustc-link-lib=pthread");
        // Make WOLFSSL_INCLUDE available to FFI code (for include_dir!()
        // patterns later, if any).
        println!("cargo:rustc-env=WOLFSSL_INCLUDE={}/include", dir);
    } else {
        println!(
            "cargo:warning=WOLFSSL_DIR not set — net-bridge --mitm-tls will \
             fail to link. Run net-bridge/tools/build-wolfssl.sh, then \
             export WOLFSSL_DIR=net-bridge/vendor/wolfssl-legacy"
        );
    }

    // ---- general rerun triggers ----------------------------------------
    // Force a rebuild if any source changes (otherwise the date sticks
    // to whenever cargo last chose to re-run this script).
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rerun-if-changed=Cargo.toml");
}
