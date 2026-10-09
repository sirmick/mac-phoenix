# Musashi 68k core (Karl Stenerud, MIT), vendored from
# https://github.com/kstenerud/Musashi at 313ebf1b (v3.32). Configuration is
# by compile definitions (m68kconf.h guards every option with #ifndef).
# One local change, in m68k_in.c (marked "MacPhoenix"): on a 68040, line F
# with coprocessor ID 1/2 is the 040's FPU and CINV/CPUSH/PFLUSH/PTEST
# (cache and MMU instructions are no-ops); upstream lets its 68020/030
# cpDBcc/cpTRAPcc stubs claim some of those opcodes (CPUSHA skipped 4
# bytes and Mac OS bombed).
#
# m68kmake generates the opcode handlers (m68kops.c/.h) from m68k_in.c.
#
# Musashi's m68k_* names don't collide with UAE's at link time (UAE is C++,
# its names are mangled), but the two cores' headers must never meet in one
# translation unit.

add_executable(m68kmake m68kmake.c)
set_target_properties(m68kmake PROPERTIES C_STANDARD 99)

set(MUSASHI_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
add_custom_command(
    OUTPUT ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kops.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MUSASHI_GEN}
    COMMAND m68kmake ${MUSASHI_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/m68k_in.c
    DEPENDS m68kmake ${CMAKE_CURRENT_SOURCE_DIR}/m68k_in.c
    COMMENT "Generating Musashi opcode handlers"
    VERBATIM)

add_library(musashi STATIC
    m68kcpu.c
    m68kdasm.c
    softfloat/softfloat.c
    ${MUSASHI_GEN}/m68kops.c
)
target_include_directories(musashi
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${MUSASHI_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/softfloat)
target_compile_options(musashi PRIVATE -w)  # upstream code; its warnings are not ours to fix
target_compile_definitions(musashi PUBLIC
    # Host callbacks the facade registers at run time.
    M68K_ILLG_HAS_CALLBACK=M68K_OPT_ON
    # No guest MMU: every access goes straight to our memory callbacks.
    M68K_EMULATE_PMMU=M68K_OPT_OFF
)
set_target_properties(musashi PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE OFF)
target_link_libraries(musashi PRIVATE m)
