/* load_preflight_wine.c — lab loader for the Wine/Proton line (plan §9, wine-proton-port).
 * Loads ffx-hooks.dll in a plain process with no FFX.exe present:
 * exercises DllMain, FF10H* exports, the 2s install-delay worker
 * (expected path: "WARN FFX.exe base not found" -> clean return,
 * dllmain.cpp:13071), and process detach. Not part of the shipped
 * runtime; lab tooling only. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "ffx-hooks.dll";
    const char *mode = argc > 2 ? argv[2] : "exit";
    if (strcmp(mode, "exit") != 0 && strcmp(mode, "free") != 0) {
        printf("MODE FAIL expected exit|free, got=%s\n", mode);
        return 2;
    }
    int free_mode = strcmp(mode, "free") == 0;
    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("LOAD FAIL err=%lu\n", (unsigned long)GetLastError()); return 1; }
    printf("LOAD OK h=%p\n", (void *)h);

    typedef const char *(*fn_t)(void);
    fn_t n = (fn_t)GetProcAddress(h, "FF10HgetName");
    fn_t v = (fn_t)GetProcAddress(h, "FF10HgetVer");
    printf("FF10HgetName: %s\n", n ? n() : "(MISSING)");
    printf("FF10HgetVer : %s\n", v ? v() : "(MISSING)");

    printf("sleeping 6s (install delay is 2s)...\n");
    fflush(stdout);
    Sleep(6000);
    if (!free_mode) {
        /* exit mode: mimic the real game — DLL stays loaded, process exit
         * drives DLL_PROCESS_DETACH via ExitProcess. */
        printf("exiting without FreeLibrary (ExitProcess path)...\n");
        fflush(stdout);
        return 0;
    }
    printf("freeing (unsupported for ffx-hooks; diagnostic mode only)...\n");
    fflush(stdout);
    BOOL ok = FreeLibrary(h);
    printf("FreeLibrary: %d err=%lu\n", (int)ok, (unsigned long)GetLastError());
    Sleep(500);
    return 0;
}
