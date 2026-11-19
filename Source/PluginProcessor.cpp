#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Common.h"

using widi::tr;

namespace
{
    const char* cloudSuffixMelodic = ", single isolated note, no background noise";
    const char* cloudSuffixDrum    = ", single drum hit, one shot, percussive, dry, no background noise";
    const char* localSuffixMelodic = ", single note";
    const char* localSuffixDrum    = ", single drum hit, one shot, percussive, dry";

    // Çoklu örnek: her kayıt farklı bir registerda (kalından inceye)
    const char* registerSuffixes[4] = { ", low register, low pitched note", ", lower middle register",
                                        ", upper middle register", ", high register, high pitched note" };

    std::vector<juce::String> registersFor (int count)
    {
        std::vector<juce::String> list;

        for (int i = 0; i < count; ++i)
        {
            const int index = count <= 1 ? 1 : juce::roundToInt ((double) i * 3.0 / (double) (count - 1));
            list.push_back (registerSuffixes[juce::jlimit (0, 3, index)]);
        }

        return list;
    }

    struct Translation { juce::String text, realism; };

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "widi-key";
        o.filenameSuffix      = ".settings";
        o.folderName          = "widi-key";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    juce::String engineUrl (const char* path)
    {
        return "http://127.0.0.1:" + juce::String (WidiKeyProcessor::enginePort) + path;
    }

    bool httpPost (const juce::String& address, const juce::String& body, const juce::String& headers,
                   int timeoutMs, juce::MemoryBlock& response, int& statusCode)
    {
        const auto url = juce::URL (address).withPOSTData (body);
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders (headers)
                           .withConnectionTimeoutMs (timeoutMs)
                           .withStatusCode (&statusCode);

        auto stream = url.createInputStream (options);

        if (stream == nullptr)
            return false;

        stream->readIntoMemoryBlock (response);
        return true;
    }

    Translation translateViaEngine (const juce::String& text, bool drum)
    {
        if (text.trim().isEmpty())
            return { text, {} };

        auto* obj = new juce::DynamicObject();
        juce::var body (obj);
        obj->setProperty ("text", text);
        obj->setProperty ("drum", drum);

        juce::MemoryBlock response;
        int statusCode = 0;

        if (httpPost (engineUrl ("/translate"), juce::JSON::toString (body, true),
                      "Content-Type: application/json", 60000, response, statusCode) && statusCode == 200)
        {
            const auto parsed = juce::JSON::parse (response.toString());
            const auto translated = parsed.getProperty ("text", {}).toString();

            if (translated.isNotEmpty())
                return { translated, parsed.getProperty ("realism", {}).toString() };
        }

        return { text, {} };
    }

    juce::String safeFileName (const juce::String& name)
    {
        auto cleaned = juce::File::createLegalFileName (name.trim()).replaceCharacters ("/:", "--");
        return cleaned.isEmpty() ? juce::String ("widi-key") : cleaned.substring (0, 60);
    }

    juce::String formatSeconds (float v, int)
    {
        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                        : juce::String (v, 1) + " s";
    }

    juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    juce::String formatHz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String formatDb (float v, int)      { return juce::String (v, 1) + " dB"; }
    juce::String formatPlain (float v, int)   { return juce::String (v, 2); }
    juce::String formatOctaves (float v, int) { return juce::String (v, 1) + " okt"; }
}

//==============================================================================
namespace PromptStore
{
    static juce::StringArray readList (const char* key)
    {
        juce::PropertiesFile file (settingsOptions());
        juce::StringArray list;
        list.addLines (file.getValue (key));
        list.removeEmptyStrings();
        return list;
    }

    static void writeList (const char* key, const juce::StringArray& list)
    {
        juce::PropertiesFile file (settingsOptions());
        file.setValue (key, list.joinIntoString ("\n"));
        file.saveIfNeeded();
    }

    juce::StringArray getHistory()   { return readList ("history"); }
    juce::StringArray getFavorites() { return readList ("favorites"); }

    void addToHistory (const juce::String& prompt)
    {
        const auto p = prompt.trim().replaceCharacters ("\r\n", "  ");

        if (p.isEmpty())
            return;

        auto list = getHistory();
        list.removeString (p);
        list.insert (0, p);

        while (list.size() > 30)
            list.remove (list.size() - 1);

        writeList ("history", list);
    }

