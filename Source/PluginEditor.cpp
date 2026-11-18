#include "PluginEditor.h"
#include "Common.h"
#include "BinaryData.h"

using widi::tr;
using widi::uiFont;
using widi::monoFont;
namespace col = widi::col;

namespace
{
    void styleLabel (juce::Label& label, juce::Font font, juce::Colour colour)
    {
        label.setFont (font);
        label.setColour (juce::Label::textColourId, colour);
        label.setBorderSize ({});
        label.setInterceptsMouseClicks (false, false);
    }

    void drawWell (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float radius = 6.0f)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    juce::String noteSafeText (const juce::String& s, int maxChars)
    {
        return s.length() > maxChars ? s.substring (0, maxChars - 1) + juce::String::fromUTF8 ("…") : s;
    }
}

//==============================================================================
WidiLookAndFeel::WidiLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bg);
    setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonOnColourId, col::segOn);
    setColour (juce::TextButton::textColourOffId, col::dim);
    setColour (juce::TextButton::textColourOnId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::line);
    setColour (juce::TextEditor::backgroundColourId, col::panel);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::fieldLine);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.3f));
    setColour (juce::CaretComponent::caretColourId, col::accent);
    setColour (juce::TooltipWindow::backgroundColourId, col::panel);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void WidiLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area   = bounds.withSizeKeepingCentre (size, size);
    const auto radius = size * 0.5f - 2.0f;
    const auto centre = area.getCentre();
    const juce::PathStrokeType stroke (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

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
    g.fillEllipse (area.reduced (size * 0.28f));

    g.setColour (col::text);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.1f, angle),
                  centre.getPointOnCircumference (radius * 0.42f, angle) }, 1.5f);
}

void WidiLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const auto track = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 2.0f, (float) width, 4.0f);
    g.setColour (col::line);
    g.fillRoundedRectangle (track, 2.0f);

    g.setColour (col::accent);
    g.fillRoundedRectangle (track.withRight (juce::jmax (track.getX() + 2.0f, sliderPos)), 2.0f);

    g.setColour (col::text);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ sliderPos, track.getCentreY() }));
}

void WidiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = (float) button.getProperties().getWithDefault ("radius", 5.0f);

    auto base = backgroundColour;

    if (! button.isEnabled())
        base = base.withMultipliedAlpha (0.55f);
    else if (down)
        base = base.darker (0.15f);
    else if (highlighted)
        base = base.isTransparent() ? col::line.withAlpha (0.5f) : base.brighter (0.08f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, radius);

    if ((bool) button.getProperties().getWithDefault ("outline", false))
    {
        const bool lit = button.getToggleState() && (bool) button.getProperties().getWithDefault ("accentWhenOn", false);
        g.setColour (lit ? col::accent : col::line);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

juce::Font WidiLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const bool bold = (bool) button.getProperties().getWithDefault ("bold", false);
    return uiFont (juce::jmin (12.0f, (float) buttonHeight * 0.5f), bold);
}

