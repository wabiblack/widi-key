#include "PluginEditor.h"
#include "BinaryData.h"

using widi::tr;
using widi::uiFont;
using widi::monoFont;
namespace col = widi::col;

namespace
{
    const juce::String installCommand =
        "curl -fsSL https://raw.githubusercontent.com/wabiblack/widi-key/main/engine/install.sh | bash";

    void styleLabel (juce::Label& label, juce::Font font, juce::Colour colour)
    {
        label.setFont (font);
        label.setColour (juce::Label::textColourId, colour);
        label.setBorderSize ({});
        label.setInterceptsMouseClicks (false, false);
    }

    // Modern kart: yumuşak gölge, hafif degrade, ince kenar
    void drawCard (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
    {
        juce::Path p;
        p.addRoundedRectangle (r, radius);
        juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 14, { 0, 4 }).drawForPath (g, p);

        g.setGradientFill (juce::ColourGradient (col::panel.brighter (0.04f), r.getX(), r.getY(),
                                                 col::panel.darker (0.12f), r.getX(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius = 8.0f)
    {
        g.setGradientFill (juce::ColourGradient (col::well, r.getX(), r.getY(),
                                                 col::well.brighter (0.03f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }

    void drawSegmentFrame (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (col::well);
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
    }

    juce::String trimText (const juce::String& s, int maxChars)
    {
        return s.length() > maxChars ? s.substring (0, maxChars - 1) + juce::String::fromUTF8 ("…") : s;
    }

    juce::String variationLetter (int index)
    {
        return juce::String::charToString ((juce::juce_wchar) ('A' + index));
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
    setColour (juce::TextEditor::backgroundColourId, col::well);
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
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::headerTextColourId, col::accent);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::segOn);
    setColour (juce::PopupMenu::highlightedTextColourId, col::text);
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
    g.setColour (col::accent.withAlpha (0.25f));
    g.strokePath (value, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::accent);
    g.strokePath (value, stroke);

    const auto cap = area.reduced (size * 0.24f);
    g.setGradientFill (juce::ColourGradient (col::knobCap.brighter (0.15f), cap.getX(), cap.getY(),
                                             col::knobCap.darker (0.2f), cap.getX(), cap.getBottom(), false));
    g.fillEllipse (cap);

    g.setColour (col::text);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.12f, angle),
                  centre.getPointOnCircumference (radius * 0.45f, angle) }, 1.6f);
}

void WidiLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const auto track = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 2.0f, (float) width, 4.0f);
    g.setColour (col::line);
    g.fillRoundedRectangle (track, 2.0f);

    const auto filled = track.withRight (juce::jmax (track.getX() + 2.0f, sliderPos));
    g.setGradientFill (juce::ColourGradient (col::accent.darker (0.3f), filled.getX(), 0.0f,
                                             col::accent, filled.getRight(), 0.0f, false));
    g.fillRoundedRectangle (filled, 2.0f);

    const auto thumb = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ sliderPos, track.getCentreY() });
    g.setColour (col::text);
    g.fillEllipse (thumb);
    g.setColour (col::accent);
    g.drawEllipse (thumb.reduced (0.5f), 1.5f);
}

void WidiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = (float) button.getProperties().getWithDefault ("radius", 7.0f);
    const bool primary = (bool) button.getProperties().getWithDefault ("primary", false);

    if (primary)
    {
        juce::Path p;
        p.addRoundedRectangle (bounds, radius);

        if (button.isEnabled())
            juce::DropShadow (col::accent.withAlpha (0.35f), 14, { 0, 2 }).drawForPath (g, p);

        auto top = col::accent.brighter (highlighted ? 0.15f : 0.05f);
        auto bottom = col::accent.darker (down ? 0.3f : 0.15f);

        if (! button.isEnabled())
        {
            top = top.withMultipliedAlpha (0.45f);
            bottom = bottom.withMultipliedAlpha (0.45f);
        }

        g.setGradientFill (juce::ColourGradient (top, 0.0f, bounds.getY(), bottom, 0.0f, bounds.getBottom(), false));
        g.fillPath (p);
        return;
    }

    auto base = backgroundColour;

    if (! button.isEnabled())
        base = base.withMultipliedAlpha (0.5f);
    else if (down)
        base = base.isTransparent() ? col::line : base.darker (0.15f);
    else if (highlighted)
        base = base.isTransparent() ? col::line.withAlpha (0.55f) : base.brighter (0.08f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, radius);

    if ((bool) button.getProperties().getWithDefault ("outline", false))
    {
        const bool lit = button.getToggleState() && (bool) button.getProperties().getWithDefault ("accentWhenOn", false);
        g.setColour (lit ? col::accent : juce::Colours::white.withAlpha (0.09f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
}

juce::Font WidiLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const bool bold = (bool) button.getProperties().getWithDefault ("bold", false);
    return uiFont (juce::jmin (12.5f, (float) buttonHeight * 0.5f), bold);
}

void WidiLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    drawWell (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height));
}

void WidiLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (editor.hasKeyboardFocus (true))
    {
        g.setColour (col::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.75f), 8.0f, 1.5f);
    }
}

void WidiLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.drawRect (0, 0, width, height);
}

//==============================================================================
IconButton::IconButton (Icon i, const juce::String& tooltip) : juce::Button ({}), icon (i)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? col::line : (highlighted ? col::line.withAlpha (0.6f) : col::well));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    const auto c = bounds.getCentre();
    const float s = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.22f;
    const bool on = getToggleState();
    const auto colour = on ? col::accent : (highlighted ? col::text : col::soft);
    g.setColour (colour);
    const juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;

    switch (icon)
    {
        case Icon::history:
            p.addEllipse (c.x - s * 1.6f, c.y - s * 1.6f, s * 3.2f, s * 3.2f);
            p.startNewSubPath (c.x, c.y - s);
            p.lineTo (c.x, c.y);
            p.lineTo (c.x + s * 0.8f, c.y + s * 0.5f);
            g.strokePath (p, stroke);
            break;

        case Icon::star:
            p.addStar (c, 5, s * 0.75f, s * 1.75f, 0.0f);
            if (on) g.fillPath (p); else g.strokePath (p, stroke);
            break;

        case Icon::library:
            p.addRoundedRectangle (c.x - s * 1.6f, c.y - s * 1.3f, s * 3.2f, s * 2.6f, 2.0f);
            p.startNewSubPath (c.x - s * 1.6f, c.y - s * 0.6f);
            p.lineTo (c.x + s * 1.6f, c.y - s * 0.6f);
            g.strokePath (p, stroke);
            break;

        case Icon::settings:
            for (int row = 0; row < 3; ++row)
            {
                const float y = c.y + (float) (row - 1) * s;
                const float knobX = c.x + (row == 1 ? -s * 0.6f : s * 0.6f);
                p.startNewSubPath (c.x - s * 1.6f, y);
                p.lineTo (c.x + s * 1.6f, y);
                g.strokePath (p, stroke);
                p.clear();
                g.fillEllipse (knobX - 2.5f, y - 2.5f, 5.0f, 5.0f);
            }
            break;
    }
}

