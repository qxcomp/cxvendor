/* rtdb/tests/test_luajit.c - LuaJIT 2.1 C API 动态封装定向测试 */
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------- 类型与常量（不引入目标库头文件） ---------- */
typedef struct lua_State lua_State;
typedef struct luaL_Reg luaL_Reg;
typedef double lua_Number;
#define LUAL_BUFFERSIZE (BUFSIZ > 16384 ? 8192 : BUFSIZ)
typedef struct luaL_Buffer {
    char *p;
    int lvl;
    lua_State *L;
    char buffer[LUAL_BUFFERSIZE];
} luaL_Buffer;
typedef ptrdiff_t lua_Integer;
#define LUA_GLOBALSINDEX (-10002)
#define LUA_REGISTRYINDEX (-10000)
#define LUA_TNIL 0
#define LUA_TNUMBER 3
#define LUA_TSTRING 4

/* ---------- 动态层接口 ---------- */
extern int luajit_dyn_init(const char *);
extern int luajit_libok(void);
extern void luajit_dyn_cleanup(void);
extern int luajit_dyn_has_symbol(const char *);

/* ---------- 本测试直接调用的原生声明 ---------- */
extern const lua_Number *lua_version(lua_State *);
extern lua_State *luaL_newstate(void);
extern void luaL_openlibs(lua_State *);
extern void lua_close(lua_State *);
extern int luaL_loadstring(lua_State *, const char *);
extern int lua_pcall(lua_State *, int, int, int);
extern void lua_getfield(lua_State *, int, const char *);
extern void lua_settop(lua_State *, int);
extern int lua_gettop(lua_State *);
extern lua_Integer lua_tointegerx(lua_State *, int, int *);
extern lua_Number lua_tonumberx(lua_State *, int, int *);
extern const char *lua_tolstring(lua_State *, int, size_t *);
extern int lua_type(lua_State *, int);
extern void lua_pushinteger(lua_State *, lua_Integer);
extern void lua_pushnumber(lua_State *, lua_Number);
extern void lua_pushstring(lua_State *, const char *);
extern void lua_createtable(lua_State *, int, int);
extern void lua_setfield(lua_State *, int, const char *);
extern void lua_rawgeti(lua_State *, int, int);
extern void lua_rawseti(lua_State *, int, int);
extern int luaL_loadbuffer(lua_State *, const char *, size_t, const char *);
extern int lua_gc(lua_State *, int, int);
extern const char *lua_pushfstring(lua_State *, const char *, ...);
extern void luaL_buffinit(lua_State *, luaL_Buffer *);
extern char *luaL_prepbuffer(luaL_Buffer *);
extern void luaL_addstring(luaL_Buffer *, const char *);
extern void luaL_pushresult(luaL_Buffer *);

