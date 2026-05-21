#pragma once

#ifndef OSCI_SCRIPTING_ENABLE_LUA
#define OSCI_SCRIPTING_ENABLE_LUA 1
#endif

#if OSCI_SCRIPTING_ENABLE_LUA && !__has_include(<lua.hpp>)
#error "osci_scripting requires LuaJIT headers. Add the LuaJIT include path or disable OSCI_SCRIPTING_ENABLE_LUA."
#endif
