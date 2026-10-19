-- MAME autoboot script: after LOWMEM_SECS emulated seconds (default 40),
-- save a snapshot and print the low-memory globals we compare against
-- MacPhoenix, then exit. Used as:
--   ./bin/maciici ... -autoboot_script lowmem.lua
local secs = tonumber(os.getenv("LOWMEM_SECS") or "40")
local done = false

local function pstr(mem, a)
    local n = mem:readv_u8(a)
    if n > 31 then return string.format("<len %d>", n) end
    local s = ""
    for i = 1, n do s = s .. string.char(mem:readv_u8(a + i)) end
    return s
end

emu.register_frame_done(function()
    if done or manager.machine.time.seconds < secs then return end
    done = true
    local mem = manager.machine.devices[":maincpu"].spaces["program"]
    local function w(a) local v = mem:readv_u16(a); if v >= 0x8000 then v = v - 0x10000 end; return v end
    print(string.format("BootDrive  %d", w(0x210)))
    print(string.format("SFSaveDisk %d", w(0x214)))
    print(string.format("SysVersion %04x", mem:readv_u16(0x15a)))
    print(string.format("ROM85      %04x", mem:readv_u16(0x28e)))
    print(string.format("MMU32Bit   %02x", mem:readv_u8(0xcb2)))
    print(string.format("BtDskRfn   %d", w(0xb34)))
    print("FinderName " .. pstr(mem, 0x2e0))
    print("CurApName  " .. pstr(mem, 0x910))
    manager.machine.video:snapshot()
    manager.machine:exit()
end)