//==============================================================================
void WaveView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawWell (g, bounds, 10.0f);

    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRect (bounds.getX() + 10.0f, bounds.getCentreY(), bounds.getWidth() - 20.0f, 1.0f);

    auto top = getLocalBounds().reduced (12, 8).removeFromTop (12);
    g.setFont (uiFont (10.0f));
    g.setColour (col::dim);
    g.drawText (proc.isDrumMode() ? tr ("DAVUL MODU · A=C  B=D  C=E  D=F (her oktavda)")
                                  : tr ("SEÇİLİ SES · gerçek notasına göre klavyeye yayıldı"),
                top, juce::Justification::centredLeft);

    if (! proc.isDrumMode())
    {
        g.setColour (col::envelope);
        g.drawText (tr ("Zarf"), top, juce::Justification::centredRight);
        g.fillRect ((float) top.getRight() - 36.0f, (float) top.getCentreY() - 1.0f, 10.0f, 2.0f);
    }

    const auto inner = bounds.reduced (12.0f, 26.0f).withTrimmedBottom (-10.0f);

    if (peaks.empty())
    {
        g.setColour (col::dim);
        g.setFont (uiFont (12.5f));
        g.drawText (tr ("Henüz ses yok · yukarıya Türkçe ya da İngilizce bir tarif yaz ve Üret'e bas"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const auto barWidth = inner.getWidth() / (float) peaks.size();
    g.setGradientFill (juce::ColourGradient (col::accent.withAlpha (busy ? 0.2f : 0.95f), 0.0f, inner.getY(),
                                             col::accent.withAlpha (busy ? 0.1f : 0.45f), 0.0f, inner.getBottom(), false));

    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const float h = juce::jmax (2.0f, peaks[i] * inner.getHeight());
        g.fillRoundedRectangle (inner.getX() + (float) i * barWidth, inner.getCentreY() - h * 0.5f,
                                juce::jmax (1.0f, barWidth - 1.2f), h, 1.0f);
    }

    // Zarf çizgisi (ADSR)
    if (! proc.isDrumMode())
    {
        auto& apvts = proc.apvts;
        const float a = apvts.getRawParameterValue ("attack")->load();
        const float d = apvts.getRawParameterValue ("decay")->load();
        const float s = apvts.getRawParameterValue ("sustain")->load();
        const float r = apvts.getRawParameterValue ("release")->load();
        const float total = (float) juce::jmax (seconds, (double) (a + d + r), 0.1);

        auto xAt = [&] (float t) { return inner.getX() + inner.getWidth() * juce::jlimit (0.0f, 1.0f, t / total); };
        const float topY = inner.getY();
        const float bottomY = inner.getBottom();
        const float sustainY = bottomY - s * (bottomY - topY);
        const float releaseStart = juce::jmax (a + d, total - r);

        juce::Path env;
        env.startNewSubPath (xAt (0.0f), bottomY);
        env.lineTo (xAt (a), topY);
        env.lineTo (xAt (a + d), sustainY);
        env.lineTo (xAt (releaseStart), sustainY);
        env.lineTo (xAt (total), bottomY);
        g.setColour (col::envelope);
        g.strokePath (env, juce::PathStrokeType (1.5f));
    }

    // Sürükleme ipucu
    g.setFont (uiFont (10.0f));
    g.setColour (col::faint);
    g.drawText (tr ("Dinlemek için tıkla · Logic'e sürükle"), getLocalBounds().reduced (12, 6).removeFromBottom (12),
                juce::Justification::centredRight);
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || peaks.empty() || e.getDistanceFromDragStart() < 8)
        return;

    dragging = true;
    const auto file = proc.exportSelectedToWav();

    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void WaveView::mouseUp (const juce::MouseEvent&)
{
    if (! dragging && ! peaks.empty())
    {
        // Ön dinleme: kök notayı kısaca çal
        const int note = proc.isDrumMode() ? 60 : 60;
        proc.keyboardState.noteOn (1, note, 0.85f);
        auto* state = &proc.keyboardState;
        juce::Timer::callAfterDelay (700, [state, note] { state->noteOff (1, note, 0.0f); });
    }

    dragging = false;
}

//==============================================================================
void VariationTile::setData (int newIndex, const std::vector<float>& peaks, const juce::String& noteText,
                             bool isSelected, bool isAvailable, bool isBusy, bool drumMode)
{
    index = newIndex;
    note = noteText;
    selected = isSelected;
    available = isAvailable;
    busy = isBusy;
    drum = drumMode;
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
    const bool lit = selected || (drum && available);

    g.setColour (lit ? col::panel.brighter (0.03f) : col::tile);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (selected ? col::accent : juce::Colours::white.withAlpha (drum && available ? 0.14f : 0.06f));
    g.drawRoundedRectangle (bounds, 8.0f, selected ? 1.5f : 1.0f);

    auto r = getLocalBounds().reduced (10, 7);
    auto top = r.removeFromTop (12);
    g.setFont (monoFont (10.0f));
    g.setColour (selected ? col::accent : (available ? col::soft : col::faint));
    g.drawText ("VAR " + variationLetter (index), top, juce::Justification::centredLeft);

    g.setColour (col::dim);
    g.setFont (monoFont (9.5f));
    g.drawText (available ? note : juce::String ("-"), top, juce::Justification::centredRight);

    r.removeFromTop (5);
    const float w = (float) r.getWidth() / (float) mini.size();
    g.setColour ((lit ? col::accent : col::idleBar).withAlpha (busy ? 0.3f : 1.0f));

    for (size_t i = 0; i < mini.size(); ++i)
    {
        const float h = available ? juce::jmax (2.0f, mini[i] * (float) r.getHeight()) : 2.0f;
        g.fillRoundedRectangle ((float) r.getX() + (float) i * w, (float) r.getCentreY() - h * 0.5f,
                                juce::jmax (1.0f, w - 1.2f), h, 0.8f);
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
    drawWell (g, getLocalBounds().toFloat(), 8.0f);

    auto r = getLocalBounds().reduced (12, 0);
    const auto font = monoFont (11.0f);
    g.setFont (font);

    const int rightWidth = rightText.isEmpty() ? 0
                         : juce::jmin (r.getWidth() / 2, (int) juce::GlyphArrangement::getStringWidth (font, rightText) + 4);
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
    slider.setBounds (getLocalBounds().withTrimmedTop (14).withHeight (13).expanded (6, 0));
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& n, const juce::String& tip)
    : attachment (state, paramId, slider), name (n)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.onValueChange = [this] { repaint(); };
    slider.setTooltip (tip);
    addAndMakeVisible (slider);
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setFont (uiFont (10.0f));
    g.setColour (col::soft);
    g.drawText (name, r.removeFromTop (12), juce::Justification::centred);

    g.setFont (monoFont (9.5f));
    g.setColour (col::text);
    g.drawText (slider.getTextFromValue (slider.getValue()), r.removeFromBottom (12), juce::Justification::centred);
}

void Knob::resized()
{
    slider.setBounds (getLocalBounds().reduced (0, 13).withSizeKeepingCentre (32, 32));
}

//==============================================================================
void ModernKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                    juce::Colour, juce::Colour)
{
    auto r = area.reduced (1.0f, 0.0f);
    juce::Path key;
    key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 5.0f, 5.0f, false, false, true, true);

    if (isDown)
        g.setGradientFill (juce::ColourGradient (col::accent.brighter (0.2f), 0.0f, r.getY(), col::accent.darker (0.1f), 0.0f, r.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd6d9df), 0.0f, r.getY(), juce::Colour (0xfff0f1f4), 0.0f, r.getBottom(), false));

    g.fillPath (key);

    if (isOver && ! isDown)
    {
        g.setColour (col::accent.withAlpha (0.18f));
        g.fillPath (key);
    }

    const int noteClass = midiNoteNumber % 12;
    const bool drum = isDrumMode != nullptr && isDrumMode();
    auto label = r.removeFromBottom (18.0f);

    if (drum)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (widi::drumNoteClasses[i] == noteClass)
            {
                g.setColour (isDown ? col::onAccent : col::accent.darker (0.35f));
                g.setFont (monoFont (10.0f));
                g.drawText (variationLetter (i), label, juce::Justification::centred);
            }
        }
    }
    else if (noteClass == 0)
    {
        g.setColour (isDown ? col::onAccent : juce::Colour (0xff6b717c));
        g.setFont (monoFont (9.0f));
        g.drawText ("C" + juce::String (midiNoteNumber / 12 - 2), label, juce::Justification::centred);
    }
}

void ModernKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    const auto r = area.reduced (1.5f, 0.0f);
    juce::Path key;
    key.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 3.5f, 3.5f, false, false, true, true);

    if (isDown)
        g.setGradientFill (juce::ColourGradient (col::accent, 0.0f, r.getY(), col::accent.darker (0.3f), 0.0f, r.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff121418), 0.0f, r.getY(), juce::Colour (0xff2b2f37), 0.0f, r.getBottom(), false));

    g.fillPath (key);

    if (isOver && ! isDown)
    {
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillPath (key);
    }

    g.setColour (juce::Colours::white.withAlpha (isDown ? 0.0f : 0.08f));
    g.fillRoundedRectangle (r.getX() + 2.0f, r.getBottom() - 8.0f, r.getWidth() - 4.0f, 3.0f, 1.5f);
}

//==============================================================================
MainView::MainView (WidiKeyProcessor& p)
    : proc (p),
      wave (p),
      keyboard (p.keyboardState, [&p] { return p.isDrumMode(); })
{
    serif = juce::Typeface::createSystemTypefaceFor (BinaryData::InstrumentSerifItalic_ttf,
                                                      BinaryData::InstrumentSerifItalic_ttfSize);

    // --- Üst bar ---
    title.setText ("widi-key", juce::dontSendNotification);
    styleLabel (title, uiFont (14.0f, true), col::text);
    addAndMakeVisible (title);

    madeIn.setText ("made in wabi", juce::dontSendNotification);
    styleLabel (madeIn, serif != nullptr ? juce::Font (juce::FontOptions (serif).withHeight (13.5f)) : uiFont (11.0f), col::accent);
    addAndMakeVisible (madeIn);

    addAndMakeVisible (lcd);

    auto setupSegment = [this] (juce::TextButton& b, const juce::String& text, const juce::String& tip, int group)
    {
        b.setButtonText (text);
        b.setTooltip (tip);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (group);
        b.getProperties().set ("radius", 7.0f);
        addAndMakeVisible (b);
    };

    setupSegment (melodicButton, tr ("Melodik"), tr ("Ses tüm klavyeye, gerçek notasına göre akortlu yayılır"), 2001);
    setupSegment (drumButton, tr ("Davul"), tr ("Her varyasyon ayrı bir tuşa oturur (C, D, E, F), perdesi değişmez"), 2001);
    melodicButton.onClick = [this] { setMode (false); };
    drumButton.onClick    = [this] { setMode (true); };

    setupSegment (cloudButton, tr ("Bulut"), tr ("ElevenLabs ile bulutta üretir. API anahtarı ister, her varyasyon kredi harcar."), 1001);
    setupSegment (localButton, tr ("   Yerel"), tr ("Mac'inde internetsiz ve limitsiz üretir. Nokta yeşilse motor çalışıyor."), 1001);
    cloudButton.onClick = [this] { proc.setEngine (WidiKeyProcessor::Engine::cloud); };
    localButton.onClick = [this] { proc.setEngine (WidiKeyProcessor::Engine::local); };

    libraryButton.onClick = [this] { showLibraryMenu(); };
    settingsButton.onClick = [this] { openSettings(); };
    addAndMakeVisible (libraryButton);
    addAndMakeVisible (settingsButton);

    // --- Tarif ---
    historyButton.onClick = [this] { showHistoryMenu(); };
    addAndMakeVisible (historyButton);

    favoriteButton.onClick = [this]
    {
        PromptStore::toggleFavorite (promptBox.getText());
        refreshFavorites();
    };
    addAndMakeVisible (favoriteButton);

    promptBox.setFont (uiFont (14.0f));
    promptBox.setTextToShowWhenEmpty (tr ("Sesi tarif et, örn. tozlu bir müzik kutusu, tek nota  ·  Enter ile üret"), col::faint);
    promptBox.setJustification (juce::Justification::centredLeft);
    promptBox.setIndents (14, 0);
    promptBox.setText (proc.getPrompt(), false);
    promptBox.onReturnKey = [this] { generate (false); };
    addAndMakeVisible (promptBox);

    negativeBox.setFont (uiFont (12.0f));
    negativeBox.setTextToShowWhenEmpty (tr ("İstemediğin (isteğe bağlı): örn. davul, gürültü, vokal"), col::faint);
    negativeBox.setJustification (juce::Justification::centredLeft);
    negativeBox.setIndents (14, 0);
    negativeBox.setText (proc.getNegative(), false);
    negativeBox.onReturnKey = [this] { generate (false); };
    addAndMakeVisible (negativeBox);

    generateButton.getProperties().set ("primary", true);
    generateButton.getProperties().set ("bold", true);
    generateButton.getProperties().set ("radius", 9.0f);
    generateButton.setColour (juce::TextButton::textColourOffId, col::onAccent);
    generateButton.onClick = [this] { generate (false); };
    addAndMakeVisible (generateButton);

    // --- Dalga formu ve varyasyonlar ---
    addAndMakeVisible (wave);

    for (int i = 0; i < WidiKeyProcessor::maxVariations; ++i)
    {
        tiles[i].onPick = [this, i] { proc.selectVariation (i); };
        addAndMakeVisible (tiles[i]);
    }

    similarButton.setButtonText (tr ("Benzerini üret"));
    similarButton.setTooltip (tr ("Seçili sesi temel alıp ona benzeyen yeni varyasyonlar üretir (Yerel mod). Ne kadar benzeyeceğini sağdaki 'Benzerlik' belirler."));
    similarButton.getProperties().set ("outline", true);
    similarButton.getProperties().set ("radius", 8.0f);
    similarButton.setColour (juce::TextButton::textColourOffId, col::accent);
    similarButton.onClick = [this] { generate (true); };
    addAndMakeVisible (similarButton);

    // --- Üretim ayarları ---
    rows.add (new ParamRow (proc.apvts, "steps",      tr ("Kalite"),          tr ("yüksek = temiz, yavaş")));
    rows.add (new ParamRow (proc.apvts, "strength",   tr ("Tarife bağlılık"), tr ("yüksek = birebir")));
    rows.add (new ParamRow (proc.apvts, "length",     tr ("Süre"),            tr ("saniye")));
    rows.add (new ParamRow (proc.apvts, "variations", tr ("Varyasyon"),       tr ("kaç sonuç")));
    rows.add (new ParamRow (proc.apvts, "similarity", tr ("Benzerlik"),       tr ("benzerini üretirken")));

    for (auto* r : rows)
        addAndMakeVisible (r);

    addAndMakeVisible (seedBox);

    lockButton.setButtonText (tr ("Kilit"));
    lockButton.setTooltip (tr ("Kilitliyken aynı seed kullanılır: aynı sesi koruyup kaliteyi, süreyi ya da tarifi değiştirerek geliştirirsin."));
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

    // --- Ses ayarları ---
    struct KnobDef { const char* id; const char* name; const char* tip; };
    const KnobDef knobData[] = {
        { "attack",     "Attack",   "Sesin ne kadar hızlı açılacağı" },
        { "decay",      "Decay",    "Açıldıktan sonra sustain seviyesine inme süresi" },
        { "sustain",    "Sustain",  "Tuş basılıyken kalınan seviye" },
        { "release",    "Release",  "Tuşu bırakınca sönme süresi" },
        { "cutoff",     "Kesim",    "Filtre kesim frekansı: düşük = boğuk, yüksek = parlak" },
        { "resonance",  "Rezonans", "Kesim noktasındaki vurgu" },
        { "fenvAmount", "F. Zarf",  "Her notada filtre ne kadar açılıp kapansın (oktav)" },
        { "fenvDecay",  "Z. Süre",  "Filtre zarfının kapanma süresi" },
        { "velocity",   "Velocity", "Tuşa basma sertliğinin sese etkisi" },
        { "glide",      "Glide",    "Notalar arası kayma süresi (sadece Melodik)" },
        { "reverb",     "Reverb",   "Oda yankısı" },
        { "gain",       "Ses",      "Çıkış seviyesi" }
    };

    for (const auto& k : knobData)
        addAndMakeVisible (knobs.add (new Knob (proc.apvts, k.id, tr (k.name), tr (k.tip))));

    loopButton.setButtonText (tr ("Döngü"));
    loopButton.setTooltip (tr ("Tuş basılı tutulduğunda ses bitmesin, ortasından döngüye girsin (pad'ler için)"));
    loopButton.setClickingTogglesState (true);
    loopButton.getProperties().set ("outline", true);
    loopButton.getProperties().set ("accentWhenOn", true);
    loopButton.getProperties().set ("radius", 8.0f);
    loopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "loop", loopButton);
    addAndMakeVisible (loopButton);

    // --- Klavye: Logic adlandırmasıyla C1 – B4 ---
    keyboard.setAvailableRange (36, 83);
    keyboard.setOctaveForMiddleC (3);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, col::well);
    addAndMakeVisible (keyboard);

    refreshFavorites();
    setSize (designWidth, designHeight);
    startTimerHz (15);
    timerCallback();
}

