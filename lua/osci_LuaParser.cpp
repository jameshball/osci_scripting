#include "osci_LuaParser.h"
#include "osci_LuaLibrary.h"

// If you haven't compiled LuaJIT yet, this will fail, and you'll get a ton of syntax errors in a few Lua-related files!
// On all platforms, this should be done automatically when you run the export.
// If not, use the luajit_win.bat or luajit_linux_macos.sh scripts in the git root from the dev environment.
#include <lua.hpp>
#include <luajit.h>
#include <limits>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

std::function<void(const std::string&)> LuaParser::onPrint;
std::function<void()> LuaParser::onClear;

namespace
{
const char parserRegistryKey = 0;
std::atomic<uint64_t> nextStateGeneration { 1 };

void closeState(lua_State*& L) {
    if (L != nullptr) {
        // Finalizers may print after their parser/editor has been destroyed.
        lua_pushlightuserdata(L, const_cast<char*>(&parserRegistryKey));
        lua_pushnil(L);
        lua_settable(L, LUA_REGISTRYINDEX);
        lua_close(L);
        L = nullptr;
    }
}

LuaParser* getParserForState(lua_State* L) {
    lua_pushlightuserdata(L, const_cast<char*>(&parserRegistryKey));
    lua_gettable(L, LUA_REGISTRYINDEX);
    auto* parser = static_cast<LuaParser*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return parser;
}

bool isIgnoredSliderNilError(const std::string& error) {
    if (error.find("attempt to") == std::string::npos || error.find("nil value") == std::string::npos) {
        return false;
    }

    auto sliderStart = error.find("'slider_");
    if (sliderStart == std::string::npos) {
        return false;
    }

    const auto suffixStart = sliderStart + std::strlen("'slider_");
    return suffixStart < error.size() && std::isalpha(static_cast<unsigned char>(error[suffixStart]));
}

void removeNewlines(std::string& text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](char c) {
        return c == '\n' || c == '\r';
    }), text.end());
}

void removeLuaStringPrefix(std::string& error) {
    static constexpr char prefix[] = "[string \"";
    if (error.rfind(prefix, 0) != 0) {
        return;
    }

    auto end = error.find("\"]:");
    if (end != std::string::npos) {
        error.erase(0, end + 3);
    }
}

int removeLeadingLineNumber(std::string& error) {
    int line = 0;
    size_t index = 0;

    while (index < error.size() && std::isdigit(static_cast<unsigned char>(error[index]))) {
        const auto digit = error[index] - '0';
        line = line > (std::numeric_limits<int>::max() - digit) / 10
            ? std::numeric_limits<int>::max() : line * 10 + digit;
        ++index;
    }

    if (index == 0 || index >= error.size() || error[index] != ':') {
        return -1;
    }

    ++index;
    if (index < error.size() && error[index] == ' ') {
        ++index;
    }

    error.erase(0, index);
    return line;
}
} // namespace

LuaState::~LuaState() {
    reset();
}

void LuaState::reset() {
    if (state != nullptr && originalAllocator != nullptr) {
        lua_sethook(state, nullptr, 0, 0);
        // LuaJIT recognises its allocator here to destroy the entire arena.
        lua_setallocf(state, originalAllocator, originalAllocatorData);
    }
    closeState(state);
    generation = 0;
    originalAllocator = nullptr;
    originalAllocatorData = nullptr;
    memoryUsed = memoryLimit = 0;
    offlineFailed = memoryExceeded = false;
}

struct LuaParser::OfflineRunContext {
    LuaParser* parser;
    LuaState* state;
    LuaVariables* variables;
    LuaResult* result;
};

juce::Result LuaParser::setOfflinePolicy(OfflinePolicy policy) {
    if (policy.instructionBudget == 0 || policy.memoryLimitBytes < 64 * 1024
        || policy.memoryLimitBytes > 256 * 1024 * 1024) {
        return juce::Result::fail("Offline Lua requires a positive instruction budget and a memory limit between 64 KiB and 256 MiB.");
    }
    offlinePolicy = policy;
    functionRef = -1;
    usingFallbackScript = false;
    forgetAllStates();
    return juce::Result::ok();
}

