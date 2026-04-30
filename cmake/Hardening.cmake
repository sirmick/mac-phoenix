# Hardening.cmake — mirror the C/C++/linker flag set that
# debian/rules → dh_auto_build → dpkg-buildflags applies in the
# packaging pipeline. Default ON so a plain `cmake -B build` catches
# the same class of failures the .deb build sees (LTO undefined refs,
# format-string vulns, missing frame pointers, etc.). Set
# -DBUILD_HARDENED=OFF for fast dev iteration.
#
# Source of truth: `dpkg-buildflags --get {CXXFLAGS,LDFLAGS}` plus
# debian/rules' DEB_BUILD_MAINT_OPTIONS=hardening=+all.
#
# The KPX dyngen interpreter is exempt (target_compile_options-applied
# elsewhere) — its precompiled-machine-code headers ICE under -O2/LTO
# the same way they would under apt's flag set, so we don't lose
# coverage by carving them out.

option(BUILD_HARDENED "Apply Debian-style hardening flags + LTO" ON)

if(NOT BUILD_HARDENED)
    message(STATUS "BUILD_HARDENED=OFF — skipping debian-style hardening")
    return()
endif()

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckLinkerFlag OPTIONAL)

# Apply a flag to both C and CXX targets if the compiler accepts it.
function(_phoenix_try_flag flag)
    string(MAKE_C_IDENTIFIER "have_${flag}" var)
    check_c_compiler_flag("${flag}" ${var}_c)
    check_cxx_compiler_flag("${flag}" ${var}_cxx)
    if(${var}_c AND ${var}_cxx)
        add_compile_options("${flag}")
    else()
        message(STATUS "Hardening: skipping unsupported flag ${flag}")
    endif()
endfunction()

function(_phoenix_try_link_flag flag)
    string(MAKE_C_IDENTIFIER "have_link_${flag}" var)
    if(COMMAND check_linker_flag)
        check_linker_flag(C "${flag}" ${var})
    else()
        # CheckLinkerFlag was added in CMake 3.18; fall back to assuming
        # supported on platforms where the runner CMake is older.
        set(${var} TRUE)
    endif()
    if(${var})
        add_link_options("${flag}")
    else()
        message(STATUS "Hardening: skipping unsupported link flag ${flag}")
    endif()
endfunction()

# ── LTO ──────────────────────────────────────────────────────────────
# Whole-program optimization at link time. The reason for this whole
# module: LTO turns "static-lib reference resolved by --start-group"
# into "undefined reference" if the symbol's defining .o never gets
# pulled in. That's how the unicorn-ppc / kpx coupling slipped past
# local builds for so long.
_phoenix_try_flag("-flto=auto")
_phoenix_try_flag("-ffat-lto-objects")
_phoenix_try_link_flag("-flto=auto")

# ── Frame pointers ───────────────────────────────────────────────────
# Debian keeps these to make perf/profilers usable in production.
_phoenix_try_flag("-fno-omit-frame-pointer")
_phoenix_try_flag("-mno-omit-leaf-frame-pointer")

# ── Stack hardening ──────────────────────────────────────────────────
_phoenix_try_flag("-fstack-protector-strong")
_phoenix_try_flag("-fstack-clash-protection")

# ── Format-string security ───────────────────────────────────────────
# debian/rules strips -Werror=format-security because uae_cpu's
# upstream code has unfixable format-string warnings; we follow suit.
_phoenix_try_flag("-Wformat")
_phoenix_try_flag("-Wformat-security")

# ── libc fortification ───────────────────────────────────────────────
# _FORTIFY_SOURCE=3 needs at least -O1; CMake's Release/RelWithDebInfo
# already imply that. Defining unconditionally is safe — glibc's macro
# is a no-op when optimization is off.
add_compile_definitions(_FORTIFY_SOURCE=3)

# ── Control-flow integrity ───────────────────────────────────────────
# x86: Intel CET (CF_PROTECTION). arm64: pointer authentication +
# branch target identification. Each compiler probes the flag and
# silently drops if the host doesn't support it.
_phoenix_try_flag("-fcf-protection=full")
_phoenix_try_flag("-mbranch-protection=standard")

# ── Linker hardening ─────────────────────────────────────────────────
# RELRO + immediate binding so the GOT is read-only after relocation.
_phoenix_try_link_flag("-Wl,-z,relro")
_phoenix_try_link_flag("-Wl,-z,now")
# Internal symbol binding — a function can't be preempted by a
# LD_PRELOAD'd override (small perf win, more importantly closes a
# CFI hole).
_phoenix_try_link_flag("-Wl,-Bsymbolic-functions")

# ── Per-target exemptions ────────────────────────────────────────────
# UAE m68k interpreter and KPX PPC dyngen are SheepShaver/Basilisk-derived
# code with decades of voodoo: pointer-math hot loops, precompiled
# machine-code blobs, hand-rolled signal stack walks. We don't want
# LTO inlining across those TUs, FORTIFY_SOURCE inserting runtime
# bounds checks into the interpreter inner loop, or stack canaries
# perturbing the dyngen patch points. Apply this to a target via:
#     phoenix_exempt_from_hardening(target_name)
# after the target is defined.
function(phoenix_exempt_from_hardening target)
    if(NOT BUILD_HARDENED OR NOT TARGET ${target})
        return()
    endif()
    # Compile-side opt-outs.
    target_compile_options(${target} PRIVATE
        -fno-lto
        -fno-stack-protector
        -U_FORTIFY_SOURCE
    )
    # Re-define _FORTIFY_SOURCE=0 since target_compile_definitions doesn't
    # expose -U as a portable spelling. PRIVATE so the override doesn't
    # leak out through INTERFACE_COMPILE_DEFINITIONS.
    target_compile_definitions(${target} PRIVATE _FORTIFY_SOURCE=0)
    # Link-side: -fno-lto on link of the final exe (we can't selectively
    # disable LTO on a single archive, but we can flag the source so it
    # doesn't generate LTO IR objects that the final link needs to merge).
endfunction()

message(STATUS "Hardening: enabled (debian-style; opt-out: -DBUILD_HARDENED=OFF)")