MainView::~MainView()
{
    stopTimer();
}

void MainView::setMode (bool drum)
{
    if (auto* param = proc.apvts.getParameter ("mode"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (drum ? 1.0f : 0.0f);
        param->endChangeGesture();
    }

    repaint();
}

void MainView::refreshFavorites()
{
    favorites = PromptStore::getFavorites();
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // Üst bar
    g.setGradientFill (juce::ColourGradient (col::header.brighter (0.03f), 0.0f, 0.0f, col::header, 0.0f, 48.0f, false));
    g.fillRect (0, 0, getWidth(), 48);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (0, 48, getWidth(), 1);

    // Logo
    const auto logo = logoArea.toFloat();
    g.setColour (col::accent);
    g.drawRoundedRectangle (logo.reduced (0.75f), 5.0f, 1.6f);
    const float heights[] = { 5.0f, 9.0f, 3.0f, 7.0f };

    for (int i = 0; i < 4; ++i)
    {
        const float x = logo.getX() + 5.5f + (float) i * 3.0f;
        g.drawLine (x, logo.getCentreY() - heights[i] * 0.5f, x, logo.getCentreY() + heights[i] * 0.5f, 1.6f);
    }

    // Seçici çerçeveleri
    drawSegmentFrame (g, melodicButton.getBounds().getUnion (drumButton.getBounds()).toFloat().expanded (3.0f));
    drawSegmentFrame (g, cloudButton.getBounds().getUnion (localButton.getBounds()).toFloat().expanded (3.0f));

    // "İstemediğin" ikonu (⊘)
    {
        const auto c = negIconArea.toFloat().getCentre();
        g.setColour (col::faint);
        g.drawEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 1.4f);
        g.drawLine (c.x - 4.0f, c.y + 4.0f, c.x + 4.0f, c.y - 4.0f, 1.4f);
    }

    // Varyasyon başlığı
    const auto header = juce::Rectangle<int> (tiles[0].getX(), tiles[0].getY() - 16, similarButton.getRight() - tiles[0].getX(), 12);
    g.setFont (uiFont (10.0f));
    g.setColour (col::dim);
    g.drawText (tr ("VARYASYONLAR"), header, juce::Justification::centredLeft);
    g.drawText (proc.isDrumMode() ? tr ("Davul modunda hepsi aynı anda çalınabilir") : tr ("Beğendiğine tıkla, klavyeye o yayılır"),
                header.withTrimmedRight (similarButton.getWidth() + 8), juce::Justification::centredRight);

    // Üretim ayarları kartı
    drawCard (g, panelRect.toFloat());
    auto panelTop = panelRect.reduced (14, 10).removeFromTop (12);
    g.setColour (col::dim);
    g.drawText (tr ("ÜRETİM AYARLARI"), panelTop, juce::Justification::centredLeft);
    const bool local = proc.getEngine() == WidiKeyProcessor::Engine::local;
    g.setColour (local ? col::accent : col::soft);
    g.drawText (local ? tr ("Yerel · Stable Audio") : tr ("Bulut · ElevenLabs"), panelTop, juce::Justification::centredRight);

    // Ses ayarları kartı
    drawCard (g, knobStrip.toFloat());
    const char* groupTitles[] = { "ZARF", "FİLTRE", "ÇALIM", "ÇIKIŞ" };

    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (groupRects[i].getX(), knobStrip.getY() + 10, 1, knobStrip.getHeight() - 20);
        }

        g.setColour (col::dim);
        g.setFont (uiFont (9.5f, true));
        g.drawText (tr (groupTitles[i]), groupRects[i].reduced (14, 0).withY (knobStrip.getY() + 8).withHeight (11),
                    juce::Justification::centredLeft);
    }

    // Klavye kuyusu
    drawWell (g, keysRect.toFloat(), 10.0f);
}