//==============================================================================
void WaveView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawWell (g, bounds, col::well);

    g.setColour (juce::Colour (0xff1a1c21));
    g.fillRect (bounds.getX(), bounds.getCentreY(), bounds.getWidth(), 1.0f);

    g.setFont (uiFont (10.0f));
    g.setColour (col::dim);
    g.drawText (tr ("SEÇİLİ SES · klavyeye yayıldı"), getLocalBounds().reduced (10, 6).removeFromTop (12), juce::Justification::topLeft);

    g.setColour (col::envelope);
    g.drawText (tr ("Zarf"), getLocalBounds().reduced (10, 6).removeFromTop (12), juce::Justification::topRight);
    g.fillRect ((float) getWidth() - 46.0f, 12.0f, 10.0f, 2.0f);

    const auto inner = bounds.reduced (10.0f, 22.0f).withTrimmedBottom (-12.0f);

    if (peaks.empty())
    {
        g.setColour (col::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (tr ("Henüz ses yok · yukarıya Türkçe bir tarif yaz ve Üret'e bas"), getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto barWidth = inner.getWidth() / (float) peaks.size();
    g.setColour (col::accent.withAlpha (busy ? 0.2f : 0.85f));

    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const float h = juce::jmax (2.0f, peaks[i] * inner.getHeight());
        g.fillRoundedRectangle (inner.getX() + (float) i * barWidth, inner.getCentreY() - h * 0.5f,
                                juce::jmax (1.0f, barWidth - 1.0f), h, 1.0f);
    }

    // Zarf çizgisi (ADSR)
    const float a = apvts.getRawParameterValue ("attack")->load();
    const float d = apvts.getRawParameterValue ("decay")->load();
    const float s = apvts.getRawParameterValue ("sustain")->load();
    const float r = apvts.getRawParameterValue ("release")->load();
    const float total = (float) juce::jmax (seconds, (double) (a + d + r), 0.1);

    auto xAt = [&] (float t) { return inner.getX() + inner.getWidth() * juce::jlimit (0.0f, 1.0f, t / total); };
    const float top = inner.getY();
    const float bottom = inner.getBottom();
    const float sustainY = bottom - s * (bottom - top);
    const float releaseStart = juce::jmax (a + d, total - r);

    juce::Path env;
    env.startNewSubPath (xAt (0.0f), bottom);
    env.lineTo (xAt (a), top);
    env.lineTo (xAt (a + d), sustainY);
    env.lineTo (xAt (releaseStart), sustainY);
    env.lineTo (xAt (total), bottom);
    g.setColour (col::envelope);
    g.strokePath (env, juce::PathStrokeType (1.5f));
}

//==============================================================================
void VariationTile::setData (int newIndex, const std::vector<float>& peaks, int newSeed, bool isSelected, bool isAvailable, bool isBusy)
{
    index = newIndex;
    seed = newSeed;
    selected = isSelected;
    available = isAvailable;
    busy = isBusy;
    mini.assign (28, 0.0f);

    if (! peaks.empty())
        for (size_t i = 0; i < mini.size(); ++i)
        {
            const size_t start = i * peaks.size() / mini.size();
            const size_t end = juce::jmax (start + 1, (i + 1) * peaks.size() / mini.size());

            for (size_t j = start; j < end && j < peaks.size(); ++j)
                mini[i] = juce::jmax (mini[i], peaks[j]);
        }

    setMouseCursor (available ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void VariationTile::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? col::panel : col::tile);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (selected ? col::accent : col::line);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (12);
    g.setFont (monoFont (10.0f));
    g.setColour (selected ? col::accent : (available ? col::soft : col::faint));
    g.drawText ("VAR " + juce::String::charToString ((juce::juce_wchar) ('A' + index)), top, juce::Justification::centredLeft);

    g.setColour (col::dim);
    g.setFont (monoFont (9.0f));
    g.drawText (! available ? juce::String ("-") : (seed >= 0 ? "#" + juce::String (seed) : tr ("bulut")),
                top, juce::Justification::centredRight);

    r.removeFromTop (4);
    const float w = (float) r.getWidth() / (float) mini.size();
    g.setColour ((selected ? col::accent : col::idleBar).withAlpha (busy ? 0.3f : 1.0f));

    for (size_t i = 0; i < mini.size(); ++i)
    {
        const float h = available ? juce::jmax (2.0f, mini[i] * (float) r.getHeight()) : 2.0f;
        g.fillRect ((float) r.getX() + (float) i * w, (float) r.getCentreY() - h * 0.5f, juce::jmax (1.0f, w - 1.0f), h);
    }
}

//==============================================================================
void LcdDisplay::setText (const juce::String& left, const juce::String& right, bool isError)
{
    if (left == leftText && right == rightText && isError == error)
        return;

    leftText = left;
    rightText = right;
    error = isError;
    repaint();
}

void LcdDisplay::paint (juce::Graphics& g)
{
    drawWell (g, getLocalBounds().toFloat(), col::well, 5.0f);

    auto r = getLocalBounds().reduced (10, 0);
    g.setFont (monoFont (11.0f));

    const int rightWidth = rightText.isEmpty() ? 0 : juce::jmin (r.getWidth() / 2, (int) juce::GlyphArrangement::getStringWidth (monoFont (11.0f), rightText) + 4);
    g.setColour (col::dim);
    g.drawText (rightText, r.removeFromRight (rightWidth), juce::Justification::centredRight);

    g.setColour (error ? col::error : col::accent);
    g.drawText (leftText, r.withTrimmedRight (8), juce::Justification::centredLeft, true);
}

//==============================================================================
ParamRow::ParamRow (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& n, const juce::String& h)
    : attachment (state, paramId, slider), name (n), hint (h)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
}