void* LuaParser::offlineAllocator(void* context, void* pointer, std::size_t oldSize, std::size_t newSize) {
    auto& state = *static_cast<LuaState*>(context);
    const auto previous = pointer == nullptr ? std::size_t(0) : oldSize;
    if (newSize > previous && (state.memoryUsed > state.memoryLimit || newSize - previous > state.memoryLimit - state.memoryUsed)) {
        state.memoryExceeded = true;
        return nullptr;
    }
    auto* result = state.originalAllocator(state.originalAllocatorData, pointer, oldSize, newSize);
    if (result != nullptr || newSize == 0) {
        if (newSize >= previous) { state.memoryUsed += newSize - previous; }
        else { state.memoryUsed -= std::min(state.memoryUsed, previous - newSize); }
    }
    return result;
}

void LuaParser::offlineHook(lua_State* L, lua_Debug*) {
    void* context = nullptr;
    lua_getallocf(L, &context);
    auto& state = *static_cast<LuaState*>(context);
    if (state.cancellation != nullptr && state.cancellation->load(std::memory_order_relaxed)) {
        state.offlineFailed = true;
        luaL_error(L, "Offline Lua cancelled.");
        return;
    }
    if (state.instructionsRemaining <= static_cast<uint64_t>(state.hookInterval)) {
        state.offlineFailed = true;
        luaL_error(L, "Offline Lua instruction budget exceeded.");
        return;
    }
    state.instructionsRemaining -= static_cast<uint64_t>(state.hookInterval);
    const auto next = static_cast<int>(std::min<uint64_t>(state.instructionsRemaining, 256));
    if (next != state.hookInterval) {
        state.hookInterval = next;
        lua_sethook(L, offlineHook, LUA_MASKCOUNT, next);
    }
}

int LuaParser::initialiseOffline(lua_State* L) {
    auto& context = *static_cast<OfflineRunContext*>(lua_touserdata(L, 1));
    auto& parser = *context.parser;
    luaL_openlibs(L);
    luaopen_oscilibrary(L);
    luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    parser.bindParserToState(L);
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushnumber(L, static_cast<double>(parser.offlinePolicy->randomSeed));
    lua_call(L, 1, 0);
    lua_pop(L, 1);
    // Protected calls/coroutines could otherwise swallow or evade the budget
    // error. Finalizers and console callbacks could execute outside the guarded
    // per-sample call, so these are intentionally unavailable offline too.
    const char* denied[] = { "io", "os", "package", "debug", "ffi", "jit", "require", "module", "dofile", "loadfile",
        "load", "loadstring", "coroutine", "pcall", "xpcall", "newproxy", "collectgarbage", "gcinfo",
        "getfenv", "setfenv", "print", "clear" };
    for (const auto* name : denied) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    const auto* text = parser.script.toRawUTF8();
    if (static_cast<unsigned char>(text[0]) == 0x1b) {
        return luaL_error(L, "Offline Lua accepts text source only.");
    }
    if (luaL_loadbuffer(L, text, static_cast<std::size_t>(parser.script.getNumBytesAsUTF8()), "offline") != 0) {
        return lua_error(L);
    }
    parser.functionRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

int LuaParser::executeOffline(lua_State* L) {
    auto& context = *static_cast<OfflineRunContext*>(lua_touserdata(L, 1));
    lua_settop(L, 0);
    context.parser->setGlobalVariables(L, *context.variables);
    lua_rawgeti(L, LUA_REGISTRYINDEX, context.parser->functionRef);
    lua_call(L, 0, LUA_MULTRET);
    if (lua_gettop(L) != 1 || !lua_istable(L, 1)) {
        return luaL_error(L, "Offline Lua must return one dense numeric array.");
    }
    // Do not use the live parser's permissive table reader: it coerces strings/booleans
    // and truncates extra channels. Scan raw keys so sparse or oversized tables
    // cannot hide extra results behind Lua's undefined length for arrays with holes.
    unsigned int present = 0;
    int count = 0;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        if (lua_type(L, -2) != LUA_TNUMBER) {
            return luaL_error(L, "Offline Lua results must use consecutive numeric keys starting at 1.");
        }
        const auto key = lua_tonumber(L, -2);
        if (!std::isfinite(key) || key < 1 || key > MAX_LUA_RESULT_VALUES || std::floor(key) != key) {
            return luaL_error(L, "Offline Lua results must contain at most six consecutive values.");
        }
        if (lua_type(L, -1) != LUA_TNUMBER) {
            return luaL_error(L, "Offline Lua result values must be numbers, not strings, booleans or tables.");
        }
        const auto value = lua_tonumber(L, -1);
        if (!std::isfinite(value) || std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
            return luaL_error(L, "Offline Lua returned a non-finite or unrepresentable number.");
        }
        const auto index = static_cast<int>(key) - 1;
        context.result->values[index] = static_cast<float>(value);
        present |= 1u << index;
        count = std::max(count, index + 1);
        lua_pop(L, 1);
    }
    if (count == 0 || present != (1u << count) - 1u) {
        return luaL_error(L, "Offline Lua must return a nonempty dense numeric array.");
    }
    context.result->count = count;
    return 0;
}