    bool isFavorite (const juce::String& prompt) { return getFavorites().contains (prompt.trim()); }

    void toggleFavorite (const juce::String& prompt)
    {
        const auto p = prompt.trim().replaceCharacters ("\r\n", "  ");

        if (p.isEmpty())
            return;

        auto list = getFavorites();

        if (list.contains (p))
            list.removeString (p);
        else
            list.insert (0, p);

        writeList ("favorites", list);
    }

    void clearHistory() { writeList ("history", {}); }
}

//==============================================================================
// Yerel motorun açık olup olmadığını arka planda kontrol eder.
class WidiKeyProcessor::EngineMonitor : public juce::Thread
{
public:
    explicit EngineMonitor (WidiKeyProcessor& o) : juce::Thread ("widi-key engine monitor"), owner (o) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            int statusCode = 0;
            auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                               .withConnectionTimeoutMs (1500)
                               .withStatusCode (&statusCode);

            auto stream = juce::URL (engineUrl ("/health")).createInputStream (options);
            owner.engineOnline = (stream != nullptr && statusCode == 200);
            stream.reset();

            wait (3000);
        }
    }

private:
    WidiKeyProcessor& owner;
};

//==============================================================================
// Ses üretimini arka planda yapar (yerel motor ya da ElevenLabs).
class WidiKeyProcessor::GenerateThread : public juce::Thread
{
public:
    struct Request
    {
        juce::String prompt, negative;
        Engine engine = Engine::local;
        bool drum = false;
        std::vector<juce::String> registers;   // çoklu örnekte kayıt başına ek
        float seconds = 3.0f;
        int steps = 8;
        float strength = 0.0f;
        float similarity = 0.5f;
        juce::MemoryBlock initAudio;
        std::vector<int> seeds;
    };

    GenerateThread (WidiKeyProcessor& o, Request r)
        : juce::Thread ("widi-key generator"), owner (o), request (std::move (r)) {}

    void run() override
    {
        std::vector<Variation> results;
        juce::String error;

        const bool ok = request.engine == Engine::local ? runLocal (results, error)
                                                        : runCloud (results, error);
        if (threadShouldExit())
            return;

        if (! ok)
        {
            owner.setStatus (Status::error, error);
            return;
        }

        owner.setStatus (Status::working, tr ("Notalar ayarlanıyor…"));

        for (auto& v : results)
        {
            if (! owner.decodeVariation (v, error))
            {
                owner.setStatus (Status::error, error);
                return;
            }
        }

        owner.setVariations (std::move (results), request.prompt);
        owner.setStatus (Status::ok, {});
    }

private:
    bool runLocal (std::vector<Variation>& results, juce::String& error)
    {
        if (! owner.isEngineOnline())
        {
            error = tr ("Yerel motor kapalı. Ayarlar'daki kurulum komutunu çalıştır.");
            return false;
        }

        auto* obj = new juce::DynamicObject();
        juce::var body (obj);
        juce::Array<juce::var> seedList;

        for (auto s : request.seeds)
            seedList.add (s);

        obj->setProperty ("prompt", request.prompt);
        obj->setProperty ("negative", request.negative);
        obj->setProperty ("suffix", juce::String (request.drum ? localSuffixDrum : localSuffixMelodic));
        obj->setProperty ("drum", request.drum);

        if (! request.registers.empty())
        {
            juce::Array<juce::var> suffixList;

            for (const auto& r : request.registers)
                suffixList.add (r);

            obj->setProperty ("suffixes", suffixList);
        }
        obj->setProperty ("seconds", (double) request.seconds);
        obj->setProperty ("steps", request.steps);
        obj->setProperty ("cfg", 1.0 + (double) request.strength * 2.0);
        obj->setProperty ("seeds", seedList);

        if (request.initAudio.getSize() > 0)
        {
            obj->setProperty ("init_audio", juce::Base64::toBase64 (request.initAudio.getData(), request.initAudio.getSize()));
            obj->setProperty ("similarity", (double) request.similarity);
        }

        owner.setStatus (Status::working, tr ("Mac'inde üretiliyor… (") + juce::String ((int) request.seeds.size())
                                              + tr (" varyasyon)"));

        juce::MemoryBlock response;
        int statusCode = 0;

        if (! httpPost (engineUrl ("/generate"), juce::JSON::toString (body, true),
                        "Content-Type: application/json", 900000, response, statusCode))
        {
            error = tr ("Yerel motora bağlanılamadı.");
            return false;
        }

        const auto parsed = juce::JSON::parse (response.toString());

        if (statusCode != 200)
        {
            error = tr ("Yerel motor hatası: ") + parsed.getProperty ("error", response.toString().substring (0, 120)).toString();
            return false;
        }

        const auto* audio = parsed.getProperty ("audio", {}).getArray();
        const auto* seeds = parsed.getProperty ("seeds", {}).getArray();

        if (audio == nullptr || audio->isEmpty())
        {
            error = tr ("Yerel motor ses döndürmedi.");
            return false;
        }

        for (int i = 0; i < audio->size(); ++i)
        {
            juce::MemoryOutputStream decoded;

            if (! juce::Base64::convertFromBase64 (decoded, (*audio)[i].toString()))
                continue;

            Variation v;
            v.bytes = decoded.getMemoryBlock();
            v.seed = (seeds != nullptr && i < seeds->size()) ? (int) (*seeds)[i] : -1;
            results.push_back (std::move (v));
        }

        return ! results.empty();
    }

