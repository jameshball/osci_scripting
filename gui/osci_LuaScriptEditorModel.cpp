#include "osci_LuaScriptEditorModel.h"

namespace osci {

LuaScriptEditorModel::LuaScriptEditorModel(juce::String scriptId,
                                           juce::String displayName,
                                           juce::String initialCode,
                                           bool shouldValidateLua,
                                           int debounceMs)
    : id(std::move(scriptId)),
      title(std::move(displayName)),
      debounce(debounceMs),
      validateLuaSyntax(shouldValidateLua) {
    suppressDocumentCallbacks = true;
    document.replaceAllContent(initialCode);
    document.clearUndoHistory();
    suppressDocumentCallbacks = false;
    document.addListener(this);
    validateCurrentCode();
}

LuaScriptEditorModel::~LuaScriptEditorModel() {
    debounce.cancel();
    document.removeListener(this);
}

juce::CodeDocument& LuaScriptEditorModel::getDocument() {
    return document;
}

const juce::CodeDocument& LuaScriptEditorModel::getDocument() const {
    return document;
}

juce::String LuaScriptEditorModel::getCode() const {
    return document.getAllContent();
}

void LuaScriptEditorModel::replaceCodeFromHost(const juce::String& code) {
    debounce.cancel();
    suppressDocumentCallbacks = true;
    document.replaceAllContent(code);
    document.clearUndoHistory();
    suppressDocumentCallbacks = false;
    editInProgress = false;
    validateCurrentCode();
    notifyChanged();
}

juce::String LuaScriptEditorModel::getDisplayName() const {
    return title;
}

void LuaScriptEditorModel::setDisplayName(juce::String newDisplayName) {
    title = std::move(newDisplayName);
    notifyChanged();
}

juce::String LuaScriptEditorModel::getScriptId() const {
    return id;
}

LuaDiagnostic LuaScriptEditorModel::getDiagnostic() const {
    const juce::ScopedLock lock(stateLock);
    return diagnostic;
}

void LuaScriptEditorModel::clearDiagnostic() {
    {
        const juce::ScopedLock lock(stateLock);
        diagnostic = {};
    }
    notifyChanged();
}

void LuaScriptEditorModel::flushPendingEdit() {
    debounce.flush();

    if (editInProgress) {
        editInProgress = false;
        if (onEditFinished) {
            onEditFinished();
        }
    }
}

void LuaScriptEditorModel::setValidateLua(bool shouldValidateLua) {
    validateLuaSyntax = shouldValidateLua;
    validateCurrentCode();
    notifyChanged();
}

void LuaScriptEditorModel::setDebounceMs(int delayMs) {
    debounce.setDefaultDelayMs(delayMs);
}

void LuaScriptEditorModel::setConsoleHistory(const juce::String& text) {
    {
        const juce::ScopedLock lock(consoleLock);
        consoleLines.clear();
        consoleLines.addLines(text);

        while (consoleLines.size() > consoleLineLimit) {
            consoleLines.remove(0);
        }
    }
    notifyChanged();
}

void LuaScriptEditorModel::appendConsoleOutput(const std::string& text) {
    luaConsolePrinted(text);
}

juce::String LuaScriptEditorModel::getConsoleHistory() const {
    const juce::ScopedLock lock(consoleLock);
    return consoleLines.joinIntoString("\n");
}

void LuaScriptEditorModel::clearConsoleHistory() {
    luaConsoleCleared();
}

bool LuaScriptEditorModel::hasConsoleHistory() const {
    const juce::ScopedLock lock(consoleLock);
    return !consoleLines.isEmpty();
}

void LuaScriptEditorModel::setConsoleLineLimit(int newLimit) {
    consoleLineLimit = juce::jmax(1, newLimit);

    {
        const juce::ScopedLock lock(consoleLock);
        while (consoleLines.size() > consoleLineLimit) {
            consoleLines.remove(0);
        }
    }
}

void LuaScriptEditorModel::codeDocumentTextInserted(const juce::String&, int) {
    onDocumentEdited();
}

void LuaScriptEditorModel::codeDocumentTextDeleted(int, int) {
    onDocumentEdited();
}

void LuaScriptEditorModel::onDocumentEdited() {
    if (suppressDocumentCallbacks) {
        return;
    }

    if (!editInProgress) {
        editInProgress = true;
        if (onEditStarted) {
            onEditStarted();
        }
    }

    debounce.trigger([this] {
        commitCurrentCode();
    });
}

void LuaScriptEditorModel::commitCurrentCode() {
    validateCurrentCode();

    if (onCodeCommitted) {
        onCodeCommitted(getCode());
    }

    notifyChanged();
}

void LuaScriptEditorModel::validateCurrentCode() {
    LuaDiagnostic newDiagnostic;
    if (validateLuaSyntax) {
        newDiagnostic = LuaParser::validateScript(getCode());
    }

    const juce::ScopedLock lock(stateLock);
    diagnostic = newDiagnostic;
}

void LuaScriptEditorModel::onError(int lineNumber, juce::String error) {
    {
        const juce::ScopedLock lock(stateLock);
        diagnostic = { lineNumber, std::move(error) };
    }
    notifyChanged();
}

juce::String LuaScriptEditorModel::getId() {
    return id;
}

void LuaScriptEditorModel::luaConsolePrinted(const std::string& text) {
    {
        const juce::ScopedLock lock(consoleLock);
        consoleLines.add(juce::String(text));

        while (consoleLines.size() > consoleLineLimit) {
            consoleLines.remove(0);
        }
    }

    if (onConsoleChanged) {
        onConsoleChanged();
    }

    notifyChanged();
}

void LuaScriptEditorModel::luaConsoleCleared() {
    {
        const juce::ScopedLock lock(consoleLock);
        consoleLines.clear();
    }

    if (onConsoleChanged) {
        onConsoleChanged();
    }

    notifyChanged();
}

void LuaScriptEditorModel::handleAsyncUpdate() {
    sendChangeMessage();
}

void LuaScriptEditorModel::notifyChanged() {
    triggerAsyncUpdate();
}

} // namespace osci
