#define V(A) void A() {}
void abort();
//#define V(A) void A() { abort(); }

V(luaL_checkany)
V(luaL_checkinteger)
V(luaL_checklstring)
V(luaL_checknumber)
V(luaL_checktype)
V(luaL_error)
V(luaL_getmetafield)
V(luaL_loadbufferx)
V(luaL_loadstring)
V(luaL_newmetatable)
V(luaL_newstate)
V(luaL_openlibs)
V(luaL_ref)
V(luaL_unref)
V(lua_callk)
V(lua_copy)
V(lua_createtable)
V(lua_getfield)
V(lua_getglobal)
V(lua_getmetatable)
V(lua_gettop)
V(lua_newuserdata)
V(lua_next)
V(lua_pcallk)
V(lua_pushboolean)
V(lua_pushcclosure)
V(lua_pushfstring)
V(lua_pushinteger)
V(lua_pushlightuserdata)
V(lua_pushnil)
V(lua_pushnumber)
V(lua_pushstring)
V(lua_pushvalue)
V(lua_rawget)
V(lua_rawgeti)
V(lua_rawseti)
V(lua_rotate)
V(lua_setfield)
V(lua_setglobal)
V(lua_setmetatable)
V(lua_settable)
V(lua_settop)
V(lua_toboolean)
V(lua_tolstring)
V(lua_tonumberx)
V(lua_touserdata)
V(lua_type)
V(lua_typename)
V(lua_close)