    bool runCloud (std::vector<Variation>& results, juce::String& error)
    {
        const auto key = WidiKeyProcessor::loadApiKey();

        if (key.isEmpty())
        {
            error = tr ("Önce Ayarlar'dan ElevenLabs API anahtarını gir.");
            return false;
        }

        // Türkçe tarifi gizlice İngilizceye çevir (motor açıksa)
        Translation prompt { request.prompt, {} };
        auto englishNegative = request.negative;

        if (owner.isEngineOnline())
        {
            owner.setStatus (Status::working, tr ("Tarif hazırlanıyor…"));
            prompt = translateViaEngine (request.prompt, request.drum);
            englishNegative = translateViaEngine (request.negative, request.drum).text;
        }

        const auto baseText = prompt.text + prompt.realism + (request.drum ? cloudSuffixDrum : cloudSuffixMelodic);
        juce::String without;

        if (englishNegative.trim().isNotEmpty())
            without << ", without " << englishNegative.trim();

        const int count = (int) request.seeds.size();

        for (int i = 0; i < count; ++i)
        {
            if (threadShouldExit())
                return false;

            owner.setStatus (Status::working, tr ("Bulutta üretiliyor… (") + juce::String (i + 1) + "/" + juce::String (count) + ")");

            auto* obj = new juce::DynamicObject();
            juce::var body (obj);
            const auto reg = i < (int) request.registers.size() ? request.registers[(size_t) i] : juce::String();
            obj->setProperty ("text", baseText + reg + without);
            obj->setProperty ("duration_seconds", (double) request.seconds);
            obj->setProperty ("prompt_influence", 0.3 + 0.7 * (double) request.strength);

            juce::MemoryBlock response;
            int statusCode = 0;

            if (! httpPost ("https://api.elevenlabs.io/v1/sound-generation", juce::JSON::toString (body, true),
                            "xi-api-key: " + key + "\r\nContent-Type: application/json\r\nAccept: audio/mpeg",
                            120000, response, statusCode))
            {
                error = tr ("Sunucuya bağlanılamadı. İnternet bağlantını kontrol et.");
                return false;
            }

            if (statusCode != 200)
            {
                error = describeCloudError (statusCode, response.toString());
                return false;
            }

            Variation v;
            v.bytes = response;
            v.seed = -1;
            results.push_back (std::move (v));
        }

        return true;
    }

    static juce::String describeCloudError (int code, const juce::String& text)
    {
        if (code == 401)
            return tr ("API anahtarı geçersiz. Ayarlar'dan kontrol et.");

        if (code == 429)
            return tr ("Çok fazla istek ya da kredin bitti. Biraz sonra tekrar dene.");

        juce::String message;

        if (auto* detail = juce::JSON::parse (text).getProperty ("detail", {}).getDynamicObject())
            message = detail->getProperty ("message").toString();

        if (message.isEmpty())
            message = text.substring (0, 120);

        return tr ("Bulut hatası (") + juce::String (code) + "): " + message;
    }

    WidiKeyProcessor& owner;
    Request request;
};

