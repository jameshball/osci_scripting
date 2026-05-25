#pragma once

namespace osci {

class LuaScriptEditorComponent final : public juce::Component,
                                       private juce::ChangeListener,
                                       private juce::Button::Listener {
public:
    struct Options {
        bool useLuaTokeniser = true;
        bool showTitle = true;
        bool showConsole = true;
        bool showExpandButton = false;
        bool showHelpButton = false;
        bool showResetButton = false;
        bool consoleInitiallyOpen = false;
        bool compact = false;
        bool legacyGroupChrome = false;
        juce::CodeTokeniser* externalTokeniser = nullptr;
        juce::String helpButtonSvg;
        juce::String resetButtonSvg;
        juce::Colour buttonColour;
        juce::Colour buttonOnColour;
    };

    explicit LuaScriptEditorComponent(LuaScriptEditorModel& model);
    LuaScriptEditorComponent(LuaScriptEditorModel& model, Options options);
    ~LuaScriptEditorComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void setConsoleOpen(bool shouldBeOpen);
    bool isConsoleOpen() const;
    void setTitleVisible(bool shouldBeVisible);
    void setExpandButtonVisible(bool shouldBeVisible);
    void setHelpButtonVisible(bool shouldBeVisible);
    void setResetButtonVisible(bool shouldBeVisible);
    void setCompact(bool shouldBeCompact);

    juce::CodeEditorComponent& getEditor();
    const juce::CodeEditorComponent& getEditor() const;

    std::function<void(bool)> onConsoleOpenChanged;

private:
    class Editor;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void buttonClicked(juce::Button* button) override;
    void updateTheme();
    void updateConsoleDocument();
    void resizedLegacyGroupChrome();
    juce::Rectangle<int> getHeaderBounds() const;
    juce::Rectangle<int> getControlsBounds() const;
    juce::Rectangle<int> getConsoleBounds(juce::Rectangle<int> bounds) const;

    static juce::String makeSvg(const char* pathData);
    static const juce::String& terminalSvg();
    static const juce::String& clearSvg();
    static const juce::String& pauseSvg();
    static const juce::String& expandSvg();
    static const juce::String& helpSvg();
    static const juce::String& resetSvg();

    LuaScriptEditorModel& model;
    Options options;
    std::unique_ptr<juce::LuaTokeniser> luaTokeniser;
    std::unique_ptr<Editor> editor;
    juce::CodeDocument consoleDocument;
    juce::CodeEditorComponent consoleEditor { consoleDocument, nullptr };
    juce::Label emptyConsoleLabel { "emptyConsoleLabel", "Console is empty" };
    osci::SvgButton consoleButton { "luaConsoleToggle", terminalSvg(), osci::Colours::textSubtle(), osci::Colours::accentColor() };
    osci::SvgButton clearConsoleButton { "luaConsoleClear", clearSvg(), osci::Colours::danger() };
    osci::SvgButton pauseConsoleButton { "luaConsolePause", pauseSvg(), osci::Colours::textSubtle(), osci::Colours::accentColor() };
    osci::SvgButton expandButton { "luaEditorExpand", expandSvg(), osci::Colours::textSubtle(), osci::Colours::text() };
    osci::SvgButton helpButton { "luaHelp", helpSvg(), osci::Colours::textSubtle(), osci::Colours::text() };
    osci::SvgButton resetButton { "luaReset", resetSvg(), osci::Colours::textSubtle(), osci::Colours::text() };
    juce::String lastConsoleText;
    bool consoleOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaScriptEditorComponent)
};

} // namespace osci