void MainView::paintOverChildren (juce::Graphics& g)
{
    // Yerel motor durumu: yeşil nokta = çalışıyor
    const auto b = localButton.getBounds().toFloat();
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ b.getX() + 15.0f, b.getCentreY() });

    if (proc.isEngineOnline())
    {
        g.setColour (col::accent.withAlpha (0.3f));
        g.fillEllipse (dot.expanded (3.0f));
    }

    g.setColour (proc.isEngineOnline() ? col::accent : col::idleBar);
    g.fillEllipse (dot);
}

void MainView::resized()
{
    const int right = getWidth() - 12;

    // --- Üst bar ---
    logoArea = { 14, 15, 20, 20 };
    title.setBounds (42, 9, 110, 16);
    madeIn.setBounds (42, 25, 110, 15);

    settingsButton.setBounds (right - 32, 9, 32, 30);
    libraryButton.setBounds (settingsButton.getX() - 8 - 32, 9, 32, 30);
    localButton.setBounds (libraryButton.getX() - 14 - 72, 12, 72, 24);
    cloudButton.setBounds (localButton.getX() - 58, 12, 58, 24);
    drumButton.setBounds (cloudButton.getX() - 16 - 60, 12, 60, 24);
    melodicButton.setBounds (drumButton.getX() - 70, 12, 70, 24);
    lcd.setBounds (170, 11, melodicButton.getX() - 14 - 170, 26);

    // --- Tarif ---
    historyButton.setBounds (12, 60, 36, 40);
    generateButton.setBounds (right - 116, 60, 116, 40);
    favoriteButton.setBounds (generateButton.getX() - 8 - 36, 60, 36, 40);
    promptBox.setBounds (56, 60, favoriteButton.getX() - 8 - 56, 40);
    negIconArea = { 12, 106, 36, 28 };
    negativeBox.setBounds (56, 106, promptBox.getWidth(), 28);

    // --- Orta bölüm ---
    panelRect = { right - 252, 146, 252, 214 };
    const int leftWidth = panelRect.getX() - 12 - 12;
    wave.setBounds (12, 146, leftWidth, 142);

    similarButton.setBounds (12 + leftWidth - 116, 310, 116, 50);
    const int tileArea = similarButton.getX() - 8 - 12;
    const int tileWidth = (tileArea - 3 * 8) / 4;

    for (int i = 0; i < WidiKeyProcessor::maxVariations; ++i)
        tiles[i].setBounds (12 + i * (tileWidth + 8), 310, tileWidth, 50);

    auto pr = panelRect.reduced (14, 10);
    pr.removeFromTop (22);

    for (auto* r : rows)
    {
        r->setBounds (pr.removeFromTop (28));
        pr.removeFromTop (2);
    }

    auto seedRow = juce::Rectangle<int> (pr.getX(), panelRect.getBottom() - 10 - 26, pr.getWidth(), 26);
    newSeedButton.setBounds (seedRow.removeFromRight (46));
    seedRow.removeFromRight (6);
    lockButton.setBounds (seedRow.removeFromRight (46));
    seedRow.removeFromRight (6);
    seedBox.setBounds (seedRow);

    // --- Ses ayarları ---
    knobStrip = { 12, 372, getWidth() - 24, 84 };
    const int units[] = { 4, 4, 3, 2 };
    const int unitWidth = knobStrip.getWidth() / 13;
    int x = knobStrip.getX();

    for (int i = 0; i < 4; ++i)
    {
        const int w = (i == 3) ? knobStrip.getRight() - x : units[i] * unitWidth;
        groupRects[i] = { x, knobStrip.getY(), w, knobStrip.getHeight() };
        x += w;
    }

    auto knobArea = [this] (int group) { return groupRects[group].reduced (6, 0).withTrimmedTop (20).withTrimmedBottom (6); };

    int k = 0;
    const int knobsInGroup[] = { 4, 4, 2, 2 };

    for (int gi = 0; gi < 4; ++gi)
    {
        auto area = knobArea (gi);
        const int slots = units[gi];
        const int cell = area.getWidth() / slots;

        for (int j = 0; j < knobsInGroup[gi]; ++j, ++k)
            knobs[k]->setBounds (area.getX() + j * cell, area.getY(), cell, area.getHeight());

        if (gi == 2)
            loopButton.setBounds (juce::Rectangle<int> (area.getX() + 2 * cell, area.getY(), cell, area.getHeight())
                                      .withSizeKeepingCentre (juce::jmin (cell - 8, 70), 28));
    }

    // --- Klavye ---
    keysRect = { 12, 468, getWidth() - 24, getHeight() - 12 - 468 };
    const auto keys = keysRect.reduced (8, 8);
    keyboard.setBounds (keys);
    keyboard.setKeyWidth ((float) keys.getWidth() / 28.0f);
}

