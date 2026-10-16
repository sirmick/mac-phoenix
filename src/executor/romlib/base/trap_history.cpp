#include <base/trap_history.h>

#include <vector>
#include <syn68k_public.h>

namespace
{
constexpr int SIZE = 1024;
struct Entry
{
    uint32_t pc;
    uint16_t trap;
};
Entry ring[SIZE];
unsigned long count;

/* Host-side moves of guest memory (BlockMove, the Memory Manager's block
   moves), for a crash that looks like data slid in place. */
constexpr int MOVES = 4096;
struct Move
{
    uint32_t dst, src, len;
    unsigned long trap_count; /* traps seen before it */
    const char *who;
};
Move moves[MOVES];
unsigned long move_count;
}

void Executor::trap_history_record(uint16_t trap, uint32_t pc)
{
    ring[count % SIZE] = { pc, trap };
    count++;
}

void Executor::trap_history_dump(FILE *out, int n)
{
    if(n > SIZE)
        n = SIZE;
    if((unsigned long)n > count)
        n = (int)count;
    fprintf(out, "last %d of %lu A-line traps (oldest first; trap word @ guest pc):\n", n, count);
    int col = 0;
    for(unsigned long i = count - n; i < count; i++)
    {
        const Entry &e = ring[i % SIZE];
        fprintf(out, "  $%04X@%08X", e.trap, e.pc);
        if(++col == 6)
        {
            fputc('\n', out);
            col = 0;
        }
    }
    if(col)
        fputc('\n', out);
}

static uint32_t guest_or_none(const void *p)
{
    return syn68k_is_guest_mapped((uintptr_t)p, 1) ? US_TO_SYN68K(p) : 0xFFFFFFFF;
}

void Executor::move_history_record(const void *dst, const void *src, uint32_t len, const char *who)
{
    moves[move_count % MOVES] = { guest_or_none(dst), guest_or_none(src), len, count, who };
    move_count++;
}

void Executor::move_history_dump(FILE *out, int n, uint32_t min_len, uint32_t below)
{
    unsigned long first = move_count > MOVES ? move_count - MOVES : 0;
    std::vector<unsigned long> picked;
    for(unsigned long i = move_count; i-- > first && (int)picked.size() < n;)
        if(moves[i % MOVES].len >= min_len && moves[i % MOVES].dst < below)
            picked.push_back(i);
    fprintf(out, "last %zu of %lu guest memory moves of %u bytes or more below $%X (dst <- src, length, at trap #; %lu traps so far):\n",
            picked.size(), move_count, min_len, below, count);
    for(auto it = picked.rbegin(); it != picked.rend(); ++it)
    {
        const Move &m = moves[*it % MOVES];
        fprintf(out, "  %08X <- %08X %6u %-12s #%lu\n", m.dst, m.src, m.len, m.who, m.trap_count);
    }
}
