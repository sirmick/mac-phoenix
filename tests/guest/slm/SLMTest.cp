/*
 * SLMTest.cp -- the Shared Library Manager's PowerPC side as a client
 * linked with the ASLM SDK's glue (LibraryManagerPPC.o) reaches it, the way
 * the TCP/IP control panel's PowerPC code does: InitLibraryManager and
 * CleanupLibraryManager, operator new and delete from the client's pool,
 * TRY/CATCH/Fail, the current client and global world, a TProcNotifier as
 * a death watcher. What Apple's "Shared Library Manager PPC" answers (the
 * G3, Mac OS 7.6.1) and what Executor's does (romlib/opentransport/
 * ot_slm_ppc.cpp; docs/executor/OpenTransport.md).
 *
 * One line per check, "PASS name" or "FAIL name: why"; "# ..." lines note
 * an answer; then "---", in Host:slm_results.txt (tests/test_guest_suite.sh
 * --results slm_results.txt). Built with MPW's MrCpp: tools/mpw/mpw build
 * tests/guest/slm -o tests/guest/slm.
 */

#include <Types.h>
#include <LowMem.h>
#include <stdio.h>
#include <string.h>

#include <LibraryManager.h>
#include <LibraryManagerUtilities.h>
#include <LibraryManagerClasses.h>
#include <GlobalNew.h>

static int failures;

static void pass(const char *name) { printf("PASS %s\n", name); }

static void fail(const char *name, const char *why, long code)
{
    printf("FAIL %s: %s (%ld)\n", name, why, code);
    failures++;
}

static void check(const char *name, int ok, const char *why, long code)
{
    if(ok)
        pass(name);
    else
        fail(name, why, code);
}

/* operator new and delete: the client's pool. */
static void test_memory()
{
    char *p = (char *)SLMNewOperator(100, NULL);
    check("new", p != NULL, "SLMNewOperator(100) answered nil", 0);
    if(!p)
        return;
    memset(p, 0x5A, 100);
    check("new_writable", p[0] == 0x5A && p[99] == 0x5A, "lost what was written", p[0]);
    SLMDeleteOperator(p);
    pass("delete");

    enum { kBlocks = 200 };
    static char *blocks[kBlocks];
    int ok = 1;
    for(int i = 0; i < kBlocks && ok; i++)
    {
        blocks[i] = (char *)SLMNewOperator(16 + 37 * i, NULL);
        if(!blocks[i])
            ok = 0;
        else
            memset(blocks[i], i, 16 + 37 * i);
    }
    check("new_many", ok, "an allocation answered nil", 0);
    for(int j = 0; ok && j < kBlocks; j++)
        if(blocks[j][0] != (char)j || blocks[j][15 + 37 * j] != (char)j)
        {
            fail("blocks_apart", "one block overwrote another", j);
            ok = 0;
        }
    if(ok)
        pass("blocks_apart");
    for(int k = 0; k < kBlocks; k++)
        if(blocks[k])
            SLMDeleteOperator(blocks[k]);
    pass("delete_many");
}

/* TRY/CATCH/Fail (LibraryManager.h): Fail goes to the innermost TRY. */
static void test_exceptions()
{
    long caught = 0;
    VOLATILE(caught);
    int reached = 0;
    VOLATILE(reached);
    TRY
    {
        Fail(-1234, NULL);
        reached = 1;
    }
    CATCH_ALL
    {
        caught = ErrorCode();
    }
    ENDTRY
    check("fail_caught", caught == -1234, "CATCH_ALL saw another error", caught);
    check("fail_jumps", !reached, "Fail returned", reached);

    caught = 0;
    int ran = 0;
    VOLATILE(ran);
    TRY
    {
        ran = 1;
    }
    CATCH_ALL
    {
        caught = 1;
    }
    ENDTRY
    check("no_fail", ran && !caught, "CATCH_ALL ran without a Fail", caught);

    caught = 0;
    TRY
    {
        TRY
        {
            Fail(-5, NULL);
        }
        CATCH_ALL
        {
            RERAISE;
        }
        ENDTRY
    }
    CATCH_ALL
    {
        caught = ErrorCode();
    }
    ENDTRY
    check("reraise", caught == -5, "the outer TRY saw another error", caught);

    static const char msg[] = "SLMTest";
    const char *got = NULL;
    VOLATILE(got);
    TRY
    {
        Fail(-7, msg);
    }
    CATCH_ALL
    {
        got = ErrorMessage();
    }
    ENDTRY
    check("fail_message", got == msg, "ErrorMessage() isn't Fail's", (long)got);

    caught = 0;
    TRY
    {
        Fail(-9, NULL);
    }
    CATCH(-8)
    {
        caught = 8;
    }
    CATCH(-9)
    {
        caught = 9;
    }
    CATCH_ALL
    {
        caught = 1;
    }
    ENDTRY
    check("catch_match", caught == 9, "MatchException picked another CATCH", caught);

    /* After all that, a Fail still reaches its TRY: nothing left over on
       the exception stack. */
    caught = 0;
    TRY
    {
        Fail(-11, NULL);
    }
    CATCH_ALL
    {
        caught = ErrorCode();
    }
    ENDTRY
    check("stack_clean", caught == -11, "a later Fail went elsewhere", caught);
}

extern "C" void notify(void *, EventCode, OSErrParm, void *) {}

/* The client and its global world. */
static void test_client()
{
    TLibraryManager *client = GetCurrentClient();
    check("current_client", client != NULL, "GetCurrentClient answered nil", 0);
    GlobalWorld world = ASLMGetCurrentGlobalWorld();
    printf("# global world %08lx, CurrentA5 %08lx\n", (long)world, (long)LMGetCurrentA5());
    check("world_is_a5", (long)world == (long)LMGetCurrentA5(),
          "the global world isn't CurrentA5", (long)world);
    GlobalWorld old = ASLMSetCurrentGlobalWorld(world);
    check("set_world", old == world, "SetCurrentGlobalWorld answered another", (long)old);

    /* The panel's death watcher. */
    TProcNotifier *n = new TProcNotifier(notify, (void *)0);
    check("notifier", n != NULL, "new TProcNotifier answered nil", 0);
    if(!n)
        return;
    check("install_watcher", InstallDeathWatcher(n), "InstallDeathWatcher answered false", 0);
    check("remove_watcher", RemoveDeathWatcher(n), "RemoveDeathWatcher answered false", 0);
    delete n;
    pass("notifier_deleted");
}

int main()
{
    freopen("Host:slm_results.txt", "w", stdout);
    OSErr err = InitLibraryManager(0, kCurrentZone, kNormalMemory);
    check("init", err == noErr, "InitLibraryManager", err);
    if(err == noErr)
    {
        test_memory();
        fflush(stdout);
        test_exceptions();
        fflush(stdout);
        test_client();
        CleanupLibraryManager();
        pass("cleanup");
        err = InitLibraryManager(0, kCurrentZone, kNormalMemory);
        check("init_again", err == noErr, "InitLibraryManager after CleanupLibraryManager", err);
        if(err == noErr)
        {
            char *p = (char *)SLMNewOperator(10, NULL);
            check("new_again", p != NULL, "SLMNewOperator answered nil", 0);
            SLMDeleteOperator(p);
            CleanupLibraryManager();
        }
    }
    printf("DONE %d\n---\n", failures);
    fclose(stdout);
    return 0;
}
