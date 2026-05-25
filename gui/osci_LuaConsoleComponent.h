#pragma once

namespace osci {

class LuaConsoleComponent final : public juce::GroupComponent,
                                  private juce::Timer {
public:
    struct Options {
        juce::String title = "Lua Console";
        int headerHeight = 30;
        int maxLines = 100000;
        int scrollbarThickness = 10;
        juce::String clearButtonSvg;
        juce::String pauseButtonSvg;
        juce::Colour clearButtonColour = juce::Colours::red;
        juce::Colour pauseButtonColour = juce::Colours::white;
        juce::Colour pauseButtonOnColour = osci::Colours::accentColor();
    };

    LuaConsoleComponent();
    explicit LuaConsoleComponent(Options options);
    ~LuaConsoleComponent() override;

    void print(const std::string& text);
    void clear(bool forceClear = false);
    void setConsoleOpen(bool open);
    bool getConsoleOpen() const;

    void setButtonSvgSources(juce::String clearSvg, juce::String pauseSvg);
    void setButtonColours(juce::Colour clearColour, juce::Colour pauseColour, juce::Colour pauseOnColour);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void timerCallback() override;
    void updateColours();

    static juce::String makeSvg(const char* pathData);
    static const juce::String& clearSvg();
    static const juce::String& pauseSvg();

    Options options;
    bool consoleOpen = false;
    juce::SpinLock lock;
    std::string buffer;
    juce::CodeDocument document;
    juce::CodeEditorComponent console { document, nullptr };
    juce::Label emptyConsoleLabel { "emptyConsoleLabel", "Console is empty" };
    int consoleLines = 0;

    osci::SvgButton clearConsoleButton { "clearConsole", clearSvg(), juce::Colours::red };
    osci::SvgButton pauseConsoleButton { "pauseConsole", pauseSvg(), juce::Colours::white, osci::Colours::accentColor() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaConsoleComponent)
};

} // namespace osci
