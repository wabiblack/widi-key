#pragma once

#include "PluginProcessor.h"
#include "Common.h"

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

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};

//==============================================================================
// İkonlu düğme (geçmiş, favori, kütüphane, ayarlar)
class IconButton : public juce::Button
{
public:
    enum class Icon { history, star, library, settings };

    IconButton (Icon i, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
};

class WaveView : public juce::Component
{
public:
    explicit WaveView (WidiKeyProcessor& p) : proc (p) {}

    void setData (std::vector<float> newPeaks, double newSeconds) { peaks = std::move (newPeaks); seconds = newSeconds; repaint(); }
    void setBusy (bool b) { if (busy != b) { busy = b; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    WidiKeyProcessor& proc;
    std::vector<float> peaks;
    double seconds = 0.0;
    bool busy = false, dragging = false;
};

class VariationTile : public juce::Component
{
public:
    std::function<void()> onPick;

    void setData (int newIndex, const std::vector<float>& peaks, const juce::String& noteText,
                  bool isSelected, bool isAvailable, bool isBusy, bool drumMode);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (available && onPick) onPick(); }

private:
    int index = 0;
    juce::String note;
    std::vector<float> mini;
    bool selected = false, available = false, busy = false, drum = false;
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
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& name, const juce::String& tip);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String name;
};

class ModernKeyboard : public juce::MidiKeyboardComponent
{
public:
    ModernKeyboard (juce::MidiKeyboardState& state, std::function<bool()> drumModeQuery)
        : juce::MidiKeyboardComponent (state, horizontalKeyboard), isDrumMode (std::move (drumModeQuery)) {}

    // Çoklu örnekte kayıtların kök notaları (tuş üzerinde işaretlenir)
    void setRootMarkers (std::vector<int> notes) { if (notes != rootMarkers) { rootMarkers = std::move (notes); repaint(); } }

    void drawWhiteNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;

private:
    void drawMarker (juce::Graphics&, int midiNoteNumber, juce::Rectangle<float> area, bool isDown);

    std::function<bool()> isDrumMode;
    std::vector<int> rootMarkers;
};

//==============================================================================
// Asıl arayüz: sabit 900×600 tasarlanır, editör bunu ölçekler.
class MainView : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int designWidth = 900;
    static constexpr int designHeight = 600;

    explicit MainView (WidiKeyProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void generate (bool similar);
    void openSettings();
    void showHistoryMenu();
    void showLibraryMenu();
    void askLibraryName();
    void setMode (int mode);
    void refreshFavorites();

    WidiKeyProcessor& proc;
    juce::Typeface::Ptr serif;

    juce::Label title, madeIn;
    LcdDisplay lcd;
    juce::TextButton melodicButton, multiButton, drumButton, cloudButton, localButton;
    IconButton libraryButton { IconButton::Icon::library, widi::tr ("Kütüphane ve dışa aktarma") };
    IconButton settingsButton { IconButton::Icon::settings, widi::tr ("Ayarlar") };

    IconButton historyButton { IconButton::Icon::history, widi::tr ("Tarif geçmişi ve favoriler") };
    IconButton favoriteButton { IconButton::Icon::star, widi::tr ("Bu tarifi favorilere ekle") };
    juce::TextEditor promptBox, negativeBox;
    juce::TextButton generateButton, similarButton;

    WaveView wave;
    VariationTile tiles[WidiKeyProcessor::maxVariations];

    juce::OwnedArray<ParamRow> rows;
    LcdDisplay seedBox;
    juce::TextButton lockButton, newSeedButton;

    juce::OwnedArray<Knob> knobs;
    juce::TextButton loopButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAttachment;
    ModernKeyboard keyboard;

    juce::Rectangle<int> logoArea, panelRect, knobStrip, keysRect, negIconArea;
    juce::Rectangle<int> groupRects[4];
    juce::StringArray favorites;
    int lastVersion = -1;
    int lastMode = -1;

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