void MainView::timerCallback()
{
    const auto status = proc.getStatus();
    const bool working = status == WidiKeyProcessor::Status::working;
    const bool drum = proc.isDrumMode();
    const bool local = proc.getEngine() == WidiKeyProcessor::Engine::local;
    const int count = (int) proc.apvts.getRawParameterValue ("variations")->load();

    melodicButton.setToggleState (! drum, juce::dontSendNotification);
    drumButton.setToggleState (drum, juce::dontSendNotification);
    cloudButton.setToggleState (! local, juce::dontSendNotification);
    localButton.setToggleState (local, juce::dontSendNotification);

    generateButton.setEnabled (! working);
    generateButton.setButtonText (working ? tr ("Üretiliyor…") : (count > 1 ? tr ("Üret  ×") + juce::String (count) : tr ("Üret")));

    similarButton.setEnabled (! working && local && proc.hasSample());
    favoriteButton.setToggleState (favorites.contains (promptBox.getText().trim()), juce::dontSendNotification);

    wave.setBusy (working);
    wave.repaint();

    const int selected = proc.getSelectedVariation();
    const auto info = proc.getVariationInfo();
    const int currentVersion = proc.getSampleVersion();

    if (currentVersion != lastVersion || drum != lastDrum)
    {
        lastVersion = currentVersion;
        lastDrum = drum;
        const bool has = selected < (int) info.size();
        wave.setData (has ? info[(size_t) selected].peaks : std::vector<float>(), has ? info[(size_t) selected].seconds : 0.0);
        keyboard.repaint();
        repaint();
    }

    for (int i = 0; i < WidiKeyProcessor::maxVariations; ++i)
    {
        const bool available = i < (int) info.size();
        juce::String note;

        if (available)
        {
            if (drum)
                note = tr ("tuş ") + juce::String::charToString ((juce::juce_wchar) "CDEF"[i]);
            else
                note = info[(size_t) i].pitched ? widi::noteName (info[(size_t) i].root) : tr ("perdesiz");
        }

        tiles[i].setData (i, available ? info[(size_t) i].peaks : std::vector<float>(), note,
                          available && i == selected && ! drum, available, working, drum);
    }

    // --- Bilgi ekranı ---
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

    juce::String rightText;

    if (! info.empty())
    {
        if (drum)
            rightText = tr ("DAVUL · ") + juce::String ((int) juce::jmin ((size_t) 4, info.size())) + tr (" SES");
        else if (selected < (int) info.size())
        {
            const auto& v = info[(size_t) selected];
            rightText = (v.pitched ? tr ("KÖK ") + widi::noteName (v.root) : tr ("PERDESİZ"))
                        + " · " + juce::String (v.seconds, 1) + " s · VAR " + variationLetter (selected);
        }
    }

    lcd.setText (trimText (left, 64), rightText, isError);
    seedBox.setText (tr ("SEED  ") + juce::String (proc.getBaseSeed()), proc.isSeedLocked() ? tr ("kilitli") : juce::String(), false);
    lockButton.setToggleState (proc.isSeedLocked(), juce::dontSendNotification);

    repaint (localButton.getBounds().expanded (4));
}

