#pragma once

#include <juce_graphics/juce_graphics.h>

namespace widi
{
    // Türkçe karakterli metinleri güvenle String'e çevirir.
    inline juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    namespace col
    {
        const juce::Colour bg        { 0xff0c0d10 };
        const juce::Colour header    { 0xff111216 };
        const juce::Colour panel     { 0xff15171b };
        const juce::Colour well      { 0xff08090b };
        const juce::Colour tile      { 0xff101115 };
        const juce::Colour line      { 0xff23262d };
        const juce::Colour fieldLine { 0xff2c3038 };
        const juce::Colour segOn     { 0xff23262d };
        const juce::Colour knobTrack { 0xff2a2e36 };
        const juce::Colour knobCap   { 0xff22262e };
        const juce::Colour text      { 0xffe6e8ec };
        const juce::Colour soft      { 0xffa9aeb8 };
        const juce::Colour dim       { 0xff8c929d };
        const juce::Colour faint     { 0xff6e7480 };
        const juce::Colour idleBar   { 0xff4a505b };
        const juce::Colour accent    { 0xff3ddcb4 };
        const juce::Colour envelope  { 0xfff2b35b };
        const juce::Colour onAccent  { 0xff08090b };
        const juce::Colour error     { 0xffff7a7a };
    }

    inline juce::Font uiFont (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size).withStyle (bold ? "Bold" : "Regular"));
    }

    inline juce::Font monoFont (float size)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
    }
}
