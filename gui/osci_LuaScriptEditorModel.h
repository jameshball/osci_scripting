#pragma once

namespace osci {

class LuaScriptEditorModel final : public ErrorListener,
                                   public LuaConsoleListener,
                                   public juce::ChangeBroadcaster,
                                   private juce::CodeDocument::Listener,
                                   private juce::AsyncUpdater {
public:
    LuaScriptEditorModel(juce::String scriptId,
                         juce::String displayName,
                         juce::String initialCode,
                         bool validateLua = true,
                         int debounceMs = 250);
    ~LuaScriptEditorModel() override;

    juce::CodeDocument& getDocument();
    const juce::CodeDocument& getDocument() const;
    juce::String getCode() const;
    void replaceCodeFromHost(const juce::String& code);

    juce::String getDisplayName() const;
    void setDisplayName(juce::String newDisplayName);
    juce::String getScriptId() const;

    LuaDiagnostic getDiagnostic() const;
    void clearDiagnostic();
    void flushPendingEdit();

    void setValidateLua(bool shouldValidateLua);
    void setDebounceMs(int delayMs);

    void setConsoleHistory(const juce::String& text);
    void appendConsoleOutput(const std::string& text);
    juce::String getConsoleHistory() const;
    void clearConsoleHistory();
    bool hasConsoleHistory() const;
    void setConsoleLineLimit(int newLimit);

    std::function<void()> onEditStarted;
    std::function<void(const juce::String& code)> onCodeCommitted;
    std::function<void()> onEditFinished;
    std::function<void()> onConsoleChanged;
    std::function<void()> onExpandRequested;
    std::function<void()> onHelpRequested;
    std::function<void()> onResetRequested;

private:
    void codeDocumentTextInserted(const juce::String&, int) override;
    void codeDocumentTextDeleted(int, int) override;
    void onDocumentEdited();
    void commitCurrentCode();
    void validateCurrentCode();

    void onError(int lineNumber, juce::String error) override;
    juce::String getId() override;

    void luaConsolePrinted(const std::string& text) override;
    void luaConsoleCleared() override;

    void handleAsyncUpdate() override;
    void notifyChanged();

    juce::String id;
    juce::String title;
    juce::CodeDocument document;
    osci::DebouncedCallback debounce;
    mutable juce::CriticalSection stateLock;
    mutable juce::CriticalSection consoleLock;
    LuaDiagnostic diagnostic;
    juce::StringArray consoleLines;
    int consoleLineLimit = 2000;
    bool validateLuaSyntax = true;
    bool suppressDocumentCallbacks = false;
    bool editInProgress = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaScriptEditorModel)
};

} // namespace osci