void ParamRow::paint (juce::Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (13);
    const auto value = slider.getTextFromValue (slider.getValue());

    g.setFont (monoFont (11.0f));
    g.setColour (col::text);
    g.drawText (value, top, juce::Justification::centredRight);

    g.setFont (uiFont (11.0f));
    g.setColour (col::soft);
    const int nameWidth = (int) juce::GlyphArrangement::getStringWidth (uiFont (11.0f), name) + 1;
    g.drawText (name, top.removeFromLeft (nameWidth), juce::Justification::centredLeft);

    g.setColour (col::faint);
    g.drawText (juce::String::fromUTF8 (" · ") + hint, top.withTrimmedRight (50), juce::Justification::centredLeft, true);
}

void ParamRow::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (15).withHeight (12).expanded (5, 0));
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& n)
    : attachment (state, paramId, slider), name (n)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.onValueChange = [this] { repaint(); };
    slider.setPopupDisplayEnabled (true, false, nullptr);
    addAndMakeVisible (slider);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().withTrimmedLeft (36);
    g.setFont (uiFont (10.0f));
    g.setColour (col::soft);
    g.drawText (name, r.removeFromTop (getHeight() / 2), juce::Justification::bottomLeft);

    g.setFont (monoFont (10.0f));
    g.setColour (col::text);
    g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::topLeft);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().removeFromLeft (30).withSizeKeepingCentre (30, 30));
}

