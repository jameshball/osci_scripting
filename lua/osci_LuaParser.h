#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

class ErrorListener {
public:
	virtual ~ErrorListener() = default;

	virtual void onError(int lineNumber, juce::String error) = 0;
	virtual juce::String getId() = 0;
};

class LuaConsoleListener {
public:
	virtual ~LuaConsoleListener() = default;

	virtual void luaConsolePrinted(const std::string& text) = 0;
	virtual void luaConsoleCleared() = 0;
};

struct LuaDiagnostic {
	int lineNumber = -1;
	juce::String message;

	bool hasError() const {
		return lineNumber >= 0 && message.isNotEmpty();
	}
};

const int NUM_SLIDERS = 26;

const char SLIDER_NAMES[NUM_SLIDERS][9] = {
	"slider_a",
	"slider_b",
	"slider_c",
	"slider_d",
	"slider_e",
	"slider_f",
	"slider_g",
	"slider_h",
	"slider_i",
	"slider_j",
	"slider_k",
	"slider_l",
	"slider_m",
	"slider_n",
	"slider_o",
	"slider_p",
	"slider_q",
	"slider_r",
	"slider_s",
	"slider_t",
	"slider_u",
	"slider_v",
	"slider_w",
	"slider_x",
	"slider_y",
	"slider_z",
};

struct LuaVariables {

	double sliders[NUM_SLIDERS] = { 0.0 };

	double step = 1;
	double phase = 0;
	double sampleRate = 0;
	double frequency = 0;
	double cycle = 1;

	// x, y, z are only used for effects
	bool isEffect = false;

	double x = 0;
	double y = 0;
	double z = 0;
	
    double ext_x = 0;
	double ext_y = 0;

	// MIDI context
	int midiNote = 60;
	double velocity = 1.0;
	int voiceIndex = 0;
	bool noteOn = false;

	// DAW transport
	double bpm = 120.0;
	double playTime = 0.0;
	double playTimeBeats = 0.0;
	bool isPlaying = false;
	int timeSigNumerator = 4;
	int timeSigDenominator = 4;

	// Envelope
	double envelope = 1.0;
	int envelopeStage = 0;  // 0=delay, 1=attack, 2=hold, 3=decay, 4=sustain, 5=release, 6=done

	// Block-relative sample index for per-sample parameter reads
	int blockSampleIndex = 0;
};

// Bit positions for the usedVarMask that controls which globals are set per sample.
// Keep in sync with detectUsedVariables() and setGlobalVariables().
enum LuaVarBit {
	LuaVar_step = 0,
	LuaVar_sampleRate,
	LuaVar_frequency,
	LuaVar_phase,
	LuaVar_cycleCount,

	LuaVar_sliderFirst = 5,                          // slider_a … slider_z occupy bits 5–30
	LuaVar_sliderLast  = LuaVar_sliderFirst + NUM_SLIDERS - 1,

	LuaVar_x = 31,
	LuaVar_y,
	LuaVar_z,
	LuaVar_extX,
	LuaVar_extY,

	LuaVar_midiNote,
	LuaVar_velocity,
	LuaVar_voiceIndex,
	LuaVar_noteOn,

	LuaVar_bpm,
	LuaVar_playTime,
	LuaVar_playTimeBeats,
	LuaVar_isPlaying,
	LuaVar_timeSigNum,
	LuaVar_timeSigDen,

	LuaVar_envelope,
	LuaVar_envelopeStage,
};

static_assert(LuaVar_envelopeStage < 64, "LuaVarBit values must fit in a uint64_t mask");

static constexpr int MAX_LUA_RESULT_VALUES = 6;

struct LuaResult {
	float values[MAX_LUA_RESULT_VALUES] = {};
	int count = 0;
};

struct lua_State;