//==============================================================================
WidiKeyProcessor::WidiKeyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    for (int i = 0; i < 16; ++i)
        synth.addVoice (new widi::WidiVoice (voiceSettings));

    synth.setNoteStealingEnabled (true);
    baseSeed = juce::Random::getSystemRandom().nextInt ({ 1000, 9999 });
    apvts.addParameterListener ("mode", this);

    // Yeni açılan plugin Çoklu örnek modunda başlar (eski projeler kendi modunda açılır)
    if (auto* modeParam = apvts.getParameter ("mode"))
        modeParam->setValueNotifyingHost (modeParam->convertTo0to1 (2.0f));

    monitor = std::make_unique<EngineMonitor> (*this);
    monitor->startThread();
}

WidiKeyProcessor::~WidiKeyProcessor()
{
    apvts.removeParameterListener ("mode", this);
    cancelPendingUpdate();

    if (monitor != nullptr)
        monitor->stopThread (3000);

    if (generator != nullptr)
        generator->stopThread (4000);
}

juce::AudioProcessorValueTreeState::ParameterLayout WidiKeyProcessor::createLayout()
{
    using Param = juce::AudioParameterFloat;
    using IntParam = juce::AudioParameterInt;
    using Attr  = juce::AudioParameterFloatAttributes;

    auto timeRange = [] (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (0.5f);
        return r;
    };

    juce::NormalisableRange<float> cutoffRange (40.0f, 20000.0f);
    cutoffRange.setSkewForCentre (1000.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // v0.2'den gelenler (kimlikler değişmedi, eski projeler açılır)
    layout.add (std::make_unique<Param> (juce::ParameterID { "attack", 1 },    "Attack",   timeRange (0.001f, 5.0f), 0.005f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "decay", 1 },     "Decay",    timeRange (0.001f, 5.0f), 0.4f,   Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "sustain", 1 },   "Sustain",  juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "release", 1 },   "Release",  timeRange (0.01f, 8.0f), 1.2f,    Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "cutoff", 1 },    "Kesim",    cutoffRange, 20000.0f,            Attr().withStringFromValueFunction (formatHz)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "resonance", 1 }, "Rezonans", juce::NormalisableRange<float> (0.5f, 5.0f), 0.707f, Attr().withStringFromValueFunction (formatPlain)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverb", 1 },    "Reverb",   juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "gain", 1 },      "Ses",      juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, Attr().withStringFromValueFunction (formatDb)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "length", 1 },   tr ("Süre"),  juce::NormalisableRange<float> (0.5f, 11.0f, 0.1f), 3.0f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "strength", 1 }, tr ("Tarife bağlılık"), juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<IntParam> (juce::ParameterID { "steps", 1 },      "Kalite",    4, 32, 8));
    layout.add (std::make_unique<IntParam> (juce::ParameterID { "variations", 1 }, "Varyasyon", 1, maxVariations, 4));

    // v0.3 yenileri
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mod", juce::StringArray { "Melodik", "Davul", tr ("Çoklu") }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "loop", 1 }, tr ("Döngü"), false));
    layout.add (std::make_unique<Param> (juce::ParameterID { "velocity", 1 },   "Velocity",       juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "glide", 1 },      "Glide",          timeRange (0.0f, 1.0f), 0.0f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "fenvAmount", 1 }, tr ("Filtre zarfı"), juce::NormalisableRange<float> (0.0f, 5.0f), 0.0f, Attr().withStringFromValueFunction (formatOctaves)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "fenvDecay", 1 },  tr ("Zarf süresi"),  timeRange (0.01f, 3.0f), 0.3f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "similarity", 1 }, "Benzerlik",      juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, Attr().withStringFromValueFunction (formatPercent)));
    return layout;
}

//==============================================================================
void WidiKeyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };
    reverb.prepare (spec);
    reverb.reset();
    keyboardState.reset();
}