LuaResult LuaParser::runOffline(LuaState& state, LuaVariables& vars) {
    LuaResult result;
    const auto currentGeneration = generation.load(std::memory_order_acquire);
    const auto fail = [&](const char* message) {
        state.offlineFailed = true;
        functionRef = -1;
        auto diagnostic = parseErrorMessage(message);
        if (!diagnostic.hasError()) {
            diagnostic = { 1, message != nullptr ? juce::String(message) : juce::String("Offline Lua failed.") };
        }
        if (errorCallback) { errorCallback(diagnostic.lineNumber, fileName, diagnostic.message); }
    };
    if (offlinePolicy->cancelled != nullptr && offlinePolicy->cancelled->load(std::memory_order_relaxed)) {
        fail("Offline Lua cancelled.");
        return result;
    }
    OfflineRunContext context { this, &state, &vars, &result };
    if (state.state == nullptr || state.generation != currentGeneration) {
        state.reset();
        functionRef = -1;
        // Keep LuaJIT's native allocator (including its low-address allocator on
        // non-GC64 platforms), wrapping only after the fixed VM bootstrap.
        state.state = luaL_newstate();
        if (state.state == nullptr) {
            fail("Unable to create offline Lua state.");
            return result;
        }
        state.generation = currentGeneration;
        state.memoryUsed = static_cast<std::size_t>(lua_gc(state.state, LUA_GCCOUNT, 0)) * 1024
            + static_cast<std::size_t>(lua_gc(state.state, LUA_GCCOUNTB, 0));
        state.memoryLimit = offlinePolicy->memoryLimitBytes;
        state.cancellation = offlinePolicy->cancelled;
        state.originalAllocator = lua_getallocf(state.state, &state.originalAllocatorData);
        lua_setallocf(state.state, offlineAllocator, &state);
        if (state.memoryUsed > state.memoryLimit || lua_cpcall(state.state, initialiseOffline, &context) != 0) {
            fail(state.memoryUsed > state.memoryLimit ? "Offline Lua memory budget exceeded." : lua_tostring(state.state, -1));
            lua_settop(state.state, 0);
            return result;
        }
        detectUsedVariables(script);
    }
    if (state.offlineFailed) { return result; }
    state.instructionsRemaining = offlinePolicy->instructionBudget;
    state.hookInterval = static_cast<int>(std::min<uint64_t>(state.instructionsRemaining, 256));
    lua_sethook(state.state, offlineHook, LUA_MASKCOUNT, state.hookInterval);
    const auto status = lua_cpcall(state.state, executeOffline, &context);
    lua_sethook(state.state, nullptr, 0, 0);
    if (status != 0 || state.memoryExceeded || state.offlineFailed) {
        fail(state.memoryExceeded ? "Offline Lua memory budget exceeded." : lua_tostring(state.state, -1));
        lua_settop(state.state, 0);
        return {};
    }
    lua_settop(state.state, 0);
    resetErrors();
    incrementVars(vars);
    return result;
}