//==============================================================================
MainView::MainView (WidiKeyProcessor& p)
    : proc (p),
      wave (p.apvts),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    serif = juce::Typeface::createSystemTypefaceFor (BinaryData::InstrumentSerifItalic_ttf,
                                                      BinaryData::InstrumentSerifItalic_ttfSize);

    // Üst bar
    title.setText ("widi-key", juce::dontSendNotification);
    styleLabel (title, uiFont (13.0f, true), col::text);
    addAndMakeVisible (title);

    madeIn.setText ("made in wabi", juce::dontSendNotification);
    styleLabel (madeIn, serif != nullptr ? juce::Font (juce::FontOptions (serif).withHeight (13.0f)) : uiFont (11.0f), col::accent);
    addAndMakeVisible (madeIn);

    addAndMakeVisible (lcd);

    cloudButton.setButtonText (tr ("Bulut"));
    cloudButton.setTooltip (tr ("ElevenLabs ile bulutta üretir. API anahtarı gerekir, her varyasyon kredi harcar."));
    cloudButton.setClickingTogglesState (true);
    cloudButton.setRadioGroupId (1001);
    cloudButton.getProperties().set ("radius", 0.0f);
    cloudButton.onClick = [this] { proc.setEngine (WidiKeyProcessor::Engine::cloud); refreshEngineButtons(); };
    addAndMakeVisible (cloudButton);

    localButton.setButtonText (tr ("   Yerel"));
    localButton.setTooltip (tr ("Mac'inde internetsiz ve limitsiz üretir. Nokta yeşilse motor çalışıyor."));
    localButton.setClickingTogglesState (true);
    localButton.setRadioGroupId (1001);
    localButton.getProperties().set ("radius", 0.0f);
    localButton.onClick = [this] { proc.setEngine (WidiKeyProcessor::Engine::local); refreshEngineButtons(); };
    addAndMakeVisible (localButton);

    settingsButton.setButtonText (tr ("Ayarlar"));
    settingsButton.getProperties().set ("outline", true);
    settingsButton.onClick = [this] { openSettings(); };
    addAndMakeVisible (settingsButton);

    // Tarif satırı
    promptBox.setFont (uiFont (13.0f));
    promptBox.setTextToShowWhenEmpty (tr ("Sesi Türkçe tarif et, örn. tozlu bir müzik kutusu, tek nota"), col::faint);
    promptBox.setJustification (juce::Justification::centredLeft);
    promptBox.setIndents (12, 0);
    promptBox.setText (proc.getPrompt(), false);
    promptBox.onReturnKey = [this] { generate(); };
    addAndMakeVisible (promptBox);

    generateButton.setColour (juce::TextButton::buttonColourId, col::accent);
    generateButton.setColour (juce::TextButton::textColourOffId, col::onAccent);
    generateButton.getProperties().set ("bold", true);
    generateButton.getProperties().set ("radius", 6.0f);
    generateButton.onClick = [this] { generate(); };
    addAndMakeVisible (generateButton);

    // Dalga formu ve varyasyonlar
    addAndMakeVisible (wave);

    for (int i = 0; i < WidiKeyProcessor::maxVariations; ++i)
    {
        tiles[i].onPick = [this, i] { proc.selectVariation (i); };
        addAndMakeVisible (tiles[i]);
    }

    // Üretim ayarları
    rows.add (new ParamRow (proc.apvts, "steps",      tr ("Kalite"),          tr ("yüksek = daha temiz, yavaş")));
    rows.add (new ParamRow (proc.apvts, "strength",   tr ("Tarife bağlılık"), tr ("yüksek = birebir")));
    rows.add (new ParamRow (proc.apvts, "length",     tr ("Süre"),            tr ("saniye")));
    rows.add (new ParamRow (proc.apvts, "variations", tr ("Varyasyon"),       tr ("kaç sonuç")));

    for (auto* r : rows)
        addAndMakeVisible (r);

    addAndMakeVisible (seedBox);

    lockButton.setButtonText (tr ("Kilit"));
    lockButton.setTooltip (tr ("Kilitliyken aynı seed kullanılır, aynı sesi geliştirirsin."));
    lockButton.setClickingTogglesState (true);
    lockButton.setToggleState (proc.isSeedLocked(), juce::dontSendNotification);
    lockButton.getProperties().set ("outline", true);
    lockButton.getProperties().set ("accentWhenOn", true);
    lockButton.onClick = [this] { proc.setSeedLocked (lockButton.getToggleState()); };
    addAndMakeVisible (lockButton);

    newSeedButton.setButtonText (tr ("Yeni"));
    newSeedButton.setTooltip (tr ("Rastgele yeni bir seed seç"));
    newSeedButton.getProperties().set ("outline", true);
    newSeedButton.onClick = [this] { proc.rerollSeed(); };
    addAndMakeVisible (newSeedButton);

    // Ses ayarları düğmeleri
    const std::pair<const char*, const char*> knobData[] = {
        { "attack", "Attack" }, { "decay", "Decay" }, { "sustain", "Sustain" }, { "release", "Release" },
        { "cutoff", "Kesim" }, { "resonance", "Rezonans" },
        { "reverb", "Reverb" }, { "gain", "Ses" }
    };

    for (const auto& [id, name] : knobData)
        addAndMakeVisible (knobs.add (new Knob (proc.apvts, id, tr (name))));

    // Klavye: Logic adlandırmasıyla C1 – B4
    keyboard.setAvailableRange (36, 83);
    keyboard.setOctaveForMiddleC (3);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffe4e6ea));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff14161a));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xffa8acb4));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, col::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, col::accent.withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colour (0xff5a606b));
    addAndMakeVisible (keyboard);

    refreshEngineButtons();
    setSize (designWidth, designHeight);
    startTimerHz (15);
    timerCallback();
}