bool WidiKeyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void WidiKeyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    auto value = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    voiceSettings.amp = { value ("attack"), value ("decay"), value ("sustain"), value ("release") };
    voiceSettings.velocityAmount   = value ("velocity");
    voiceSettings.glideSeconds     = value ("glide");
    voiceSettings.loop             = value ("loop") > 0.5f;
    voiceSettings.cutoff           = value ("cutoff");
    voiceSettings.resonance        = value ("resonance");
    voiceSettings.filterEnvOctaves = value ("fenvAmount");
    voiceSettings.filterEnvDecay   = value ("fenvDecay");

    synth.renderNextBlock (buffer, midi, 0, numSamples);

    const float mix = value ("reverb");
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.75f;
    rp.damping  = 0.4f;
    rp.width    = 1.0f;
    rp.wetLevel = mix * 0.6f;
    rp.dryLevel = 1.0f - mix * 0.4f;
    reverb.setParameters (rp);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);

    buffer.applyGain (juce::Decibels::decibelsToGain (value ("gain")));
}

//==============================================================================
void WidiKeyProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "mode")
        triggerAsyncUpdate();
}

void WidiKeyProcessor::handleAsyncUpdate()
{
    rebuildSounds();
}

bool WidiKeyProcessor::isDrumMode() const
{
    return juce::roundToInt (apvts.getRawParameterValue ("mode")->load()) == 1;
}

bool WidiKeyProcessor::isMultiMode() const
{
    return juce::roundToInt (apvts.getRawParameterValue ("mode")->load()) == 2;
}

void WidiKeyProcessor::previewVariation (int index)
{
    int note = 60;

    {
        const juce::ScopedLock sl (dataLock);

        if (index < 0 || index >= (int) variations.size())
            return;

        const auto& v = variations[(size_t) index];

        if (isDrumMode())
            note = 60 + widi::drumNoteClasses[juce::jlimit (0, 3, index)];
        else if (v.pitch.pitched)
            note = juce::jlimit (0, 127, juce::roundToInt (v.pitch.midi));
    }

    keyboardState.noteOn (1, note, 0.85f);
    auto* state = &keyboardState;
    juce::Timer::callAfterDelay (700, [state, note] { state->noteOff (1, note, 0.0f); });
}

//==============================================================================
void WidiKeyProcessor::startGeneration (const juce::String& prompt, const juce::String& negative, bool similarToSelected)
{
    if (generator != nullptr && generator->isThreadRunning())
        return;

    const auto cleaned = prompt.trim();

    if (cleaned.isEmpty())
    {
        setStatus (Status::error, tr ("Önce sesi tarif et."));
        return;
    }

    if (similarToSelected && engine.load() != Engine::local)
    {
        setStatus (Status::error, tr ("\"Benzerini üret\" sadece Yerel modda çalışır."));
        return;
    }

    {
        const juce::ScopedLock sl (dataLock);
        lastPrompt = cleaned;
        lastNegative = negative.trim();
    }

    PromptStore::addToHistory (cleaned);

    if (! seedLocked.load())
        rerollSeed();

    GenerateThread::Request request;
    request.prompt     = cleaned;
    request.negative   = negative.trim();
    request.engine     = engine.load();
    request.drum       = isDrumMode();
    request.seconds    = apvts.getRawParameterValue ("length")->load();
    request.steps      = (int) apvts.getRawParameterValue ("steps")->load();
    request.strength   = apvts.getRawParameterValue ("strength")->load();
    request.similarity = apvts.getRawParameterValue ("similarity")->load();

    if (similarToSelected)
        request.initAudio = selectedAsWav();

    const int count = juce::jlimit (1, maxVariations, (int) apvts.getRawParameterValue ("variations")->load());

    for (int i = 0; i < count; ++i)
        request.seeds.push_back (baseSeed.load() + i * 1009);

    if (isMultiMode() && count > 1)
        request.registers = registersFor (count);

    setStatus (Status::working, tr ("Hazırlanıyor…"));

    generator.reset();
    generator = std::make_unique<GenerateThread> (*this, std::move (request));
    generator->startThread();
}

void WidiKeyProcessor::rerollSeed()
{
    baseSeed = juce::Random::getSystemRandom().nextInt ({ 1000, 99999 });
}

std::unique_ptr<juce::AudioFormatReader> WidiKeyProcessor::createReader (const juce::MemoryBlock& bytes)
{
    const bool isWav = bytes.getSize() > 4 && std::memcmp (bytes.getData(), "RIFF", 4) == 0;

    if (isWav)
        return std::unique_ptr<juce::AudioFormatReader> (
            formatManager.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, true)));

   #if JUCE_USE_MP3AUDIOFORMAT
    juce::MP3AudioFormat mp3;

    if (auto* reader = mp3.createReaderFor (new juce::MemoryInputStream (bytes, true), true))
        return std::unique_ptr<juce::AudioFormatReader> (reader);
   #endif

    return std::unique_ptr<juce::AudioFormatReader> (
        formatManager.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, true)));
}

