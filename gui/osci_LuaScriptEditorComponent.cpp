#include "osci_LuaScriptEditorComponent.h"

namespace osci {

class LuaScriptEditorComponent::Editor final : public juce::CodeEditorComponent {
public:
    Editor(juce::CodeDocument& document, juce::CodeTokeniser* tokeniser, LuaScriptEditorModel& modelToUse)
        : juce::CodeEditorComponent(document, tokeniser), model(modelToUse) {
        setName("Lua Code Editor");
        setScrollbarThickness(8);
        setLineNumbersShown(true);
    }

    void paint(juce::Graphics& g) override {
        juce::CodeEditorComponent::paint(g);

        const auto diagnostic = model.getDiagnostic();
        if (!diagnostic.hasError()) {
            return;
        }

        const auto lineIndex = diagnostic.lineNumber - 1;
        const auto firstVisibleLine = getFirstLineOnScreen();
        const auto lastVisibleLine = firstVisibleLine + getNumLinesOnScreen();
        if (lineIndex < firstVisibleLine || lineIndex > lastVisibleLine) {
            return;
        }

        const auto lineBounds = getErrorLineBounds(diagnostic.lineNumber);
        if (lineBounds.isEmpty()) {
            return;
        }

        const auto line = getDocument().getLine(lineIndex);
        const auto leadingWhitespace = line.length() - line.trimStart().length();
        const auto charWidth = static_cast<float>(getCharWidth());
        const auto start = charWidth * static_cast<float>(leadingWhitespace);
        const auto width = juce::jmax(charWidth * 2.0f, charWidth * static_cast<float>(line.length()));
        const auto squiggleHeight = 3.0f;
        const auto lineIncrement = 2.5f;
        const auto yMax = lineBounds.getBottom() - squiggleHeight + 2.0f;
        const auto yMin = lineBounds.getBottom() + 2.0f;

        juce::Path squiggle;
        squiggle.startNewSubPath(lineBounds.getX() + start, yMax);
        for (float x = start; x < width; x += 2.0f * lineIncrement) {
            squiggle.lineTo(lineBounds.getX() + x, yMax);
            squiggle.lineTo(lineBounds.getX() + x + lineIncrement, yMin);
        }

        g.setColour(osci::Colours::danger());
        g.strokePath(squiggle, juce::PathStrokeType(1.1f));

        if (errorLineHovered) {
            auto messageBounds = lineBounds.translated(0.0f, lineBounds.getHeight()).withHeight(juce::jmax(20.0f, lineBounds.getHeight()));
            messageBounds = messageBounds.getIntersection(getLocalBounds().toFloat().reduced(4.0f));

            g.setColour(osci::Colours::danger().withAlpha(0.96f));
            g.fillRoundedRectangle(messageBounds, 4.0f);
            g.setColour(osci::Colours::textOnAccent());
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            osci::LookAndFeelHelpers::drawFittedText(g,
                                                     diagnostic.message,
                                                     messageBounds.reduced(7.0f, 1.0f),
                                                     juce::Justification::centredLeft,
                                                     1,
                                                     0.6f);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override {
        juce::CodeEditorComponent::mouseMove(event);
        const auto diagnostic = model.getDiagnostic();
        const auto hovered = diagnostic.hasError()
                           && getErrorLineBounds(diagnostic.lineNumber).contains(event.position);

        if (hovered != errorLineHovered) {
            errorLineHovered = hovered;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& event) override {
        juce::CodeEditorComponent::mouseExit(event);
        if (errorLineHovered) {
            errorLineHovered = false;
            repaint();
        }
    }

    void focusLost(FocusChangeType cause) override {
        model.flushPendingEdit();
        juce::CodeEditorComponent::focusLost(cause);
    }

private:
    juce::Rectangle<float> getErrorLineBounds(int oneBasedLineNumber) const {
        const auto lineIndex = oneBasedLineNumber - 1;
        const auto firstVisibleLine = getFirstLineOnScreen();
        const auto y = static_cast<float>(getLineHeight() * (lineIndex - firstVisibleLine));
        const auto gutterWidth = 35.0f;
        return { gutterWidth, y, juce::jmax(0.0f, static_cast<float>(getWidth()) - gutterWidth), static_cast<float>(getLineHeight()) };
    }

    LuaScriptEditorModel& model;
    bool errorLineHovered = false;
};

LuaScriptEditorComponent::LuaScriptEditorComponent(LuaScriptEditorModel& modelToUse)
    : LuaScriptEditorComponent(modelToUse, Options {}) {
}

LuaScriptEditorComponent::LuaScriptEditorComponent(LuaScriptEditorModel& modelToUse, Options optionsToUse)
    : model(modelToUse),
      options(optionsToUse),
      consoleOpen(optionsToUse.consoleInitiallyOpen) {
    setOpaque(false);
    setInterceptsMouseClicks(true, true);

    juce::CodeTokeniser* tokeniser = nullptr;
    if (options.useLuaTokeniser) {
        luaTokeniser = std::make_unique<juce::LuaTokeniser>();
        tokeniser = luaTokeniser.get();
    }

    editor = std::make_unique<Editor>(model.getDocument(), tokeniser, model);
    addAndMakeVisible(*editor);

    consoleDocument.getUndoManager().setMaxNumberOfStoredUnits(0, 0);
    consoleEditor.setName("Lua Console");
    consoleEditor.setReadOnly(true);
    consoleEditor.setLineNumbersShown(false);
    consoleEditor.setScrollbarThickness(0);
    addAndMakeVisible(consoleEditor);
    addAndMakeVisible(emptyConsoleLabel);

    emptyConsoleLabel.setJustificationType(juce::Justification::centred);

    for (auto* button : { &consoleButton, &clearConsoleButton, &pauseConsoleButton, &expandButton, &helpButton, &resetButton }) {
        button->addListener(this);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible(*button);
    }

    consoleButton.setTooltip("Show Lua console");
    clearConsoleButton.setTooltip("Clear Lua console");
    pauseConsoleButton.setTooltip("Pause console scrolling");
    expandButton.setTooltip("Expand Lua editor");
    helpButton.setTooltip("Lua scripting reference");
    resetButton.setTooltip("Reset Lua state");

    consoleButton.setToggleState(consoleOpen, juce::dontSendNotification);
    pauseConsoleButton.setClickingTogglesState(true);

    model.addChangeListener(this);
    updateTheme();
    updateConsoleDocument();
    resized();
}

LuaScriptEditorComponent::~LuaScriptEditorComponent() {
    model.removeChangeListener(this);
    for (auto* button : { &consoleButton, &clearConsoleButton, &pauseConsoleButton, &expandButton, &helpButton, &resetButton }) {
        button->removeListener(this);
    }
}

void LuaScriptEditorComponent::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const auto corner = options.compact ? 6.0f : 8.0f;

    g.setColour(osci::Colours::codeBackground());
    g.fillRoundedRectangle(bounds, corner);
    g.setColour(osci::Colours::outlineSubtle());
    g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);

    auto header = getHeaderBounds().toFloat();
    if (!header.isEmpty()) {
        g.setColour(osci::Colours::surfaceRaised().withAlpha(osci::Theme::isDark() ? 0.42f : 0.72f));
        g.fillRoundedRectangle(header.reduced(1.0f, 1.0f), corner - 1.0f);

        if (options.showTitle) {
            auto titleBounds = header.reduced(10.0f, 0.0f);
            titleBounds.removeFromRight(static_cast<float>(getControlsBounds().getWidth() + 4));
            g.setColour(osci::Colours::text());
            g.setFont(juce::Font(juce::FontOptions(options.compact ? 12.0f : 13.5f, juce::Font::bold)));
            osci::LookAndFeelHelpers::drawFittedText(g, model.getDisplayName(), titleBounds, juce::Justification::centredLeft, 1);
        }
    }
}

void LuaScriptEditorComponent::resized() {
    auto bounds = getLocalBounds().reduced(1);

    const auto headerHeight = options.showTitle ? (options.compact ? 24 : 30) : 23;
    auto header = bounds.removeFromTop(headerHeight);
    auto controls = header.removeFromRight(juce::jmin(header.getWidth(), 180));

    const auto placeButton = [&controls](juce::Button& button) {
        button.setBounds(controls.removeFromRight(24).withSizeKeepingCentre(18, 18));
        controls.removeFromRight(2);
    };

    resetButton.setVisible(options.showResetButton);
    helpButton.setVisible(options.showHelpButton);
    expandButton.setVisible(options.showExpandButton);
    consoleButton.setVisible(options.showConsole);
    clearConsoleButton.setVisible(options.showConsole && consoleOpen);
    pauseConsoleButton.setVisible(options.showConsole && consoleOpen);

    if (options.showHelpButton) {
        placeButton(helpButton);
    }
    if (options.showResetButton) {
        placeButton(resetButton);
    }
    if (options.showExpandButton) {
        placeButton(expandButton);
    }
    if (options.showConsole && consoleOpen) {
        placeButton(clearConsoleButton);
        placeButton(pauseConsoleButton);
    }
    if (options.showConsole) {
        placeButton(consoleButton);
    }

    bounds.reduce(4, 4);

    auto consoleBounds = getConsoleBounds(bounds);
    if (!consoleBounds.isEmpty()) {
        bounds.removeFromBottom(consoleBounds.getHeight() + 5);
    }

    if (editor != nullptr) {
        editor->setBounds(bounds);
    }

    consoleEditor.setBounds(consoleBounds);
    emptyConsoleLabel.setBounds(consoleBounds);
    consoleEditor.setVisible(options.showConsole && consoleOpen && model.hasConsoleHistory());
    emptyConsoleLabel.setVisible(options.showConsole && consoleOpen && !model.hasConsoleHistory());
}

void LuaScriptEditorComponent::lookAndFeelChanged() {
    updateTheme();
    repaint();
}

void LuaScriptEditorComponent::setConsoleOpen(bool shouldBeOpen) {
    if (consoleOpen == shouldBeOpen) {
        return;
    }

    consoleOpen = shouldBeOpen;
    consoleButton.setToggleState(consoleOpen, juce::dontSendNotification);
    updateConsoleDocument();
    resized();
    repaint();

    if (onConsoleOpenChanged) {
        onConsoleOpenChanged(consoleOpen);
    }
}

bool LuaScriptEditorComponent::isConsoleOpen() const {
    return consoleOpen;
}

void LuaScriptEditorComponent::setTitleVisible(bool shouldBeVisible) {
    options.showTitle = shouldBeVisible;
    resized();
    repaint();
}

void LuaScriptEditorComponent::setExpandButtonVisible(bool shouldBeVisible) {
    options.showExpandButton = shouldBeVisible;
    resized();
}

void LuaScriptEditorComponent::setHelpButtonVisible(bool shouldBeVisible) {
    options.showHelpButton = shouldBeVisible;
    resized();
}

void LuaScriptEditorComponent::setResetButtonVisible(bool shouldBeVisible) {
    options.showResetButton = shouldBeVisible;
    resized();
}

void LuaScriptEditorComponent::setCompact(bool shouldBeCompact) {
    options.compact = shouldBeCompact;
    resized();
    repaint();
}

juce::CodeEditorComponent& LuaScriptEditorComponent::getEditor() {
    return *editor;
}

const juce::CodeEditorComponent& LuaScriptEditorComponent::getEditor() const {
    return *editor;
}

void LuaScriptEditorComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    updateConsoleDocument();