void MainView::generate (bool similar)
{
    if (proc.getEngine() == WidiKeyProcessor::Engine::cloud && WidiKeyProcessor::loadApiKey().isEmpty())
    {
        openSettings();
        return;
    }

    proc.startGeneration (promptBox.getText(), negativeBox.getText(), similar);
}

void MainView::showHistoryMenu()
{
    refreshFavorites();
    const auto history = PromptStore::getHistory();

    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("FAVORİLER"));

    if (favorites.isEmpty())
        menu.addItem (-1, tr ("Henüz favori yok · yıldıza bas"), false);

    for (int i = 0; i < favorites.size() && i < 30; ++i)
        menu.addItem (1000 + i, juce::String::fromUTF8 ("★  ") + trimText (favorites[i], 60));

    menu.addSectionHeader (tr ("SON TARİFLER"));

    if (history.isEmpty())
        menu.addItem (-2, tr ("Henüz geçmiş yok"), false);

    for (int i = 0; i < history.size(); ++i)
        menu.addItem (2000 + i, trimText (history[i], 60));

    if (! history.isEmpty())
    {
        menu.addSeparator();
        menu.addItem (1, tr ("Geçmişi temizle"));
    }

    juce::Component::SafePointer<MainView> safe (this);
    const auto favs = favorites;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&historyButton),
                        [safe, history, favs] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 1)
            PromptStore::clearHistory();
        else if (result >= 2000 && result - 2000 < history.size())
            safe->promptBox.setText (history[result - 2000], false);
        else if (result >= 1000 && result - 1000 < favs.size())
            safe->promptBox.setText (favs[result - 1000], false);
    });
}

void MainView::showLibraryMenu()
{
    const auto files = WidiKeyProcessor::listLibrary();
    const bool has = proc.hasSample();

    juce::PopupMenu menu;
    menu.addItem (1, tr ("Seçili sesi kütüphaneye kaydet…"), has);
    menu.addItem (2, tr ("Seçili sesi WAV olarak dışa aktar"), has);
    menu.addItem (3, tr ("Kütüphane klasörünü Finder'da aç"));
    menu.addSectionHeader (tr ("KÜTÜPHANEM"));

    if (files.isEmpty())
        menu.addItem (-1, tr ("Henüz kayıtlı ses yok"), false);

    for (int i = 0; i < files.size() && i < 60; ++i)
        menu.addItem (100 + i, files[i].getFileNameWithoutExtension());

    juce::Component::SafePointer<MainView> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&libraryButton),
                        [safe, files] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& p = safe->proc;

        if (result == 1)
            safe->askLibraryName();
        else if (result == 2)
        {
            const auto file = p.exportSelectedToWav();

            if (file.existsAsFile())
                file.revealToUser();
        }
        else if (result == 3)
        {
            auto folder = WidiKeyProcessor::getLibraryFolder();
            folder.createDirectory();
            folder.revealToUser();
        }
        else if (result >= 100 && result - 100 < files.size())
        {
            if (p.loadFromLibrary (files[result - 100]))
            {
                safe->promptBox.setText (p.getPrompt(), false);
                safe->negativeBox.setText (p.getNegative(), false);
            }
        }
    });
}

void MainView::askLibraryName()
{
    auto* window = new juce::AlertWindow (tr ("Kütüphaneye kaydet"),
                                          tr ("Bu sese bir isim ver. ~/Music/widi-key/Kütüphane klasörüne kaydedilecek."),
                                          juce::MessageBoxIconType::NoIcon, this);

    const auto letter = variationLetter (proc.getSelectedVariation());
    window->addTextEditor ("name", proc.getSampleName() + " " + letter, tr ("İsim"));
    window->addButton (tr ("Kaydet"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Vazgeç"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    auto* processor = &proc;

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, processor] (int result)
    {
        if (result == 1)
            processor->saveSelectedToLibrary (window->getTextEditorContents ("name"));
    }), true);
}

void MainView::openSettings()
{
    const juce::String message =
        tr ("BULUT (ElevenLabs)\nAPI anahtarını aşağıya yapıştır. Anahtar sadece bu Mac'te saklanır.\n\n"
            "YEREL MOTOR\nDurum: ") + (proc.isEngineOnline() ? tr ("çalışıyor") : tr ("kapalı")) +
        tr ("\nKurmak ya da güncellemek için Terminal'e şunu yapıştır:\n") + installCommand +
        tr ("\n\nKÜTÜPHANE\n~/Music/widi-key");

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
            juce::SystemClipboard::copyTextToClipboard (installCommand);
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