bool WidiKeyProcessor::decodeVariation (Variation& variation, juce::String& error)
{
    auto reader = createReader (variation.bytes);

    if (reader == nullptr || reader->sampleRate <= 0)
    {
        error = tr ("Gelen ses dosyası okunamadı.");
        return false;
    }

    const int length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);

    auto audio = std::make_shared<juce::AudioBuffer<float>> (channels, juce::jmax (1, length));
    audio->clear();
    reader->read (audio.get(), 0, length, 0, true, channels > 1);

    variation.audio = audio;
    variation.sourceRate = reader->sampleRate;
    variation.seconds = length / reader->sampleRate;
    variation.pitch = widi::detectPitch (*audio, reader->sampleRate);
    variation.peaks.assign (160, 0.0f);

    if (length > 0)
    {
        float maxPeak = 0.0f;
        const auto count = variation.peaks.size();

        for (size_t i = 0; i < count; ++i)
        {
            const int start = (int) ((i * (size_t) length) / count);
            const int end   = juce::jmin (length, juce::jmax (start + 1, (int) (((i + 1) * (size_t) length) / count)));
            float peak = 0.0f;

            for (int ch = 0; ch < channels; ++ch)
                peak = juce::jmax (peak, audio->getMagnitude (ch, start, end - start));

            variation.peaks[i] = peak;
            maxPeak = juce::jmax (maxPeak, peak);
        }

        if (maxPeak > 0.0f)
            for (auto& p : variation.peaks)
                p /= maxPeak;
    }

    return true;
}

void WidiKeyProcessor::setVariations (std::vector<Variation>&& newVariations, const juce::String& name)
{
    {
        const juce::ScopedLock sl (dataLock);
        variations = std::move (newVariations);
        selected = 0;
        sampleName = name;
    }

    rebuildSounds();
}

void WidiKeyProcessor::selectVariation (int index)
{
    {
        const juce::ScopedLock sl (dataLock);

        if (index < 0 || index >= (int) variations.size() || index == selected)
            return;

        selected = index;
    }

    rebuildSounds();
}

void WidiKeyProcessor::rebuildSounds()
{
    std::vector<juce::SynthesiserSound::Ptr> sounds;
    const bool drum = isDrumMode();

    {
        const juce::ScopedLock sl (dataLock);

        if (drum)
        {
            // Davul modu: her varyasyon kendi tuşunda, perdesi sabit (C, D, E, F)
            for (size_t i = 0; i < variations.size() && i < 4; ++i)
            {
                juce::BigInteger notes;

                for (int n = 0; n < 128; ++n)
                    if (n % 12 == widi::drumNoteClasses[i])
                        notes.setBit (n);

                const auto& v = variations[i];
                sounds.push_back (new widi::WidiSound (v.audio, v.sourceRate, 60.0, notes, true));
            }
        }
        else if (isMultiMode() && variations.size() > 1)
        {
            // Çoklu örnek: her tuş, kök notası kendisine en yakın kaydı çalar
            std::vector<size_t> pitched;

            for (size_t i = 0; i < variations.size(); ++i)
                if (variations[i].pitch.pitched)
                    pitched.push_back (i);

            if (pitched.empty() && selected >= 0 && selected < (int) variations.size())
                pitched.push_back ((size_t) selected);

            std::vector<juce::BigInteger> zones (pitched.size());

            for (int n = 0; n < 128; ++n)
            {
                size_t best = 0;
                double bestDistance = 1.0e9;

                for (size_t z = 0; z < pitched.size(); ++z)
                {
                    const auto& v = variations[pitched[z]];
                    const double root = v.pitch.pitched ? v.pitch.midi : 60.0;
                    const double distance = std::abs (n - root);

                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = z;
                    }
                }

                zones[best].setBit (n);
            }

            for (size_t z = 0; z < pitched.size(); ++z)
            {
                const auto& v = variations[pitched[z]];
                sounds.push_back (new widi::WidiSound (v.audio, v.sourceRate, v.pitch.pitched ? v.pitch.midi : 60.0, zones[z], false));
            }
        }
        else if (selected >= 0 && selected < (int) variations.size())
        {
            // Melodik mod: seçili ses tüm klavyeye, gerçek notasına göre akortlu
            juce::BigInteger notes;
            notes.setRange (0, 128, true);

            const auto& v = variations[(size_t) selected];
            const double root = v.pitch.pitched ? v.pitch.midi : 60.0;
            sounds.push_back (new widi::WidiSound (v.audio, v.sourceRate, root, notes, false));
        }
    }

    synth.allNotesOff (0, false);
    synth.clearSounds();

    for (auto& s : sounds)
        synth.addSound (s);

    ++sampleVersion;
}

