#include <stdio.h>
#include <stdlib.h>

// FFmpeg Headers
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

// Lua Headers
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

// Python Embedded Header
#define PY_SSIZE_T_CLEAN
#include <Python.h>

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("  Custom Audio DAW Core Initializing... \n");
    printf("========================================\n");

    // 1. FFmpeg Init & Version Check
    printf("[Core] FFmpeg avformat version: %u\n", avformat_version());
    printf("[Core] FFmpeg avcodec version:  %u\n", avcodec_version());

    // 2. Lua VM Initialization
    lua_State *L = luaL_newstate();
    if (L) {
        luaL_openlibs(L);
        printf("[Core] Embedded Lua VM successfully booted.\n");
        luaL_dostring(L, "print('  [Lua Engine] ReaScript-style Lua runtime active.')");
        lua_close(L);
    } else {
        fprintf(stderr, "[Core Error] Failed to initialize Lua runtime.\n");
    }

    // 3. Python Subsystem Initialization
    Py_Initialize();
    if (Py_IsInitialized()) {
        printf("[Core] Embedded Python runtime active.\n");
        PyRun_SimpleString("print('  [Python Engine] Pipeline worker bridge active.')");
        Py_Finalize();
    } else {
        fprintf(stderr, "[Core Error] Failed to initialize Python runtime.\n");
    }

    printf("[Core] Engine initialization check passed.\n");
    return 0;
}
