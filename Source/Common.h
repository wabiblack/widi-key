#pragma once

#include <juce_graphics/juce_graphics.h>

namespace widi
{
    // Türkçe karakterli metinleri güvenle String'e çevirir.
    inline juce::String tr (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    namespace col
    {
        const juce::Colour bg        { 0xff111317 };
        const juce::Colour panel     { 0xff181b21 };
        const juce::Colour border    { 0xff262a32 };
        const juce::Colour field     { 0xff0e1014 };
        const juce::Colour chip      { 0xff20242b };
        const juce::Colour segOn     { 0xff2a2e36 };
        const juce::Colour knobTrack { 0xff2a2e36 };
        const juce::Colour knobCap   { 0xff22262e };
        const juce::Colour text      { 0xffe8eaee };
        const juce::Colour dim       { 0xff9aa0aa };
        const juce::Colour accent    { 0xff3ddcb4 };
        const juce::Colour onAccent  { 0xff0b0d10 };
        const juce::Colour error     { 0xffff7a7a };
    }
}