MainView::~MainView()
{
    stopTimer();
}

void MainView::refreshEngineButtons()
{
    const bool local = proc.getEngine() == WidiKeyProcessor::Engine::local;
    cloudButton.setToggleState (! local, juce::dontSendNotification);
    localButton.setToggleState (local, juce::dontSendNotification);
    repaint();
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // Üst bar
    g.setColour (col::header);
    g.fillRect (0, 0, getWidth(), 40);
    g.setColour (col::line);
    g.fillRect (0, 40, getWidth(), 1);

    // Logo
    const auto logo = logoArea.toFloat();
    g.setColour (col::accent);
    g.drawRoundedRectangle (logo.reduced (0.75f), 4.0f, 1.5f);
    const float heights[] = { 4.0f, 7.0f, 2.0f, 5.0f };

    for (int i = 0; i < 4; ++i)
    {
        const float x = logo.getX() + 5.0f + (float) i * 2.6f;
        g.drawLine (x, logo.getCentreY() - heights[i] * 0.5f, x, logo.getCentreY() + heights[i] * 0.5f, 1.5f);
    }

    // Motor seçici çerçevesi
    const auto seg = cloudButton.getBounds().getUnion (localButton.getBounds()).toFloat();
    g.setColour (col::line);
    g.drawRoundedRectangle (seg.reduced (0.5f), 5.0f, 1.0f);

    // Varyasyon başlığı
    const int tileTop = tiles[0].getY();
    auto header = juce::Rectangle<int> (tiles[0].getX(), tileTop - 16, tiles[3].getRight() - tiles[0].getX(), 12);
    g.setFont (uiFont (10.0f));
    g.setColour (col::dim);
    g.drawText (tr ("VARYASYONLAR"), header, juce::Justification::centredLeft);
    g.drawText (tr ("Her üretimde birkaç farklı sonuç · beğendiğine tıkla"), header, juce::Justification::centredRight);

    // Üretim ayarları paneli
    drawWell (g, panelRect.toFloat(), col::panel);
    auto panelTop = panelRect.reduced (12, 8).removeFromTop (12);
    g.setColour (col::dim);
    g.drawText (tr ("ÜRETİM AYARLARI"), panelTop, juce::Justification::centredLeft);
    const bool local = proc.getEngine() == WidiKeyProcessor::Engine::local;
    g.setColour (local ? col::accent : col::soft);
    g.drawText (local ? tr ("Yerel · Stable Audio") : tr ("Bulut · ElevenLabs"), panelTop, juce::Justification::centredRight);

    g.setColour (col::faint);
    g.drawText (tr ("Kilit açıkken her üretim yeni, kilitliyken aynı sesi geliştirirsin."),
                juce::Rectangle<int> (panelRect.getX() + 12, seedBox.getY() - 16, panelRect.getWidth() - 24, 12),
                juce::Justification::centredLeft, true);

    // Ses ayarları şeridi
    drawWell (g, knobStrip.toFloat(), col::panel);
    const char* groupTitles[] = { "ZARF · notanın açılıp kapanışı", "FİLTRE · parlaklık", "ÇIKIŞ" };

    for (int i = 0; i < 3; ++i)
    {
        if (i > 0)
        {
            g.setColour (col::line);
            g.fillRect (groupRects[i].getX(), knobStrip.getY() + 1, 1, knobStrip.getHeight() - 2);
        }

        g.setColour (col::dim);
        g.setFont (uiFont (9.0f));
        g.drawText (tr (groupTitles[i]), groupRects[i].reduced (12, 0).withY (knobStrip.getY() + 8).withHeight (10),
                    juce::Justification::centredLeft);
    }

    // Klavye kuyusu
    drawWell (g, keysRect.toFloat(), col::well);
}

void MainView::paintOverChildren (juce::Graphics& g)
{
    // Yerel motor durumu: yeşil nokta = çalışıyor
    const auto b = localButton.getBounds().toFloat();
    const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ b.getX() + 14.0f, b.getCentreY() });
    g.setColour (proc.isEngineOnline() ? col::accent : col::idleBar);
    g.fillEllipse (dot);
}