int main(int argc, char **argv) {
    int errs = 0;
    unsigned int i, found = 0;
    const char *names[] = {
        "luaL_addlstring", "luaL_addstring", "luaL_addvalue", "luaL_argerror", "luaL_buffinit",
        "luaL_callmeta", "luaL_checkany", "luaL_checkinteger", "luaL_checklstring", "luaL_checknumber",
        "luaL_checkoption", "luaL_checkstack", "luaL_checktype", "luaL_checkudata", "luaL_error",
        "luaL_execresult", "luaL_fileresult", "luaL_findtable", "luaL_getmetafield", "luaL_gsub",
        "luaL_loadbuffer", "luaL_loadbufferx", "luaL_loadfile", "luaL_loadfilex", "luaL_loadstring",
        "luaL_newmetatable", "luaL_newstate", "luaL_openlib", "luaL_openlibs", "luaL_optinteger",
        "luaL_optlstring", "luaL_optnumber", "luaL_prepbuffer", "luaL_pushmodule", "luaL_pushresult",
        "luaL_ref", "luaL_register", "luaL_setfuncs", "luaL_setmetatable", "luaL_testudata",
        "luaL_traceback", "luaL_typerror", "luaL_unref", "luaL_where", "lua_atpanic",
        "lua_call", "lua_checkstack", "lua_close", "lua_concat", "lua_copy",
        "lua_cpcall", "lua_createtable", "lua_dump", "lua_equal", "lua_error",
        "lua_gc", "lua_getallocf", "lua_getfenv", "lua_getfield", "lua_gethook",
        "lua_gethookcount", "lua_gethookmask", "lua_getinfo", "lua_getlocal", "lua_getmetatable",
        "lua_getstack", "lua_gettable", "lua_gettop", "lua_getupvalue", "lua_insert",
        "lua_iscfunction", "lua_isnumber", "lua_isstring", "lua_isuserdata", "lua_isyieldable",
        "lua_lessthan", "lua_load", "lua_loadx", "lua_newstate", "lua_newthread",
        "lua_newuserdata", "lua_next", "lua_objlen", "lua_pcall", "lua_pushboolean",
        "lua_pushcclosure", "lua_pushfstring", "lua_pushinteger", "lua_pushlightuserdata", "lua_pushlstring",
        "lua_pushnil", "lua_pushnumber", "lua_pushstring", "lua_pushthread", "lua_pushvalue",
        "lua_pushvfstring", "lua_rawequal", "lua_rawget", "lua_rawgeti", "lua_rawset",
        "lua_rawseti", "lua_remove", "lua_replace", "lua_resume", "lua_setallocf",
        "lua_setfenv", "lua_setfield", "lua_sethook", "lua_setlevel", "lua_setlocal",
        "lua_setmetatable", "lua_settable", "lua_settop", "lua_setupvalue", "lua_status",
        "lua_toboolean", "lua_tocfunction", "lua_tointeger", "lua_tointegerx", "lua_tolstring",
        "lua_tonumber", "lua_tonumberx", "lua_topointer", "lua_tothread", "lua_touserdata",
        "lua_type", "lua_typename", "lua_upvalueid", "lua_upvaluejoin", "lua_version",
        "lua_xmove", "lua_yield", "luaopen_base", "luaopen_bit", "luaopen_debug",
        "luaopen_ffi", "luaopen_io", "luaopen_jit", "luaopen_math", "luaopen_os",
        "luaopen_package", "luaopen_string", "luaopen_string_buffer", "luaopen_table"
    };

    if (luajit_dyn_init(argc > 1 ? argv[1] : NULL) != 0)
        TEST_SKIP("LuaJIT not installed, skipping luajit_dyn test\n");

    errs += CHECK(luajit_libok());
    errs += CHECK(!luajit_dyn_has_symbol("lua_no_such_symbol_xyz"));
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (luajit_dyn_has_symbol(names[i])) found++;
    }
    printf("info: LuaJIT public exports found = %u/%u (lua_setlevel declared but not exported)\n",
           found, (unsigned int)(sizeof names / sizeof names[0]));
    errs += CHECK(found == 143);

    {
        const lua_Number *ver = lua_version(NULL);
        errs += CHECK(ver != NULL);
        if (ver) printf("info: LuaJIT version number %.0f\n", *ver);
        errs += CHECK(ver && *ver == 501);
    }

    {
        lua_State *L = luaL_newstate();
        errs += CHECK(L != NULL);
        if (!L) goto done;

        luaL_openlibs(L);
        errs += CHECK(lua_gettop(L) == 0);

        errs += CHECK(luaL_loadstring(L, "x = 7; y = 'jit'") == 0);
        errs += CHECK(lua_pcall(L, 0, 0, 0) == 0);
        lua_getfield(L, LUA_GLOBALSINDEX, "x");
        {
            int isnum = 0;
            errs += CHECK(lua_tointegerx(L, -1, &isnum) == 7);
            errs += CHECK(isnum != 0);
        }
        lua_getfield(L, LUA_GLOBALSINDEX, "y");
        {
            size_t len = 0;
            const char *s = lua_tolstring(L, -1, &len);
            errs += CHECK(s != NULL && len == 3 && memcmp(s, "jit", 3) == 0);
        }
        errs += CHECK(lua_type(L, -1) == LUA_TSTRING);
        lua_settop(L, 0);

        errs += CHECK(luaL_loadbuffer(L, "return 40 + 2", 13, "=chunk") == 0);
        errs += CHECK(lua_pcall(L, 0, 1, 0) == 0);
        {
            int isnum = 0;
            errs += CHECK(lua_tonumberx(L, -1, &isnum) == 42);
            errs += CHECK(isnum != 0);
        }
        lua_settop(L, 0);

        lua_createtable(L, 0, 1);
        lua_pushinteger(L, 99);
        lua_rawseti(L, -2, 1);
        lua_rawgeti(L, -1, 1);
        {
            int isnum = 0;
            errs += CHECK(lua_tointegerx(L, -1, &isnum) == 99);
        }
        lua_settop(L, 0);

        {
            const char *s = lua_pushfstring(L, "n=%d s=%s", 12, "ok");
            errs += CHECK(s != NULL && strcmp(s, "n=12 s=ok") == 0);
            lua_settop(L, 0);
        }

        {
            luaL_Buffer b;
            luaL_buffinit(L, &b);
            (void)luaL_prepbuffer(&b);
            luaL_addstring(&b, "abc");
            luaL_pushresult(&b);
            {
                size_t len = 0;
                const char *s = lua_tolstring(L, -1, &len);
                errs += CHECK(s != NULL && len == 3 && memcmp(s, "abc", 3) == 0);
            }
            lua_settop(L, 0);
        }

        errs += CHECK(lua_gc(L, 0, 0) >= 0); /* LUA_GCSTOP = 0 */
        errs += CHECK(lua_gc(L, 1, 0) >= 0); /* LUA_GCRESTART = 1 */
        lua_close(L);
    }

done:
    luajit_dyn_cleanup();
    return errs ? 1 : 0;
}
