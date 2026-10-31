# Per-worktree test ports (tests/lib/ports.py, docs/Testing.md "Ports"):
#   source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1
# maps a nominal port (18000-18999) to this checkout's range; other ports
# are left alone. A port something already listens on fails at once, naming
# it (a session left over from a killed run would otherwise hold up the
# test until its timeout). The base is worked out once and exported, so
# scripts this one starts reuse it.
MACPHOENIX_PORT_BASE=$(python3 "$(dirname "${BASH_SOURCE[0]}")/ports.py" base) || exit 1
export MACPHOENIX_PORT_BASE
mp_port() {
    local p=$1 who
    (( p >= 18000 && p < 19000 )) && p=$((MACPHOENIX_PORT_BASE + p - 18000))
    who=$(ss -ltnpH "sport = :$p" 2>/dev/null)
    if [[ -n "$who" ]]; then
        local pid; pid=$(grep -o 'pid=[0-9]*' <<< "$who" | head -1 | cut -d= -f2)
        echo "FAIL: port $p is in use${pid:+ by pid $pid: $(tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null | cut -c1-200)}" >&2
        return 1
    fi
    echo "$p"
}
