/* hello.c — the MPW toolchain test's tool (tests/test_mpw_toolchain.sh):
   arguments, stdio, a file round trip and the exit status. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    FILE *f;
    char line[64];
    int i;

    printf("hello from MPW");
    for(i = 1; i < argc; i++)
        printf(" [%s]", argv[i]);
    printf("\n");

    f = fopen("hello.out", "w");
    if(!f)
        return 2;
    fprintf(f, "written by hello\n");
    fclose(f);
    f = fopen("hello.out", "r");
    if(!f || !fgets(line, sizeof line, f) || strcmp(line, "written by hello\n"))
        return 3;
    fclose(f);
    printf("file round trip ok\n");
    return argc > 1 && !strcmp(argv[1], "fail") ? 7 : 0;
}