void LuaParser::forgetAllStates() {
    generation.store(nextStateGeneration.fetch_add(1, std::memory_order_relaxed), std::memory_order_release);
}

void LuaParser::maximumInstructionsReached(lua_State* L, lua_Debug* D) {
    lua_getstack(L, 1, D);
    lua_getinfo(L, "l", D);
    
	std::string msg = std::to_string(D->currentline) + ": Maximum instructions reached! You may have an infinite loop.";
	lua_pushstring(L, msg.c_str());
	lua_error(L);
}

void LuaParser::setMaximumInstructions(lua_State*& L, int count) {
    lua_sethook(L, LuaParser::maximumInstructionsReached, LUA_MASKCOUNT, count);
}

LuaParser::LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, juce::String fallbackScript) : script(script), fallbackScript(fallbackScript), errorCallback(errorCallback), fileName(fileName), generation(nextStateGeneration.fetch_add(1, std::memory_order_relaxed)) {}

void LuaParser::setConsoleCallbacks(std::function<void(const std::string&)> printCallback, std::function<void()> clearCallback) {
    consolePrintCallback = std::move(printCallback);
    consoleClearCallback = std::move(clearCallback);
}

void LuaParser::reset(lua_State*& L, juce::String script) {
    functionRef = -1;

    closeState(L);
    
    L = luaL_newstate();
    luaL_openlibs(L);
	luaopen_oscilibrary(L);
    bindParserToState(L);
    
    this->script = script;
    parse(L);
}

LuaDiagnostic LuaParser::parseErrorMessage(const char* errorChars) {
    if (errorChars == nullptr) {
        return {};
    }

    std::string error = errorChars;
    // ignore nil errors about global variables, these are likely caused by other errors
    if (isIgnoredSliderNilError(error)) {
        return {};
    }

    removeNewlines(error);
    removeLuaStringPrefix(error);

    auto line = removeLeadingLineNumber(error);
    if (line >= 0) {
        return { line, juce::String(error) };
    }

    if (!error.empty()) {
        return { 1, juce::String(error) };
    }

    return {};
}

LuaDiagnostic LuaParser::validateScript(const juce::String& scriptToValidate) {
    lua_State* validationState = luaL_newstate();
    if (validationState == nullptr) {
        return { 1, "Unable to create Lua validation state" };
    }

    const int ret = luaL_loadstring(validationState, scriptToValidate.toUTF8());
    LuaDiagnostic diagnostic;

    if (ret != 0) {
        diagnostic = parseErrorMessage(lua_tostring(validationState, -1));
        lua_pop(validationState, 1);
    } else {
        lua_pop(validationState, 1);
    }

    lua_close(validationState);
    return diagnostic;
}

void LuaParser::reportError(const char* errorChars) {
    auto diagnostic = parseErrorMessage(errorChars);
    if (diagnostic.hasError()) {
        errorCallback(diagnostic.lineNumber, fileName, diagnostic.message);
    }
}

void LuaParser::bindParserToState(lua_State* L) {
    lua_pushlightuserdata(L, const_cast<char*>(&parserRegistryKey));
    lua_pushlightuserdata(L, this);
    lua_settable(L, LUA_REGISTRYINDEX);
}

void LuaParser::parse(lua_State*& L) {
    const int ret = luaL_loadstring(L, script.toUTF8());
    if (ret != 0) {
        const char* error = lua_tostring(L, -1);
        reportError(error);
        lua_pop(L, 1);
        revertToFallback(L);
    } else {
        functionRef = luaL_ref(L, LUA_REGISTRYINDEX);
        detectUsedVariables(script);
    }
}

