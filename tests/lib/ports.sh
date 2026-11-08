# Test ports (tests/lib/ports.py, docs/Testing.md "Ports"):
#   source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1
# gives a nominal port (18000-18999) a free port of its own, held until this
# script exits (or, with MACPHOENIX_PORT_BASE set, maps it into that range);
# other ports are left alone. A port something already listens on fails at
# once, naming it (a session left over from a killed run would otherwise
# hold up the test until its timeout).
MP_PORTS_PY="$(dirname "${BASH_SOURCE[0]}")/ports.py"
python3 "$MP_PORTS_PY" base > /dev/null || exit 1   # a bad MACPHOENIX_PORT_BASE
mp_port() {
    local p=$1 who
    if (( p >= 18000 && p < 19000 )); then
        if [[ -n "${MACPHOENIX_PORT_BASE:-}" ]]; then
            p=$((MACPHOENIX_PORT_BASE + p - 18000))
        else
            # claimed from the pool, held until this script ($$) exits
            python3 "$MP_PORTS_PY" port "$p" --pid $$
            return
        fi
    fi
    who=$(ss -ltnpH "sport = :$p" 2>/dev/null)
    if [[ -n "$who" ]]; then
        local pid; pid=$(grep -o 'pid=[0-9]*' <<< "$who" | head -1 | cut -d= -f2)
        echo "FAIL: port $p is in use${pid:+ by pid $pid: $(tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null | cut -c1-200)}" >&2
        return 1
    fi
    echo "$p"
}