class LuaState {
public:
    LuaState() = default;
    ~LuaState();
    LuaState(const LuaState&) = delete;
    LuaState& operator=(const LuaState&) = delete;
    void reset();

private:
    friend class LuaParser;
    lua_State* state = nullptr;
    uint64_t generation = 0;
    using Allocator = void* (*)(void*, void*, std::size_t, std::size_t);
    Allocator originalAllocator = nullptr;
    void* originalAllocatorData = nullptr;
    std::size_t memoryUsed = 0, memoryLimit = 0;
    uint64_t instructionsRemaining = 0;
    int hookInterval = 256;
    const std::atomic<bool>* cancellation = nullptr;
    bool offlineFailed = false, memoryExceeded = false;
};
struct lua_Debug;
class LuaParser {
public:
    // Opt-in, worker-only policy. Not thread-safe: set it on the thread that
    // runs this parser, before its first run, with a fresh per-worker LuaState;
    // the cancellation flag must outlive all runs using this parser.
    // Bounds Lua-requested heap bytes (plus a fixed native VM bootstrap), not
    // allocator arena overhead. Hooks interrupt Lua bytecode, not long native
    // library calls. Random numbers have a fixed seed; unordered table traversal
    // and address-derived strings are not cross-VM reproducibility guarantees.
    // Errors are terminal for that state: no fallback script is substituted.
    struct OfflinePolicy {
        uint64_t instructionBudget = 100000;
        std::size_t memoryLimitBytes = 16 * 1024 * 1024;
        uint32_t randomSeed = 0;
        const std::atomic<bool>* cancelled = nullptr;
    };
    juce::Result setOfflinePolicy(OfflinePolicy policy);

	LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, juce::String fallbackScript = "return { 0.0, 0.0 }");

	LuaResult run(LuaState& state, LuaVariables& vars);
	bool isFunctionValid();
	juce::String getScript();
	void resetErrors();
	void forgetAllStates();
	std::function<void(int, juce::String, juce::String)> getErrorCallback() const { return errorCallback; }
	void setConsoleCallbacks(std::function<void(const std::string&)> printCallback, std::function<void()> clearCallback);

	static std::function<void(const std::string&)> onPrint;
	static std::function<void()> onClear;
	static void emitPrint(lua_State* L, const std::string& text);
	static void emitClear(lua_State* L);
	static LuaDiagnostic validateScript(const juce::String& script);

private:
    struct OfflineRunContext;
    LuaResult runOffline(LuaState& state, LuaVariables& vars);
    static int initialiseOffline(lua_State* L);
    static int executeOffline(lua_State* L);
    static void offlineHook(lua_State* L, lua_Debug* D);
    static void* offlineAllocator(void* context, void* pointer, std::size_t oldSize, std::size_t newSize);
    std::optional<OfflinePolicy> offlinePolicy;
	static void maximumInstructionsReached(lua_State* L, lua_Debug* D);
	static LuaDiagnostic parseErrorMessage(const char* error);

	void reset(lua_State*& L, juce::String script);
	void reportError(const char* error);
	void bindParserToState(lua_State* L);
	void parse(lua_State*& L);
	void setGlobalVariable(lua_State*& L, const char* name, double value);
	void setGlobalVariable(lua_State*& L, const char* name, int value);
	void setGlobalVariable(lua_State*& L, const char* name, bool value);
	void setGlobalVariables(lua_State*& L, LuaVariables& vars);
	void incrementVars(LuaVariables& vars);
	void clearStack(lua_State*& L);
	void revertToFallback(lua_State*& L);
	void readTable(lua_State*& L, LuaResult& result);
	void setMaximumInstructions(lua_State*& L, int count);
	void detectUsedVariables(const juce::String& scriptText);

	int functionRef = -1;
	bool usingFallbackScript = false;
	juce::String script;
	juce::String fallbackScript;
	std::function<void(int, juce::String, juce::String)> errorCallback;
	juce::String fileName;
	std::atomic<uint64_t> generation;
	uint64_t usedVarMask = ~uint64_t(0);
	std::function<void(const std::string&)> consolePrintCallback;
	std::function<void()> consoleClearCallback;
};
