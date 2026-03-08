# Test suite for mac-phoenix
#
# Run with: meson test -C build
# Verbose:  meson test -C build -v
# Specific: meson test -C build api_endpoints
# Skip slow: meson test -C build --suite api

# Find test scripts
test_dir = meson.current_source_dir()
test_rom = get_option('test_rom')

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

# --- Boot tests (require ROM, longer timeout) ---
# These run sequentially to avoid port conflicts

# UAE backend boots to Finder
test('boot_uae',
  find_program('bash'),
  args: [test_dir / 'test_boot_to_finder.sh', '--backend', 'uae', '--timeout', '15', '--port', '18090'],
  timeout: 30,
  is_parallel: false,
  suite: 'boot',
  env: {
    'MACEMU_ROM': test_rom,
  },
)

# Unicorn backend boots to Finder (slow - JIT compilation)
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
