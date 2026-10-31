# Per-worktree test ports (tests/lib/ports.py, docs/Testing.md "Ports"):
#   source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT")
# maps a nominal port (18000-18999) to this checkout's range; other ports
# are left alone. The base is worked out once and exported, so scripts this
# one starts reuse it.
MACPHOENIX_PORT_BASE=$(python3 "$(dirname "${BASH_SOURCE[0]}")/ports.py" base) || exit 1
export MACPHOENIX_PORT_BASE
mp_port() {
    if (( $1 >= 18000 && $1 < 19000 )); then
        echo $((MACPHOENIX_PORT_BASE + $1 - 18000))
    else
        echo "$1"
    fi
}