void MainView::resized()
{
    // Üst bar
    logoArea = { 12, 11, 18, 18 };
    title.setBounds (38, 7, 110, 14);
    madeIn.setBounds (38, 21, 110, 14);
    settingsButton.setBounds (getWidth() - 12 - 56, 7, 56, 26);
    localButton.setBounds (settingsButton.getX() - 8 - 66, 7, 66, 26);
    cloudButton.setBounds (localButton.getX() - 58, 7, 58, 26);
    lcd.setBounds (170, 7, cloudButton.getX() - 12 - 170, 26);

    // Tarif satırı
    generateButton.setBounds (getWidth() - 12 - 96, 50, 96, 36);
    promptBox.setBounds (12, 50, generateButton.getX() - 8 - 12, 36);

    // Orta bölüm
    panelRect = { getWidth() - 12 - 236, 96, 236, 196 };
    const int leftWidth = panelRect.getX() - 10 - 12;
    wave.setBounds (12, 96, leftWidth, 126);

    const int tileWidth = (leftWidth - 3 * 8) / 4;

    for (int i = 0; i < WidiKeyProcessor::maxVariations; ++i)
        tiles[i].setBounds (12 + i * (tileWidth + 8), 246, tileWidth, 46);

    auto pr = panelRect.reduced (12, 8);
    pr.removeFromTop (18);

    for (auto* r : rows)
    {
        r->setBounds (pr.removeFromTop (27));
        pr.removeFromTop (3);
    }

    auto seedRow = juce::Rectangle<int> (pr.getX(), panelRect.getBottom() - 8 - 26, pr.getWidth(), 26);
    newSeedButton.setBounds (seedRow.removeFromRight (44));
    seedRow.removeFromRight (6);
    lockButton.setBounds (seedRow.removeFromRight (44));
    seedRow.removeFromRight (6);
    seedBox.setBounds (seedRow);

    // Ses ayarları şeridi
    knobStrip = { 12, 302, getWidth() - 24, 72 };
    const int unit = knobStrip.getWidth() / 8;
    groupRects[0] = { knobStrip.getX(),            knobStrip.getY(), unit * 4, knobStrip.getHeight() };
    groupRects[1] = { groupRects[0].getRight(),    knobStrip.getY(), unit * 2, knobStrip.getHeight() };
    groupRects[2] = { groupRects[1].getRight(),    knobStrip.getY(), knobStrip.getRight() - groupRects[1].getRight(), knobStrip.getHeight() };

    const int knobsPerGroup[] = { 4, 2, 2 };
    int k = 0;

    for (int gi = 0; gi < 3; ++gi)
    {
        auto area = groupRects[gi].reduced (12, 0).withTrimmedTop (22).withHeight (40);
        const int cell = area.getWidth() / knobsPerGroup[gi];

        for (int j = 0; j < knobsPerGroup[gi]; ++j, ++k)
            knobs[k]->setBounds (area.getX() + j * cell, area.getY(), juce::jmin (cell, 96), 40);
    }

    // Klavye
    keysRect = { 12, 384, getWidth() - 24, getHeight() - 12 - 384 };
    const auto keys = keysRect.reduced (6);
    keyboard.setBounds (keys);
    keyboard.setKeyWidth ((float) keys.getWidth() / 28.0f);
}

