/* Quick utility to find the offset of 'cpu' field in uc_struct */
#include <stdio.h>
#include <stddef.h>
#include "../external/unicorn/include/uc_priv.h"

int main() {
    printf("Offset of cpu field in uc_struct: %zu bytes\n",
           offsetof(struct uc_struct, cpu));
    return 0;
}