void LuaParser::detectUsedVariables(const juce::String& scriptText) {
    uint64_t mask = 0;
    auto text = scriptText.toRawUTF8();

    // If script uses dynamic global access or loads external code, conservatively enable all variables
    if (strstr(text, "_G") || strstr(text, "getfenv") || strstr(text, "rawget")
        || strstr(text, "require") || strstr(text, "dofile") || strstr(text, "loadfile")) {
        usedVarMask = ~uint64_t(0);
        return;
    }

    if (strstr(text, "step"))               mask |= (1ULL << LuaVar_step);
    if (strstr(text, "sample_rate"))        mask |= (1ULL << LuaVar_sampleRate);
    if (strstr(text, "frequency"))          mask |= (1ULL << LuaVar_frequency);
    if (strstr(text, "phase"))              mask |= (1ULL << LuaVar_phase);
    if (strstr(text, "cycle_count"))        mask |= (1ULL << LuaVar_cycleCount);

    for (int i = 0; i < NUM_SLIDERS; i++) {
        if (strstr(text, SLIDER_NAMES[i]))  mask |= (1ULL << (LuaVar_sliderFirst + i));
    }

    if (strstr(text, "x"))                  mask |= (1ULL << LuaVar_x);
    if (strstr(text, "y"))                  mask |= (1ULL << LuaVar_y);
    if (strstr(text, "z"))                  mask |= (1ULL << LuaVar_z);
    if (strstr(text, "ext_x"))              mask |= (1ULL << LuaVar_extX);
    if (strstr(text, "ext_y"))              mask |= (1ULL << LuaVar_extY);

    if (strstr(text, "midi_note"))          mask |= (1ULL << LuaVar_midiNote);
    if (strstr(text, "velocity"))           mask |= (1ULL << LuaVar_velocity);
    if (strstr(text, "voice_index"))        mask |= (1ULL << LuaVar_voiceIndex);
    if (strstr(text, "note_on"))            mask |= (1ULL << LuaVar_noteOn);

    if (strstr(text, "bpm"))                mask |= (1ULL << LuaVar_bpm);
    if (strstr(text, "play_time"))          mask |= (1ULL << LuaVar_playTime);
    if (strstr(text, "play_time_beats"))    mask |= (1ULL << LuaVar_playTimeBeats);
    if (strstr(text, "is_playing"))         mask |= (1ULL << LuaVar_isPlaying);
    if (strstr(text, "time_sig_num"))       mask |= (1ULL << LuaVar_timeSigNum);
    if (strstr(text, "time_sig_den"))       mask |= (1ULL << LuaVar_timeSigDen);

    if (strstr(text, "envelope"))           mask |= (1ULL << LuaVar_envelope);
    if (strstr(text, "envelope_stage"))     mask |= (1ULL << LuaVar_envelopeStage);

    usedVarMask = mask;
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, double value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, int value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, bool value) {
    lua_pushboolean(L, value ? 1 : 0);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariables(lua_State*& L, LuaVariables& vars) {
    auto set = [&](LuaVarBit bit, const char* name, auto value) {
        if (usedVarMask & (1ULL << bit))
            setGlobalVariable(L, name, value);
    };

    set(LuaVar_step,       "step",        vars.step);
    set(LuaVar_sampleRate, "sample_rate", vars.sampleRate);
    set(LuaVar_frequency,  "frequency",   vars.frequency);
    set(LuaVar_phase,      "phase",       vars.phase);
    set(LuaVar_cycleCount, "cycle_count", vars.cycle);

    for (int i = 0; i < NUM_SLIDERS; i++)
        set((LuaVarBit)(LuaVar_sliderFirst + i), SLIDER_NAMES[i], vars.sliders[i]);

    if (vars.isEffect) {
        set(LuaVar_x, "x", vars.x);
        set(LuaVar_y, "y", vars.y);
        set(LuaVar_z, "z", vars.z);
    }

    set(LuaVar_extX, "ext_x", vars.ext_x);
    set(LuaVar_extY, "ext_y", vars.ext_y);

    // MIDI context
    set(LuaVar_midiNote,   "midi_note",   vars.midiNote);
    set(LuaVar_velocity,   "velocity",    vars.velocity);
    set(LuaVar_voiceIndex, "voice_index", vars.voiceIndex);
    set(LuaVar_noteOn,     "note_on",     vars.noteOn);

    // DAW transport
    set(LuaVar_bpm,           "bpm",             vars.bpm);
    set(LuaVar_playTime,      "play_time",        vars.playTime);
    set(LuaVar_playTimeBeats, "play_time_beats",  vars.playTimeBeats);
    set(LuaVar_isPlaying,     "is_playing",       vars.isPlaying);
    set(LuaVar_timeSigNum,    "time_sig_num",     vars.timeSigNumerator);
    set(LuaVar_timeSigDen,    "time_sig_den",     vars.timeSigDenominator);

    // Envelope
    set(LuaVar_envelope,      "envelope",       vars.envelope);
    set(LuaVar_envelopeStage, "envelope_stage", vars.envelopeStage);
}

void LuaParser::incrementVars(LuaVariables& vars) {
    vars.step++;
    vars.phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
    if (vars.phase > 2 * std::numbers::pi) {
        vars.phase -= 2 * std::numbers::pi;
        vars.cycle += 1;
    }
}

void LuaParser::clearStack(lua_State*& L) {
    lua_settop(L, 0);
}

void LuaParser::revertToFallback(lua_State*& L) {
    functionRef = -1;
    usingFallbackScript = true;
    if (script != fallbackScript) {
        reset(L, fallbackScript);
    }
}

void LuaParser::readTable(lua_State*& L, LuaResult& result) {
    int length = std::min((int)lua_objlen(L, -1), MAX_LUA_RESULT_VALUES);
    result.count = length;

    for (int i = 1; i <= length; i++) {
        lua_rawgeti(L, -1, i);
        result.values[i - 1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
}

// only the audio thread runs this fuction
LuaResult LuaParser::run(LuaState& state, LuaVariables& vars) {
    if (offlinePolicy.has_value()) { return runOffline(state, vars); }
    if (state.originalAllocator != nullptr) { state.reset(); }
    const auto currentGeneration = generation.load(std::memory_order_acquire);
    auto& L = state.state;
    if (L == nullptr || state.generation != currentGeneration) {
        reset(L, script);
        state.generation = currentGeneration;
    }

    LuaResult result;

    // Reset instruction counter before setting globals and calling pcall.
    setMaximumInstructions(L, 5000000);
	
	setGlobalVariables(L, vars);
    
	// Get the function from the registry
	lua_rawgeti(L, LUA_REGISTRYINDEX, functionRef);
    
    if (lua_isfunction(L, -1)) {
        const int ret = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (ret != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            reportError(error);
            revertToFallback(L);
        } else {            
            if (lua_istable(L, -1)) {
                readTable(L, result);
            }
        }
    } else {
        revertToFallback(L);
    }

    if (functionRef != -1 && !usingFallbackScript) {
        resetErrors();
    }

	clearStack(L);
    
	incrementVars(vars);
    
	return result;
}

bool LuaParser::isFunctionValid() {
    return functionRef != -1;
}

juce::String LuaParser::getScript() {
    return script;
}

void LuaParser::resetErrors() {
    errorCallback(-1, fileName, "");
}

void LuaParser::emitPrint(lua_State* L, const std::string& text) {
    auto* parser = getParserForState(L);
    if (parser == nullptr) {
        return;
    }
    if (parser->consolePrintCallback) {
        parser->consolePrintCallback(text);
        return;
    }

    if (onPrint) {
        onPrint(text);
    }
}

void LuaParser::emitClear(lua_State* L) {
    auto* parser = getParserForState(L);
    if (parser == nullptr) {
        return;
    }
    if (parser->consoleClearCallback) {
        parser->consoleClearCallback();
        return;
    }

    if (onClear) {
        onClear();
    }
}