std::vector<WidiKeyProcessor::VariationInfo> WidiKeyProcessor::getVariationInfo() const
{
    const juce::ScopedLock sl (dataLock);
    std::vector<VariationInfo> info;

    for (const auto& v : variations)
        info.push_back ({ v.peaks, v.seconds, v.seed, v.pitch.pitched, v.pitch.midi });

    return info;
}

int WidiKeyProcessor::getSelectedVariation() const { const juce::ScopedLock sl (dataLock); return selected; }
bool WidiKeyProcessor::hasSample() const          { const juce::ScopedLock sl (dataLock); return ! variations.empty(); }

void WidiKeyProcessor::setStatus (Status newStatus, const juce::String& message)
{
    {
        const juce::ScopedLock sl (dataLock);
        statusMessage = message;
    }
    status = newStatus;
}

juce::String WidiKeyProcessor::getStatusMessage() const { const juce::ScopedLock sl (dataLock); return statusMessage; }
juce::String WidiKeyProcessor::getPrompt() const        { const juce::ScopedLock sl (dataLock); return lastPrompt; }
juce::String WidiKeyProcessor::getNegative() const      { const juce::ScopedLock sl (dataLock); return lastNegative; }
juce::String WidiKeyProcessor::getSampleName() const    { const juce::ScopedLock sl (dataLock); return sampleName; }

//==============================================================================
juce::MemoryBlock WidiKeyProcessor::bufferToWav (const juce::AudioBuffer<float>& audio, double rate)
{
    juce::MemoryBlock block;
    juce::WavAudioFormat wav;

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (audio.getNumChannels())
                                                   .withBitsPerSample (24));

    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());

    writer.reset();
    return block;
}

juce::MemoryBlock WidiKeyProcessor::selectedAsWav() const
{
    const juce::ScopedLock sl (dataLock);

    if (selected < 0 || selected >= (int) variations.size())
        return {};

    const auto& v = variations[(size_t) selected];

    if (v.bytes.getSize() > 4 && std::memcmp (v.bytes.getData(), "RIFF", 4) == 0)
        return v.bytes;

    return v.audio != nullptr ? bufferToWav (*v.audio, v.sourceRate) : juce::MemoryBlock();
}

juce::File WidiKeyProcessor::getLibraryFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("widi-key").getChildFile (tr ("Kütüphane"));
}

juce::File WidiKeyProcessor::getExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("widi-key").getChildFile (tr ("Dışa Aktarılanlar"));
}

juce::File WidiKeyProcessor::exportSelectedToWav()
{
    const auto wav = selectedAsWav();

    if (wav.getSize() == 0)
        return {};

    auto folder = getExportFolder();
    folder.createDirectory();

    const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + getSelectedVariation()));
    auto file = folder.getNonexistentChildFile (safeFileName (getSampleName()) + " " + letter, ".wav", false);

    return file.replaceWithData (wav.getData(), wav.getSize()) ? file : juce::File();
}

bool WidiKeyProcessor::saveSelectedToLibrary (const juce::String& name)
{
    const auto wav = selectedAsWav();

    if (wav.getSize() == 0)
        return false;

    auto folder = getLibraryFolder();
    folder.createDirectory();

    auto file = folder.getNonexistentChildFile (safeFileName (name), ".wav", false);

    if (! file.replaceWithData (wav.getData(), wav.getSize()))
        return false;

    // Tarifi yanına not olarak kaydet
    auto* obj = new juce::DynamicObject();
    juce::var meta (obj);
    obj->setProperty ("prompt", getPrompt());
    obj->setProperty ("negative", getNegative());
    file.withFileExtension ("json").replaceWithText (juce::JSON::toString (meta));
    return true;
}

