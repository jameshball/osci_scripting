#pragma once

#if !__has_include(<lua.hpp>)
#error "osci_scripting requires LuaJIT headers. Initialise third_party/LuaJIT and add its src directory to the include path, or omit the osci_scripting module."
#endif
