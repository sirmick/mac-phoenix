# Test suite for mac-phoenix
#
# Run with: meson test -C build
# Verbose:  meson test -C build -v
# Specific: meson test -C build api_endpoints
# Skip slow: meson test -C build --suite api

# Find test scripts
test_dir = meson.current_source_dir()
test_rom = get_option('test_rom')
test_ppc_rom = get_option('test_ppc_rom')
test_ppc_cdrom = get_option('test_ppc_cdrom')

# --- Fast tests (no ROM / no boot required) ---

# API endpoint smoke test - verifies all endpoints return expected status codes
test('api_endpoints',
  find_program('bash'),
  args: [test_dir / 'test_api_endpoints.sh'],
  timeout: 30,
  is_parallel: false,
  suite: 'api',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# Config API test - verifies config round-trip, CDROM, bootdriver
test('config_api',
  find_program('bash'),
  args: [test_dir / 'test_config_api.sh', '18092'],
  timeout: 30,
  is_parallel: false,
  suite: 'api',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# ExtFS config test - verifies extfs config round-trip, CLI flags, backward compat
test('extfs',
  find_program('bash'),
  args: [test_dir / 'test_extfs.sh', '18098'],
  timeout: 60,
  is_parallel: false,
  suite: 'api',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# --- Boot tests (require ROM, longer timeout) ---
# These run sequentially to avoid port conflicts

# UAE interpreter (no JIT)
test('boot_uae_interp',
  find_program('bash'),
  args: [test_dir / 'test_boot_to_finder.sh', '--backend', 'uae', '--no-jit', '--timeout', '20', '--port', '18090'],
  timeout: 35,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# UAE with JIT compiler
test('boot_uae_jit',
  find_program('bash'),
  args: [test_dir / 'test_boot_to_finder.sh', '--backend', 'uae', '--jit', '--timeout', '20', '--port', '18090'],
  timeout: 35,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# Unicorn backend boots to Finder (slow - QEMU TCG JIT)
test('boot_unicorn',
  find_program('bash'),
  args: [test_dir / 'test_boot_to_finder.sh', '--backend', 'unicorn', '--timeout', '120', '--port', '18090'],
  timeout: 180,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# Command bridge API endpoints
test('command_bridge',
  find_program('bash'),
  args: [test_dir / 'test_command_bridge.sh'],
  timeout: 30,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# PPC KPX interpreter (no PPC JIT)
test('boot_ppc_interp',
  find_program('bash'),
  args: [test_dir / 'test_boot_ppc.sh', '--no-ppc-jit', '--timeout', '45', '--port', '18095'],
  timeout: 60,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_ppc_rom,
  },
)

# PPC KPX with dyngen JIT
test('boot_ppc_jit',
  find_program('bash'),
  args: [test_dir / 'test_boot_ppc.sh', '--ppc-jit', '--timeout', '45', '--port', '18095'],
  timeout: 60,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_ppc_rom,
  },
)

# PPC boot with webserver — validates boot phase tracking via /api/status
test('boot_ppc_api',
  find_program('bash'),
  args: [test_dir / 'test_boot_ppc.sh', '--timeout', '45', '--port', '18095', '--webserver'],
  timeout: 60,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_ppc_rom,
  },
)

# Mouse position API works after boot
test('mouse_position',
  find_program('bash'),
  args: [test_dir / 'test_mouse_position.sh', '--backend', 'uae', '--timeout', '15', '--port', '18090'],
  timeout: 30,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_rom,
  },
)