void MainView::timerCallback()
{
    const auto status = proc.getStatus();
    const bool working = status == WidiKeyProcessor::Status::working;
    const int count = (int) proc.apvts.getRawParameterValue ("variations")->load();

    generateButton.setEnabled (! working);
    generateButton.setButtonText (working ? tr ("Üretiliyor…") : (count > 1 ? tr ("Üret ×") + juce::String (count) : tr ("Üret")));
    wave.setBusy (working);
    wave.repaint();

    const int selected = proc.getSelectedVariation();
    const auto info = proc.getVariationInfo();

    const int currentVersion = proc.getSampleVersion();

    if (currentVersion != lastVersion)
    {
        lastVersion = currentVersion;
        const bool has = selected < (int) info.size();
        wave.setData (has ? info[(size_t) selected].peaks : std::vector<float>(), has ? info[(size_t) selected].seconds : 0.0);
    }

    for (int i = 0; i < WidiKeyProcessor::maxVariations; ++i)
    {
        const bool available = i < (int) info.size();
        tiles[i].setData (i, available ? info[(size_t) i].peaks : std::vector<float>(),
                          available ? info[(size_t) i].seed : -1, available && i == selected, available, working);
    }

    // Bilgi ekranı
    juce::String left;
    const bool isError = status == WidiKeyProcessor::Status::error;

    if (working || isError)
        left = proc.getStatusMessage();
    else
    {
        const auto name = proc.getSampleName();
        left = name.isEmpty() ? tr ("Hazır · sesi tarif et ve Üret'e bas")
                              : name.substring (0, 1).toUpperCase() + name.substring (1);
    }

    juce::String right;

    if (! info.empty() && selected < (int) info.size())
        right = tr ("KÖK C3 · ") + juce::String (info[(size_t) selected].seconds, 1) + " s · VAR "
                + juce::String::charToString ((juce::juce_wchar) ('A' + selected));

    lcd.setText (noteSafeText (left, 70), right, isError);
    seedBox.setText (tr ("SEED  ") + juce::String (proc.getBaseSeed()), proc.isSeedLocked() ? tr ("kilitli") : juce::String(), false);
    lockButton.setToggleState (proc.isSeedLocked(), juce::dontSendNotification);

    repaint (localButton.getBounds());
}

void MainView::generate()
{
    if (proc.getEngine() == WidiKeyProcessor::Engine::cloud && WidiKeyProcessor::loadApiKey().isEmpty())
    {
        openSettings();
        return;
    }

    proc.startGeneration (promptBox.getText());
}

void MainView::openSettings()
{
    const juce::String message =
        tr ("BULUT (ElevenLabs)\nAPI anahtarını aşağıya yapıştır. Anahtar sadece bu Mac'te saklanır.\n\n"
            "YEREL MOTOR\nDurum: ") + (proc.isEngineOnline() ? tr ("çalışıyor") : tr ("kapalı")) +
        tr ("\nKurmak için Terminal'e şunu yapıştır:\n"
            "curl -fsSL https://raw.githubusercontent.com/wabiblack/widi-key/main/engine/install.sh | bash");

    auto* window = new juce::AlertWindow (tr ("widi-key ayarları"), message, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("key", WidiKeyProcessor::loadApiKey(), tr ("ElevenLabs API anahtarı"), true);
    window->addButton (tr ("Kaydet"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Komutu kopyala"), 2);
    window->addButton (tr ("Kapat"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window] (int result)
    {
        if (result == 1)
            WidiKeyProcessor::saveApiKey (window->getTextEditorContents ("key"));
        else if (result == 2)
            juce::SystemClipboard::copyTextToClipboard (
                "curl -fsSL https://raw.githubusercontent.com/wabiblack/widi-key/main/engine/install.sh | bash");
    }), true);
}

//==============================================================================
WidiKeyEditor::WidiKeyEditor (WidiKeyProcessor& p)
    : AudioProcessorEditor (p), view (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (view);

    setResizable (true, true);
    setResizeLimits (MainView::designWidth * 3 / 4, MainView::designHeight * 3 / 4,
                     MainView::designWidth * 3 / 2, MainView::designHeight * 3 / 2);

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) MainView::designWidth / (double) MainView::designHeight);

    setSize (MainView::designWidth, MainView::designHeight);
}

WidiKeyEditor::~WidiKeyEditor()
{
    setLookAndFeel (nullptr);
}

void WidiKeyEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void WidiKeyEditor::resized()
{
    const float scale = (float) getWidth() / (float) MainView::designWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, MainView::designWidth, MainView::designHeight);
}
