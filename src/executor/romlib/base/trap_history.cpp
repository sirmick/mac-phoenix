#include <base/trap_history.h>

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
