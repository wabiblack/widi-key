#include "PluginEditor.h"
#include "Common.h"

using widi::tr;
namespace col = widi::col;

namespace
{
    juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size).withStyle (bold ? "Bold" : "Regular"));
    }

    void styleLabel (juce::Label& label, float size, juce::Colour colour, bool bold = false)
    {
        label.setFont (font (size, bold));
        label.setColour (juce::Label::textColourId, colour);
        label.setBorderSize ({});
        label.setInterceptsMouseClicks (false, false);
    }
}

//==============================================================================
WidiLookAndFeel::WidiLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::TextButton::buttonColourId, col::chip);
    setColour (juce::TextButton::buttonOnColourId, col::segOn);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::border);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::border);
    setColour (juce::TextEditor::backgroundColourId, col::field);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::border);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::TooltipWindow::backgroundColourId, col::chip);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::border);
}

void WidiLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
    const auto size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area   = bounds.withSizeKeepingCentre (size, size);
    const auto radius = size * 0.5f - 3.0f;
    const auto centre = area.getCentre();
    const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (col::knobTrack);
    g.strokePath (track, stroke);

    const auto angle = startAngle + sliderPos * (endAngle - startAngle);
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
    g.setColour (col::accent);
    g.strokePath (value, stroke);

    g.setColour (col::knobCap);
    g.fillEllipse (area.reduced (size * 0.27f));

    g.setColour (col::text);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.08f, angle),
                  centre.getPointOnCircumference (radius * 0.42f, angle) }, 2.0f);
}

void WidiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = (float) button.getProperties().getWithDefault ("radius", 10.0f);

    auto base = backgroundColour;

    if (! button.isEnabled())
        base = base.withMultipliedAlpha (0.55f);
    else if (down)
        base = base.darker (0.15f);
    else if (highlighted)
        base = base.brighter (0.08f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, radius);

    if ((bool) button.getProperties().getWithDefault ("outline", false))
    {
        g.setColour (col::border.brighter (0.15f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

juce::Font WidiLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const bool bold = (bool) button.getProperties().getWithDefault ("bold", false);
    return font (juce::jmin (15.0f, (float) buttonHeight * 0.45f), bold);
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (col::field);
    g.fillRoundedRectangle (bounds, 10.0f);

    if (peaks.empty())
    {
        g.setColour (col::dim);
        g.setFont (font (13.0f));
        g.drawText (tr ("Henüz ses yok. Yukarıya bir tarif yaz ve Üret'e bas."), getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto inner = bounds.reduced (10.0f, 8.0f);
    const auto barWidth = inner.getWidth() / (float) peaks.size();
    g.setColour (col::accent.withAlpha (busy ? 0.25f : 1.0f));

    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const float h = juce::jmax (2.0f, peaks[i] * inner.getHeight());
        const float x = inner.getX() + (float) i * barWidth;
        g.fillRoundedRectangle (x + 0.5f, inner.getCentreY() - h * 0.5f, juce::jmax (1.0f, barWidth - 1.5f), h, 1.0f);
    }
}

//==============================================================================
WidiKeyEditor::WidiKeyEditor (WidiKeyProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf);

    // Üst bar
    title.setText ("widi-key", juce::dontSendNotification);
    styleLabel (title, 24.0f, col::text, true);
    addAndMakeVisible (title);

    version.setText ("v0.1", juce::dontSendNotification);
    styleLabel (version, 11.0f, col::dim);
    version.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (version);

    cloudButton.setButtonText (tr ("Bulut"));
    cloudButton.setToggleState (true, juce::dontSendNotification);
    cloudButton.setColour (juce::TextButton::buttonColourId, col::chip);
    cloudButton.setTooltip (tr ("Sesi ElevenLabs ile bulutta üretir (Mac'i yormaz)"));
    addAndMakeVisible (cloudButton);

    localButton.setButtonText (tr ("Yerel (Mac)"));
    localButton.setEnabled (false);
    localButton.setColour (juce::TextButton::buttonColourId, col::chip);
    localButton.setTooltip (tr ("Yakında: M1'de internetsiz ve limitsiz üretim"));
    addAndMakeVisible (localButton);

    settingsButton.setButtonText (tr ("Ayarlar"));
    settingsButton.getProperties().set ("outline", true);
    settingsButton.onClick = [this] { openSettings(); };
    addAndMakeVisible (settingsButton);

    // Tarif kutusu
    promptLabel.setText (tr ("SESİ TARİF ET"), juce::dontSendNotification);
    styleLabel (promptLabel, 12.0f, col::dim, true);
    addAndMakeVisible (promptLabel);

    promptBox.setFont (font (16.0f));
    promptBox.setTextToShowWhenEmpty (tr ("örn. warm dusty music box, single note"), col::dim);
    promptBox.setJustification (juce::Justification::centredLeft);
    promptBox.setIndents (16, 0);
    promptBox.setText (proc.getPrompt(), false);
    promptBox.onReturnKey = [this] { generate(); };
    addAndMakeVisible (promptBox);

    generateButton.setButtonText (tr ("Üret"));
    generateButton.setColour (juce::TextButton::buttonColourId, col::accent);
    generateButton.setColour (juce::TextButton::textColourOffId, col::onAccent);
    generateButton.getProperties().set ("bold", true);
    generateButton.getProperties().set ("radius", 12.0f);
    generateButton.onClick = [this] { generate(); };
    addAndMakeVisible (generateButton);

    const std::pair<const char*, const char*> chipData[] = {
        { "Sıcak analog pad", "warm analog synth pad, single sustained note" },
        { "Cam çan",          "glass bell, single bright note" },
        { "Tozlu piyano",     "dusty old upright piano, single note" },
        { "Fütüristik bas",   "futuristic deep bass synth, single note" },
        { "Rüzgârda flüt",    "breathy wooden flute, single sustained note" }
    };

    for (const auto& [label, prompt] : chipData)
    {
        auto* chip = chips.add (new juce::TextButton (tr (label)));
        chip->getProperties().set ("radius", 15.0f);
        chip->getProperties().set ("outline", true);
        chip->setColour (juce::TextButton::textColourOffId, col::text.withAlpha (0.85f));
        const auto text = tr (prompt);
        chip->onClick = [this, text] { promptBox.setText (text, false); promptBox.grabKeyboardFocus(); };
        addAndMakeVisible (chip);
    }

    // Dalga formu
    styleLabel (sampleNameLabel, 14.0f, col::text, true);
    addAndMakeVisible (sampleNameLabel);

    styleLabel (metaLabel, 11.0f, col::dim);
    metaLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (metaLabel);

    styleLabel (statusLabel, 12.0f, col::dim);
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (waveform);

    // Düğmeler
    const std::pair<const char*, const char*> knobData[] = {
        { "attack", "Attack" }, { "decay", "Decay" }, { "sustain", "Sustain" }, { "release", "Release" },
        { "cutoff", "Filtre" }, { "reverb", "Reverb" }, { "length", "Süre" },     { "gain", "Ses" }
    };

    for (const auto& [id, name] : knobData)
    {
        auto* knob = knobs.add (new Knob());
        knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
        knob->slider.setColour (juce::Slider::textBoxTextColourId, col::text);
        knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (knob->slider);

        knob->label.setText (tr (name), juce::dontSendNotification);
        styleLabel (knob->label, 11.0f, col::dim);
        knob->label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob->label);

        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, knob->slider);
    }

    knobs[6]->slider.setTooltip (tr ("Bir sonraki üretilecek sesin uzunluğu"));

    // Klavye
    keysLabel.setText (tr ("KLAVYE"), juce::dontSendNotification);
    styleLabel (keysLabel, 12.0f, col::dim, true);
    addAndMakeVisible (keysLabel);

    keyboard.setAvailableRange (48, 83);     // Logic adlandırmasıyla C2 – B4
    keyboard.setOctaveForMiddleC (3);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffeceef2));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff15171c));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xffb0b4bc));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, col::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, col::accent.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colour (0xff4a505b));
    addAndMakeVisible (keyboard);

    setSize (1000, 680);
    startTimerHz (15);
    timerCallback();
}

