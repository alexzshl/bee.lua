#pragma once

#include <lua.hpp>
#include <errno.h>
#include <string.h>
#include <bee/lua/binding.h>

namespace bee::lua {
    inline luaL_Stream* tolstream(lua_State* L) {
        return (luaL_Stream*)getObject(L, 1, "file");
    }
    inline bool isclosed(luaL_Stream* p) {
        return p->closef == NULL;
    }
    inline FILE* tofile(lua_State* L) {
        luaL_Stream *p = tolstream(L);
        if (isclosed(p))
            luaL_error(L, "attempt to use a closed file");
        lua_assert(p->f);
        return p->f;
    }
    inline int test_eof(lua_State* L, FILE* f) {
        int c = getc(f);
        ungetc(c, f);
        lua_pushliteral(L, "");
        return (c != EOF);
    }
    inline void read_all(lua_State* L, FILE* f) {
        size_t nr;
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        do {
            char *p = luaL_prepbuffer(&b);
            nr = fread(p, sizeof(char), LUAL_BUFFERSIZE, f);
            luaL_addsize(&b, nr);
        } while (nr == LUAL_BUFFERSIZE);
        luaL_pushresult(&b);
    }
    inline int read_chars(lua_State* L, FILE* f, size_t n) {
        size_t nr;
        char *p;
        luaL_Buffer b;
        luaL_buffinit(L, &b);
        p = luaL_prepbuffsize(&b, n);
        nr = fread(p, sizeof(char), n, f);
        luaL_addsize(&b, nr);
        luaL_pushresult(&b);
        return (nr > 0);
    }
    inline int f_read(lua_State* L) {
        FILE* f = tofile(L);
        int success = 1;
        clearerr(f);
        if (lua_type(L, 2) == LUA_TNUMBER) {
            size_t l = (size_t)luaL_checkinteger(L, 2);
            success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
        }
        else {
            const char *p = luaL_checkstring(L, 2);
            if (*p == 'a') {
                read_all(L, f);
            }
            else {
                return luaL_argerror(L, 2, "invalid format");
            }
        }
        if (ferror(f))
            return luaL_fileresult(L, 0, NULL);
        if (!success) {
            lua_pop(L, 1);
            luaL_pushfail(L);
        }
        return 1;
    }
    inline int f_write(lua_State* L) {
        FILE* f = tofile(L);
        int status = 1;
        if (lua_type(L, 2) == LUA_TNUMBER) {
            int len = lua_isinteger(L, 2)
                        ? fprintf(f, LUA_INTEGER_FMT,
                                    (LUAI_UACINT)lua_tointeger(L, 2))
                        : fprintf(f, LUA_NUMBER_FMT,
                                    (LUAI_UACNUMBER)lua_tonumber(L, 2));
            status = len > 0;
        }
        else {
            size_t l;
            const char *s = luaL_checklstring(L, 2, &l);
            status = fwrite(s, sizeof(char), l, f) == l;
        }
        if (status) {
            lua_pushvalue(L, 1);
            return 1;
        }
        return luaL_fileresult(L, status, NULL);
    }
    inline int _fileclose(lua_State* L) {
        luaL_Stream* p = tolstream(L);
        int ok = fclose(p->f);
        int en = errno;
        if (ok) {
            lua_pushboolean(L, 1);
            return 1;
        }
        else {
            lua_pushnil(L);
            lua_pushfstring(L, "%s", strerror(en));
            lua_pushinteger(L, en);
            return 3;
        }
    }
    inline int aux_close(lua_State* L) {
        luaL_Stream *p = tolstream(L);
        volatile lua_CFunction cf = p->closef;
        p->closef = NULL;
        return (*cf)(L);
    }
    inline int f_close(lua_State* L) {
        tofile(L); 
        return aux_close(L);
    }
    inline int f_gc(lua_State* L) {
        luaL_Stream *p = tolstream(L);
        if (!isclosed(p) && p->f != NULL)
            aux_close(L);
        return 0;
    }
    inline int f_tostring(lua_State* L) {
        luaL_Stream *p = tolstream(L);
        if (isclosed(p))
            lua_pushliteral(L, "file (closed)");
        else
            lua_pushfstring(L, "file (%p)", p->f);
        return 1;
    }
    inline int newfile(lua_State* L, FILE* f) {
        luaL_Stream* pf = (luaL_Stream*)lua_newuserdatauv(L, sizeof(luaL_Stream), 0);
        pf->closef = &_fileclose;
        pf->f = f;
        if (newObject(L, "file")) {
            const luaL_Reg meth[] = {
                {"read", f_read},
                {"write", f_write},
                //{"lines", f_lines},
                //{"flush", f_flush},
                //{"seek", f_seek},
                {"close", f_close},
                //{"setvbuf", f_setvbuf},
                {NULL, NULL}
            };
            const luaL_Reg metameth[] = {
                {"__index", NULL},
                {"__gc", f_gc},
                {"__close", f_gc},
                {"__tostring", f_tostring},
                {NULL, NULL}
            };
            luaL_setfuncs(L, metameth, 0);
            luaL_newlibtable(L, meth);
            luaL_setfuncs(L, meth, 0);
            lua_setfield(L, -2, "__index"); 
        }
        lua_setmetatable(L, -2);
        return 1;
    }
}