juce::Array<juce::File> WidiKeyProcessor::listLibrary()
{
    auto files = getLibraryFolder().findChildFiles (juce::File::findFiles, false, "*.wav");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return files;
}

bool WidiKeyProcessor::loadFromLibrary (const juce::File& file)
{
    Variation v;

    if (! file.loadFileAsData (v.bytes))
        return false;

    juce::String error;

    if (! decodeVariation (v, error))
    {
        setStatus (Status::error, error);
        return false;
    }

    const auto meta = juce::JSON::parse (file.withFileExtension ("json"));

    {
        const juce::ScopedLock sl (dataLock);
        lastPrompt = meta.getProperty ("prompt", lastPrompt).toString();
        lastNegative = meta.getProperty ("negative", lastNegative).toString();
    }

    std::vector<Variation> list;
    list.push_back (std::move (v));
    setVariations (std::move (list), file.getFileNameWithoutExtension());
    setStatus (Status::ok, {});
    return true;
}

//==============================================================================
juce::String WidiKeyProcessor::loadApiKey()
{
    juce::PropertiesFile file (settingsOptions());
    return file.getValue ("elevenlabs_key").trim();
}

void WidiKeyProcessor::saveApiKey (const juce::String& key)
{
    juce::PropertiesFile file (settingsOptions());
    file.setValue ("elevenlabs_key", key.trim());
    file.saveIfNeeded();
}

//==============================================================================
// Proje kaydedilince ayarlar, tarif ve tüm varyasyonlar projeyle birlikte saklanır.
void WidiKeyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("WIDIKEY");
    root.appendChild (apvts.copyState(), nullptr);
    root.setProperty ("engine", engine.load() == Engine::local ? "local" : "cloud", nullptr);
    root.setProperty ("seedLocked", seedLocked.load(), nullptr);
    root.setProperty ("baseSeed", baseSeed.load(), nullptr);

    {
        const juce::ScopedLock sl (dataLock);
        root.setProperty ("prompt", lastPrompt, nullptr);
        root.setProperty ("negative", lastNegative, nullptr);
        root.setProperty ("name", sampleName, nullptr);
        root.setProperty ("selected", selected, nullptr);

        for (const auto& v : variations)
        {
            juce::ValueTree child ("VARIATION");
            child.setProperty ("seed", v.seed, nullptr);
            child.setProperty ("audio", v.bytes.toBase64Encoding(), nullptr);
            root.appendChild (child, nullptr);
        }
    }

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void WidiKeyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml (*xml);

    if (! root.isValid())
        return;

    auto params = root.getChildWithName (apvts.state.getType());

    if (params.isValid())
    {
        const bool hasMode = params.getChildWithProperty ("id", "mode").isValid();
        apvts.replaceState (params);

        // Mod kaydı olmayan eski (v0.2) projeler eskisi gibi Melodik açılsın
        if (! hasMode)
            if (auto* modeParam = apvts.getParameter ("mode"))
                modeParam->setValueNotifyingHost (0.0f);
    }

    engine = root["engine"].toString() == "cloud" ? Engine::cloud : Engine::local;
    seedLocked = (bool) root.getProperty ("seedLocked", false);
    baseSeed = (int) root.getProperty ("baseSeed", baseSeed.load());

    const auto name = root["name"].toString();

    {
        const juce::ScopedLock sl (dataLock);
        lastPrompt = root["prompt"].toString();
        lastNegative = root["negative"].toString();
    }

    std::vector<Variation> restored;

    for (int i = 0; i < root.getNumChildren(); ++i)
    {
        auto child = root.getChild (i);

        if (! child.hasType ("VARIATION"))
            continue;

        Variation v;
        v.seed = (int) child.getProperty ("seed", -1);

        if (! v.bytes.fromBase64Encoding (child["audio"].toString()))
            continue;

        juce::String error;

        if (decodeVariation (v, error))
            restored.push_back (std::move (v));
    }

    if (! restored.empty())
    {
        const int wanted = (int) root.getProperty ("selected", 0);
        setVariations (std::move (restored), name);
        selectVariation (wanted);
    }
    else
    {
        rebuildSounds();
    }
}

juce::AudioProcessorEditor* WidiKeyProcessor::createEditor()
{
    return new WidiKeyEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WidiKeyProcessor();
}
