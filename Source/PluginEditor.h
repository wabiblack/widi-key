#pragma once

#include "PluginProcessor.h"

class WidiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WidiLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

//==============================================================================
class WaveView : public juce::Component
{
public:
    explicit WaveView (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

    void setData (std::vector<float> newPeaks, double newSeconds) { peaks = std::move (newPeaks); seconds = newSeconds; repaint(); }
    void setBusy (bool b) { if (busy != b) { busy = b; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<float> peaks;
    double seconds = 0.0;
    bool busy = false;
};

class VariationTile : public juce::Component
{
public:
    std::function<void()> onPick;

    void setData (int newIndex, const std::vector<float>& peaks, int newSeed, bool isSelected, bool isAvailable, bool isBusy);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (available && onPick) onPick(); }

private:
    int index = 0, seed = -1;
    std::vector<float> mini;
    bool selected = false, available = false, busy = false;
};

class LcdDisplay : public juce::Component
{
public:
    void setText (const juce::String& left, const juce::String& right, bool isError);
    void paint (juce::Graphics&) override;

private:
    juce::String leftText, rightText;
    bool error = false;
};

class ParamRow : public juce::Component
{
public:
    ParamRow (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name, const juce::String& hint);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name, hint;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name;
};

//==============================================================================
// Asıl arayüz: sabit 820×540 tasarlanır, editör bunu ölçekler.
class MainView : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int designWidth = 820;
    static constexpr int designHeight = 540;

    explicit MainView (WidiKeyProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void generate();
    void openSettings();
    void refreshEngineButtons();

    WidiKeyProcessor& proc;
    juce::Typeface::Ptr serif;

    juce::Label title, madeIn;
    LcdDisplay lcd;
    juce::TextButton cloudButton, localButton, settingsButton;

    juce::TextEditor promptBox;
    juce::TextButton generateButton;

    WaveView wave;
    VariationTile tiles[WidiKeyProcessor::maxVariations];

    juce::OwnedArray<ParamRow> rows;
    LcdDisplay seedBox;
    juce::TextButton lockButton, newSeedButton;

    juce::OwnedArray<Knob> knobs;
    juce::MidiKeyboardComponent keyboard;

    juce::Rectangle<int> logoArea, panelRect, knobStrip, keysRect;
    juce::Rectangle<int> groupRects[3];
    int lastVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

class WidiKeyEditor : public juce::AudioProcessorEditor
{
public:
    explicit WidiKeyEditor (WidiKeyProcessor&);
    ~WidiKeyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    WidiLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    MainView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WidiKeyEditor)
};
