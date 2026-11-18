#pragma once

#include "PluginProcessor.h"

class WidiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WidiLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

class WaveformView : public juce::Component
{
public:
    void setPeaks (std::vector<float> newPeaks) { peaks = std::move (newPeaks); repaint(); }
    void setBusy (bool shouldBeBusy)            { if (busy != shouldBeBusy) { busy = shouldBeBusy; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    std::vector<float> peaks;
    bool busy = false;
};

class WidiKeyEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit WidiKeyEditor (WidiKeyProcessor&);
    ~WidiKeyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void generate();
    void openSettings();

    WidiKeyProcessor& proc;
    WidiLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 500 };

    juce::Label title, version;
    juce::TextButton cloudButton, localButton, settingsButton;

    juce::Label promptLabel;
    juce::TextEditor promptBox;
    juce::TextButton generateButton;
    juce::OwnedArray<juce::TextButton> chips;
    juce::StringArray chipPrompts;

    juce::Label sampleNameLabel, metaLabel, statusLabel, keysLabel;
    WaveformView waveform;
    juce::OwnedArray<Knob> knobs;
    juce::MidiKeyboardComponent keyboard;

    juce::Rectangle<int> logoArea, promptPanel, wavePanel, knobPanel, keysPanel;
    int lastVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WidiKeyEditor)
};