WidiKeyEditor::~WidiKeyEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void WidiKeyEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    auto drawPanel = [&g] (juce::Rectangle<int> r)
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (r.toFloat(), 14.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 14.0f, 1.0f);
    };

    drawPanel (promptPanel);
    drawPanel (wavePanel);
    drawPanel (knobPanel);
    drawPanel (keysPanel);

    // Logo
    const auto logo = logoArea.toFloat().withSizeKeepingCentre (28.0f, 28.0f);
    g.setColour (col::accent);
    g.drawRoundedRectangle (logo.reduced (1.0f), 7.0f, 2.0f);
    const float heights[] = { 8.0f, 12.0f, 6.0f, 10.0f, 4.0f };

    for (int i = 0; i < 5; ++i)
    {
        const float x = logo.getX() + 7.0f + (float) i * 4.0f;
        g.drawLine (x, logo.getCentreY() - heights[i] * 0.5f, x, logo.getCentreY() + heights[i] * 0.5f, 2.0f);
    }

    // Versiyon rozeti
    g.setColour (col::border.brighter (0.1f));
    g.drawRoundedRectangle (version.getBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void WidiKeyEditor::resized()
{
    auto area = getLocalBounds().reduced (24, 20).withTrimmedBottom (4);

    // Üst bar
    auto header = area.removeFromTop (44);
    logoArea = header.removeFromLeft (36);
    header.removeFromLeft (8);
    title.setBounds (header.removeFromLeft (112));
    version.setBounds (header.removeFromLeft (44).withSizeKeepingCentre (44, 22));

    settingsButton.setBounds (header.removeFromRight (92).withSizeKeepingCentre (92, 40));
    header.removeFromRight (12);
    localButton.setBounds (header.removeFromRight (112).withSizeKeepingCentre (112, 36));
    header.removeFromRight (4);
    cloudButton.setBounds (header.removeFromRight (80).withSizeKeepingCentre (80, 36));

    // Tarif paneli
    area.removeFromTop (16);
    promptPanel = area.removeFromTop (130);
    {
        auto r = promptPanel.reduced (20, 16);
        promptLabel.setBounds (r.removeFromTop (16));
        r.removeFromTop (10);

        auto row = r.removeFromTop (52);
        generateButton.setBounds (row.removeFromRight (130));
        row.removeFromRight (10);
        promptBox.setBounds (row);

        r.removeFromTop (10);
        int x = r.getX();

        for (auto* chip : chips)
        {
            chip->changeWidthToFitText (30);
            chip->setBounds (x, r.getY(), chip->getWidth() + 12, 30);
            x += chip->getWidth() + 8;
        }
    }

    // Dalga formu + düğmeler
    area.removeFromTop (16);
    auto middle = area.removeFromTop (176);
    knobPanel = middle.removeFromRight (360);
    middle.removeFromRight (16);
    wavePanel = middle;
    {
        auto r = wavePanel.reduced (18, 14);
        auto top = r.removeFromTop (18);
        metaLabel.setBounds (top.removeFromRight (160));
        sampleNameLabel.setBounds (top);
        statusLabel.setBounds (r.removeFromBottom (18));
        r.removeFromBottom (8);
        r.removeFromTop (8);
        waveform.setBounds (r);
    }
    {
        auto r = knobPanel.reduced (10, 8);
        const int cellW = r.getWidth() / 4;
        const int cellH = r.getHeight() / 2;

        for (int i = 0; i < knobs.size(); ++i)
        {
            juce::Rectangle<int> cell (r.getX() + (i % 4) * cellW, r.getY() + (i / 4) * cellH, cellW, cellH);
            knobs[i]->label.setBounds (cell.removeFromTop (14));
            knobs[i]->slider.setBounds (cell);
        }
    }

    // Klavye
    area.removeFromTop (16);
    keysPanel = area;
    {
        auto r = keysPanel.reduced (16, 12);
        keysLabel.setBounds (r.removeFromTop (16));
        r.removeFromTop (8);
        keyboard.setBounds (r);
        keyboard.setKeyWidth ((float) r.getWidth() / 21.0f);
    }
}

void WidiKeyEditor::timerCallback()
{
    const auto status = proc.getStatus();
    const bool working = status == WidiKeyProcessor::Status::working;

    auto message = proc.getStatusMessage();

    if (message.isEmpty())
        message = proc.getSampleLengthSeconds() > 0.0 ? tr ("Hazır · klavyeden çalabilirsin")
                                                      : tr ("Bir tarif yaz ve Üret'e bas.");

    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, status == WidiKeyProcessor::Status::error ? col::error : col::dim);

    generateButton.setEnabled (! working);
    generateButton.setButtonText (working ? tr ("Üretiliyor…") : tr ("Üret"));
    waveform.setBusy (working);

    const int currentVersion = proc.getSampleVersion();

    if (currentVersion != lastVersion)
    {
        lastVersion = currentVersion;
        waveform.setPeaks (proc.getPeaks());

        const auto name = proc.getSampleName();
        sampleNameLabel.setText (name.isEmpty() ? tr ("Henüz ses yok")
                                                : name.substring (0, 1).toUpperCase() + name.substring (1),
                                 juce::dontSendNotification);

        const auto seconds = proc.getSampleLengthSeconds();
        metaLabel.setText (seconds > 0.0 ? tr ("KÖK C3 · ") + juce::String (seconds, 1).replace (".", ",") + " s"
                                         : juce::String(),
                           juce::dontSendNotification);
    }
}

void WidiKeyEditor::generate()
{
    if (WidiKeyProcessor::loadApiKey().isEmpty())
    {
        openSettings();
        return;
    }

    proc.startGeneration (promptBox.getText());
}

void WidiKeyEditor::openSettings()
{
    auto* window = new juce::AlertWindow (tr ("widi-key ayarları"),
                                          tr ("ElevenLabs API anahtarını yapıştır. Anahtar sadece bu Mac'te saklanır.\n"
                                              "Anahtarı elevenlabs.io'da giriş yaptıktan sonra API Keys bölümünden alabilirsin."),
                                          juce::MessageBoxIconType::NoIcon,
                                          this);

    window->addTextEditor ("key", WidiKeyProcessor::loadApiKey(), tr ("API anahtarı"), true);
    window->addButton (tr ("Kaydet"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("İptal"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window] (int result)
    {
        if (result == 1)
            WidiKeyProcessor::saveApiKey (window->getTextEditorContents ("key"));
    }), true);
}
