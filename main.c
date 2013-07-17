/*
 * Astra Main App
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#include <astra.h>

#include <signal.h>
#include <sys/stat.h>
#include <setjmp.h>

#include "config.h"

static jmp_buf main_loop;
static volatile bool asc_core_loop_alive;

void astra_exit(void)
{
    longjmp(main_loop, 1);
}

void astra_abort(void)
{
    asc_log_error("[main] abort execution. Lua backtrace:");

    lua_Debug ar;
    int level = 1;
    while(lua_getstack(lua, level, &ar))
    {
        lua_getinfo(lua, "nSl", &ar);
        asc_log_error("[main] %d: %s:%d -- %s [%s]"
                      , level, ar.short_src, ar.currentline
                      , (ar.name) ? ar.name : "<unknown>"
                      , ar.what);
        ++level;
    }

    abort();
}

static void signal_handler(int signum)
{
#ifndef _WIN32
    if(signum == SIGHUP)
    {
        asc_log_hup();
        return;
    }
#else
    (void)signum;
#endif
    asc_core_loop_alive = false;
}

static void astra_init(int argc, const char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#ifndef _WIN32
    signal(SIGHUP, signal_handler);
    signal(SIGQUIT, signal_handler);
#endif

    ASC_INIT();
    asc_core_loop_alive = true;

    lua = luaL_newstate();
    luaL_openlibs(lua);

    for(int i = 0; astra_mods[i]; i++)
        astra_mods[i](lua);

    /* argv table */
    lua_newtable(lua);
    for(int i = 0; i < argc; i++)
    {
        lua_pushinteger(lua, i + 1);
        lua_pushstring(lua, argv[i]);
        lua_settable(lua, -3);
    }
    lua_setglobal(lua, "argv");

    /* change package.path */
    lua_getglobal(lua, "package");
    lua_pushfstring(lua, "./?.lua;/etc/astra/scripts-%d.%d/?.lua"
                    , ASTRA_VERSION_MAJOR, ASTRA_VERSION_MINOR);
    lua_setfield(lua, -2, "path");
    lua_pushstring(lua, "");
    lua_setfield(lua, -2, "cpath");
    lua_pop(lua, 1);
}

void astra_do_file(int argc, const char **argv, const char *filename)
{
    if(filename[0] == '-' && filename[1] == '\0')
        filename = NULL;
    else if(!access(filename, R_OK))
        ;
    else
    {
        printf("Error: initial script isn't found [%s]\n", strerror(errno));
        return;
    }

    astra_init(argc, argv);

    if(!setjmp(main_loop))
    {
        if(luaL_dofile(lua, filename))
            luaL_error(lua, "[main] %s", lua_tostring(lua, -1));

        ASC_LOOP(asc_core_loop_alive);
    }

    lua_close(lua);
    ASC_DESTROY();
}

void astra_do_text(int argc, const char **argv, const char *text, size_t size)
{
    astra_init(argc, argv);

    if(!setjmp(main_loop))
    {
        if(luaL_loadbuffer(lua, text, size, "=inscript") || lua_pcall(lua, 0, LUA_MULTRET, 0))
            luaL_error(lua, "[main] %s", lua_tostring(lua, -1));

        ASC_LOOP(asc_core_loop_alive);
    }

    lua_close(lua);
    ASC_DESTROY();
}

#ifndef ASTRA_SHELL
int main(int argc, const char **argv)
{
    if(argc < 2)
    {
        printf("Astra " ASTRA_VERSION_STR "\n"
               "Usage: %s script [argv]\n"
               , argv[0]);
        return 1;
    }

    /* 2 - skip app and script names */
    astra_do_file(argc - 2, argv + 2, argv[1]);

    return 0;
}
#endif /* ! ASTRA_SHELL */
