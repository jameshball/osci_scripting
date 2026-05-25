#include "osci_LuaConsoleComponent.h"

namespace osci {

LuaConsoleComponent::LuaConsoleComponent()
    : LuaConsoleComponent(Options {}) {
}

LuaConsoleComponent::LuaConsoleComponent(Options optionsToUse)
    : options(std::move(optionsToUse)) {
    setOpaque(false);
    setText(options.title);

    console.setReadOnly(true);
    console.setLineNumbersShown(false);
    console.setScrollbarThickness(0);
    document.getUndoManager().setMaxNumberOfStoredUnits(0, 0);

    clearConsoleButton.onClick = [this] {
        clear(true);
    };
    clearConsoleButton.setTooltip("Clear console output. You can clear the console from Lua with the clear() function.");

    pauseConsoleButton.setClickingTogglesState(true);
    pauseConsoleButton.onClick = [this] {
        console.setScrollbarThickness(pauseConsoleButton.getToggleState() ? options.scrollbarThickness : 0);
    };
    pauseConsoleButton.setTooltip("Pause console output, and show a scrollbar to navigate through the console history.");

    if (options.clearButtonSvg.isNotEmpty() || options.pauseButtonSvg.isNotEmpty()) {
        setButtonSvgSources(options.clearButtonSvg, options.pauseButtonSvg);
    }
    setButtonColours(options.clearButtonColour, options.pauseButtonColour, options.pauseButtonOnColour);

    addAndMakeVisible(console);
    addAndMakeVisible(clearConsoleButton);
    addAndMakeVisible(pauseConsoleButton);
    addAndMakeVisible(emptyConsoleLabel);

    emptyConsoleLabel.setJustificationType(juce::Justification::centred);
    updateColours();
}

LuaConsoleComponent::~LuaConsoleComponent() {
    stopTimer();
}

void LuaConsoleComponent::print(const std::string& text) {
    juce::SpinLock::ScopedLockType l(lock);

    if (consoleOpen && !pauseConsoleButton.getToggleState()) {
        buffer += text + "\n";
        consoleLines++;
    }
}

void LuaConsoleComponent::clear(bool forceClear) {
    juce::SpinLock::ScopedLockType l(lock);

    if (forceClear || !pauseConsoleButton.getToggleState()) {
        document.replaceAllContent("");
        document.clearUndoHistory();
        consoleLines = 0;
        buffer.clear();

        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<LuaConsoleComponent>(this)] {
            if (safeThis == nullptr) {
                return;
            }

            safeThis->console.setVisible(false);
            safeThis->emptyConsoleLabel.setVisible(true);
        });
    }
}

void LuaConsoleComponent::setConsoleOpen(bool open) {
    juce::SpinLock::ScopedLockType l(lock);

    consoleOpen = open;
    console.setVisible(open);
    if (open) {
        startTimerHz(10);
    } else {
        stopTimer();
    }
}

bool LuaConsoleComponent::getConsoleOpen() const {
    return consoleOpen;
}

void LuaConsoleComponent::setButtonSvgSources(juce::String clearSvgToUse, juce::String pauseSvgToUse) {
    if (clearSvgToUse.isNotEmpty()) {
        clearConsoleButton.setSvgSources(std::move(clearSvgToUse));
    }
    if (pauseSvgToUse.isNotEmpty()) {
        pauseConsoleButton.setSvgSources(std::move(pauseSvgToUse));
    }
}

void LuaConsoleComponent::setButtonColours(juce::Colour clearColour, juce::Colour pauseColour, juce::Colour pauseOnColour) {
    clearConsoleButton.setColours(clearColour, clearColour);
    pauseConsoleButton.setColours(pauseColour, pauseOnColour);
}

void LuaConsoleComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    auto alpha = isEnabled() ? 1.0f : 0.5f;
    auto radius = static_cast<float>(osci::LookAndFeel::RECT_RADIUS);

    g.setColour(findColour(osci::groupComponentBackgroundColourId).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, radius);

    auto header = bounds;
    header.setHeight(static_cast<float>(options.headerHeight));

    juce::Path headerPath;
    if (getHeight() <= options.headerHeight) {
        headerPath.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius, radius);
    } else {
        headerPath.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius, radius, true, true, false, false);
    }

    g.setColour(findColour(osci::groupComponentHeaderColourId).withMultipliedAlpha(alpha));
    g.fillPath(headerPath);

    g.setColour(findColour(juce::GroupComponent::textColourId).withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawText(getText(), header.reduced(32.0f, 0.0f).withY(header.getY() + 7.0f).withHeight(15.0f), juce::Justification::centredLeft, true);
}

void LuaConsoleComponent::resized() {
    auto topBar = getLocalBounds().removeFromTop(options.headerHeight);
    auto area = getLocalBounds().withTrimmedTop(options.headerHeight);
    area.removeFromBottom(juce::jmin(area.getHeight(), osci::LookAndFeel::RECT_RADIUS));
    console.setBounds(area);
    emptyConsoleLabel.setBounds(area);

    clearConsoleButton.setBounds(topBar.removeFromRight(options.headerHeight).withSizeKeepingCentre(20, 20));
    pauseConsoleButton.setBounds(topBar.removeFromRight(options.headerHeight).withSizeKeepingCentre(20, 20));
}

void LuaConsoleComponent::lookAndFeelChanged() {
    updateColours();
}

void LuaConsoleComponent::timerCallback() {
    juce::SpinLock::ScopedLockType l(lock);

    if (consoleOpen && !pauseConsoleButton.getToggleState()) {
        document.insertText(juce::CodeDocument::Position(document, std::numeric_limits<int>::max(), std::numeric_limits<int>::max()), buffer);
        buffer.clear();

        if (consoleLines > options.maxLines) {
            int linesToClear = static_cast<int>(static_cast<float>(consoleLines) * 0.9f);
            document.deleteSection(juce::CodeDocument::Position(document, 0, 0), juce::CodeDocument::Position(document, linesToClear, 0));
            consoleLines -= linesToClear;
        }

        console.moveCaretToTop(false);
        console.moveCaretToEnd(false);
        console.scrollDown();

        if (consoleLines > 0) {
            console.setVisible(true);
            emptyConsoleLabel.setVisible(false);
        }
    }
}

void LuaConsoleComponent::updateColours() {
    console.setColour(juce::CodeEditorComponent::backgroundColourId, findColour(osci::groupComponentBackgroundColourId, true));
    console.setColour(juce::CodeEditorComponent::defaultTextColourId, findColour(juce::CodeEditorComponent::defaultTextColourId, true));
    console.setColour(juce::CodeEditorComponent::highlightColourId, findColour(juce::CodeEditorComponent::highlightColourId, true));
    emptyConsoleLabel.setColour(juce::Label::textColourId, findColour(juce::GroupComponent::textColourId, true).withMultipliedAlpha(0.8f));
}

juce::String LuaConsoleComponent::makeSvg(const char* pathData) {
    return juce::String("<svg xmlns=\"http://www.w3.org/2000/svg\" height=\"24\" viewBox=\"0 -960 960 960\" width=\"24\"><path d=\"")
         + pathData
         + "\"/></svg>";
}

const juce::String& LuaConsoleComponent::clearSvg() {
    static const juce::String svg = makeSvg("M280-120q-33 0-56.5-23.5T200-200v-520h-40v-80h200v-40h240v40h200v80h-40v520q0 33-23.5 56.5T680-120H280Zm400-600H280v520h400v-520ZM360-280h80v-360h-80v360Zm160 0h80v-360h-80v360Z");
    return svg;
}

const juce::String& LuaConsoleComponent::pauseSvg() {
    static const juce::String svg = makeSvg("M320-200v-560h120v560H320Zm200 0v-560h120v560H520Z");
    return svg;
}

} // namespace osci
