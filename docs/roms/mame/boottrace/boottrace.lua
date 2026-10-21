-- MAME autoboot script for boot traces (see README.md next to it).
-- Logs hardware accesses (I/O space $50xxxxxx, NuBus slot space $Fxxxxxxx)
-- with the CPU's PC and emulated time, dumps low memory at boot milestones,
-- and exits a few seconds after the Finder is up (or at BT_MAXSECS).
--
-- Env: BT_OUT (output dir, required), BT_MAXSECS (default 120),
--      BT_IOMAX (I/O lines to log, default 400000),
--      BT_FINDER_EXTRA (seconds to run after the Finder, default 8),
--      BT_SLOTALL=1 (log all slot-space reads, not only each slot's top 64K)
local out = assert(os.getenv("BT_OUT"), "BT_OUT not set")
local maxsecs = tonumber(os.getenv("BT_MAXSECS") or "120")
local iomax = tonumber(os.getenv("BT_IOMAX") or "400000")
local finder_extra = tonumber(os.getenv("BT_FINDER_EXTRA") or "8")

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local pcst = cpu.state["CURPC"]

local io = io
local hw = io.open(out .. "/hw.raw.tsv", "w")
hw:write("# time\tpc\trw\taddr\tdata\tmask\trepeat\n")
local nlines = 0
local last = nil      -- {pc, rw, addr, data, mask, time}
local lastrep = 0
local counts = {}     -- (page) -> count, for everything incl. what isn't logged
local seen = {}       -- exact (pc,rw,addr,data) -> times logged
local seen2 = {}      -- (pc,rw,addr) -> times logged
local uniq = {}       -- (pc,rw,addr) -> {first time, first data, count, last data}
local uniqkeys = {}

local function now() return manager.machine.time:as_double() end

local function flush_last()
    if last then
        hw:write(string.format("%.6f\t%08x\t%s\t%08x\t%08x\t%08x\t%d\n",
            last[6], last[1], last[2], last[3], last[4], last[5], lastrep))
        nlines = nlines + 1
        last = nil
        lastrep = 0
    end
end

-- Sequential log (consecutive repeats folded), but each exact
-- (pc,rw,addr,data) at most 8 times and each (pc,rw,addr) at most 64
-- times, so polling loops don't drown it; plus a table of every distinct
-- (pc,rw,addr) with its first time/value and count (hw.unique.tsv).
local function log(rw, addr, data, mask)
    local pc = pcst.value
    local page = addr & 0xfffff000
    counts[page] = (counts[page] or 0) + 1
    local k2 = string.format("%08x %s %08x", pc, rw, addr)
    local u = uniq[k2]
    if u then u[3] = u[3] + 1; u[4] = data
    else uniq[k2] = {now(), data, 1, data, mask}; uniqkeys[#uniqkeys + 1] = k2 end
    if last and last[1] == pc and last[2] == rw and last[3] == addr and last[4] == data then
        lastrep = lastrep + 1
        return
    end
    if nlines >= iomax then return end
    local k = k2 .. string.format(" %08x", data)
    local n = seen[k] or 0
    local n2 = seen2[k2] or 0
    if n >= 8 or n2 >= 64 then flush_last(); return end
    seen[k] = n + 1; seen2[k2] = n2 + 1
    flush_last()
    last = {pc, rw, addr, data, mask, now()}
    lastrep = 1
end

local taps = {}
-- I/O: VIA1/VIA2/SCC/SCSI/ASC/IWM/RBV, all mirrors
taps[#taps + 1] = space:install_read_tap(0x50000000, 0x50ffffff, "bt_ior",
    function(off, data, mask) log("R", off, data, mask) end)
taps[#taps + 1] = space:install_write_tap(0x50000000, 0x50ffffff, "bt_iow",
    function(off, data, mask) log("W", off, data, mask) end)
-- NuBus / slot space: only the declaration-ROM end of each slot ($sFFxxxx)
-- and super-slot probes are interesting; frame-buffer traffic is only counted.
local slotall = os.getenv("BT_SLOTALL") == "1"   -- log every slot-space read
local function slotlog(rw, off, data, mask)
    if slotall or (off & 0x00ff0000) == 0x00ff0000 then
        log(rw, off, data, mask)
    else
        local page = off & 0xfff00000
        counts[page] = (counts[page] or 0) + 1
    end
end
taps[#taps + 1] = space:install_read_tap(0xf0000000, 0xffffffff, "bt_slotr",
    function(off, data, mask) slotlog("R", off, data, mask) end)
taps[#taps + 1] = space:install_read_tap(0x90000000, 0xefffffff, "bt_superr",
    function(off, data, mask) log("R", off, data, mask) end)

-- low memory
local function dump_lowmem(name)
    local f = io.open(out .. "/lowmem-" .. name .. ".bin", "wb")
    local t = {}
    for a = 0, 0x2fff do t[#t + 1] = string.char(space:readv_u8(a)) end
    f:write(table.concat(t))
    f:close()
    local m = io.open(out .. "/milestones.tsv", "a")
    m:write(string.format("%.6f\t%s\t%08x\n", now(), name, pcst.value))
    m:close()
end

local function pstr(a)
    local n = space:readv_u8(a)
    if n == 0 or n > 31 then return "" end
    local s = ""
    for i = 1, n do s = s .. string.char(space:readv_u8(a + i)) end
    return s
end

local state = {}
-- ROM-milestone low-memory dumps come from the CPU tracer (MACTRACE_SNAP)
local finder_at = nil
local last_ap = ""
emu.register_frame_done(function()
    local t = now()
    local ap = pstr(0x910)
    if ap ~= last_ap then
        local m = io.open(out .. "/milestones.tsv", "a")
        m:write(string.format("%.6f\tCurApName=%s\t%08x\n", t, ap, pcst.value))
        m:close()
        last_ap = ap
    end
    if not finder_at and ap == "Finder" then finder_at = t end
    if finder_at and not state.finder and t > finder_at + finder_extra then
        state.finder = true
        dump_lowmem("finder")
        manager.machine.video:snapshot()
    end
    if state.finder or t > maxsecs then
        if not state.finder then dump_lowmem("timeout"); manager.machine.video:snapshot() end
        flush_last()
        hw:close()
        local c = io.open(out .. "/hw.pagecounts.tsv", "w")
        local keys = {}
        for k in pairs(counts) do keys[#keys + 1] = k end
        table.sort(keys)
        for _, k in ipairs(keys) do c:write(string.format("%08x\t%d\n", k, counts[k])) end
        c:close()
        local q = io.open(out .. "/hw.unique.tsv", "w")
        q:write("# pc rw addr\tfirst_time\tfirst_data\tcount\tlast_data\tmask\n")
        for _, k in ipairs(uniqkeys) do
            local u = uniq[k]
            q:write(string.format("%s\t%.6f\t%08x\t%d\t%08x\t%08x\n", k, u[1], u[2], u[3], u[4], u[5]))
        end
        q:close()
        for _, tp in ipairs(taps) do tp:remove() end
        manager.machine:exit()
    end
end)