    if (editor != nullptr) {
        editor->repaint();
    }

    resized();
    repaint();
}

void LuaScriptEditorComponent::buttonClicked(juce::Button* button) {
    if (button == &consoleButton) {
        setConsoleOpen(consoleButton.getToggleState());
    } else if (button == &clearConsoleButton) {
        pauseConsoleButton.setToggleState(false, juce::dontSendNotification);
        model.clearConsoleHistory();
        updateConsoleDocument();
    } else if (button == &pauseConsoleButton) {
        consoleEditor.setScrollbarThickness(pauseConsoleButton.getToggleState() ? 8 : 0);
        if (!pauseConsoleButton.getToggleState()) {
            updateConsoleDocument();
        }
    } else if (button == &expandButton) {
        model.flushPendingEdit();
        if (model.onExpandRequested) {
            model.onExpandRequested();
        }
    } else if (button == &helpButton) {
        if (model.onHelpRequested) {
            model.onHelpRequested();
        }
    } else if (button == &resetButton) {
        if (model.onResetRequested) {
            model.onResetRequested();
        }
    }
}

void LuaScriptEditorComponent::updateTheme() {
    if (editor != nullptr) {
        editor->setColourScheme(osci::LookAndFeel::getDefaultColourScheme());
        editor->setColour(juce::CodeEditorComponent::backgroundColourId, osci::Colours::codeBackground());
        editor->setColour(juce::CodeEditorComponent::defaultTextColourId, osci::Colours::codeForeground());
        editor->setColour(juce::CodeEditorComponent::lineNumberBackgroundId, osci::Colours::surfaceSunken());
        editor->setColour(juce::CodeEditorComponent::lineNumberTextId, osci::Colours::textSubtle());
        editor->setColour(juce::CodeEditorComponent::highlightColourId, osci::Colours::codeSelection());
    }

    consoleEditor.setColour(juce::CodeEditorComponent::backgroundColourId, osci::Colours::surfaceSunken());
    consoleEditor.setColour(juce::CodeEditorComponent::defaultTextColourId, osci::Colours::text());
    consoleEditor.setColour(juce::CodeEditorComponent::highlightColourId, osci::Colours::codeSelection());
    emptyConsoleLabel.setColour(juce::Label::textColourId, osci::Colours::textSubtle());

    consoleButton.setColours(osci::Colours::textSubtle(), osci::Colours::accentColor());
    clearConsoleButton.setColours(osci::Colours::danger(), osci::Colours::danger());
    pauseConsoleButton.setColours(osci::Colours::textSubtle(), osci::Colours::accentColor());
    expandButton.setColours(osci::Colours::textSubtle(), osci::Colours::text());
    helpButton.setColours(osci::Colours::textSubtle(), osci::Colours::text());
    resetButton.setColours(osci::Colours::textSubtle(), osci::Colours::text());
}

void LuaScriptEditorComponent::updateConsoleDocument() {
    if (pauseConsoleButton.getToggleState()) {
        return;
    }

    const auto text = model.getConsoleHistory();
    if (text == lastConsoleText) {
        return;
    }

    lastConsoleText = text;
    consoleDocument.replaceAllContent(text);
    consoleDocument.clearUndoHistory();
    consoleEditor.moveCaretToEnd(false);
    consoleEditor.scrollDown();
}

juce::Rectangle<int> LuaScriptEditorComponent::getHeaderBounds() const {
    const auto headerHeight = options.showTitle ? (options.compact ? 24 : 30) : 23;
    return getLocalBounds().reduced(1).removeFromTop(headerHeight);
}

juce::Rectangle<int> LuaScriptEditorComponent::getControlsBounds() const {
    auto header = getHeaderBounds();
    return header.removeFromRight(juce::jmin(header.getWidth(), 180));
}

juce::Rectangle<int> LuaScriptEditorComponent::getConsoleBounds(juce::Rectangle<int> bounds) const {
    if (!options.showConsole || !consoleOpen) {
        return {};
    }

    const auto height = juce::jlimit(54, options.compact ? 112 : 220, bounds.getHeight() / (options.compact ? 3 : 4));
    return bounds.removeFromBottom(height);
}

juce::String LuaScriptEditorComponent::makeSvg(const char* pathData) {
    return juce::String("<svg xmlns=\"http://www.w3.org/2000/svg\" height=\"24\" viewBox=\"0 -960 960 960\" width=\"24\"><path d=\"")
         + pathData
         + "\"/></svg>";
}

const juce::String& LuaScriptEditorComponent::terminalSvg() {
    static const juce::String svg = makeSvg("M160-160v-640h640v640H160Zm80-80h480v-480H240v480Zm80-120 56-56-80-80 80-80-56-56-136 136 136 136Zm176 0h184v-80H496v80Z");
    return svg;
}

const juce::String& LuaScriptEditorComponent::clearSvg() {
    static const juce::String svg = makeSvg("M280-120q-33 0-56.5-23.5T200-200v-520h-40v-80h200v-40h240v40h200v80h-40v520q0 33-23.5 56.5T680-120H280Zm400-600H280v520h400v-520ZM360-280h80v-360h-80v360Zm160 0h80v-360h-80v360Z");
    return svg;
}

const juce::String& LuaScriptEditorComponent::pauseSvg() {
    static const juce::String svg = makeSvg("M320-200v-560h120v560H320Zm200 0v-560h120v560H520Z");
    return svg;
}

const juce::String& LuaScriptEditorComponent::expandSvg() {
    static const juce::String svg = makeSvg("M200-200v-240h80v104l136-136 56 56-136 136h104v80H200Zm344-288-56-56 136-136H520v-80h240v240h-80v-104L544-488Z");
    return svg;
}

const juce::String& LuaScriptEditorComponent::helpSvg() {
    static const juce::String svg = makeSvg("M440-280h80v-240h-80v240Zm40-320q17 0 28.5-11.5T520-640q0-17-11.5-28.5T480-680q-17 0-28.5 11.5T440-640q0 17 11.5 28.5T480-600Zm0 520q-83 0-156-31.5T197-197q-54-54-85.5-127T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 83-31.5 156T763-197q-54 54-127 85.5T480-80Z");
    return svg;
}

const juce::String& LuaScriptEditorComponent::resetSvg() {
    static const juce::String svg = makeSvg("M480-160q-134 0-227-93t-93-227q0-134 93-227t227-93q69 0 132 28.5T720-690v-110h80v280H520v-80h168q-32-56-87.5-88T480-720q-100 0-170 70t-70 170q0 100 70 170t170 70q77 0 139-44t87-116h84q-28 106-114 173t-196 67Z");
    return svg;
}

} // namespace osci
