#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Common.h"

using widi::tr;

namespace
{
    constexpr int rootNote = 60; // Logic'te C3

    const char* cloudSuffix = ", single isolated note, one shot, clean, no background noise";
    const char* localSuffix = ", single note, one shot, clean";

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

    juce::String formatDb (float v, int)     { return juce::String (v, 1) + " dB"; }
    juce::String formatPlain (float v, int)  { return juce::String (v, 2); }
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
        juce::String prompt;
        Engine engine = Engine::local;
        float seconds = 3.0f;
        int steps = 8;
        float strength = 0.0f;
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

        for (auto& v : results)
        {
            if (! owner.decodeVariation (v, request.prompt, error))
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
            error = tr ("Yerel motor kapalı. Kurulum için README'deki tek satırlık komutu çalıştır.");
            return false;
        }

        auto* obj = new juce::DynamicObject();
        juce::var body (obj);
        juce::Array<juce::var> seedList;

        for (auto s : request.seeds)
            seedList.add (s);

        obj->setProperty ("prompt", request.prompt);
        obj->setProperty ("suffix", juce::String (localSuffix));
        obj->setProperty ("seconds", (double) request.seconds);
        obj->setProperty ("steps", request.steps);
        obj->setProperty ("cfg", 1.0 + (double) request.strength * 2.0);
        obj->setProperty ("seeds", seedList);

        owner.setStatus (Status::working, tr ("Mac'inde üretiliyor… (") + juce::String ((int) request.seeds.size())
                                              + tr (" varyasyon)"));

        juce::MemoryBlock response;
        int statusCode = 0;

        if (! httpPost (engineUrl ("/generate"), juce::JSON::toString (body, true),
                        "Content-Type: application/json", 600000, response, statusCode))
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
        auto englishPrompt = request.prompt;

        if (owner.isEngineOnline())
        {
            owner.setStatus (Status::working, tr ("Tarif hazırlanıyor…"));

            auto* t = new juce::DynamicObject();
            juce::var tBody (t);
            t->setProperty ("text", request.prompt);

            juce::MemoryBlock tResponse;
            int tStatus = 0;

            if (httpPost (engineUrl ("/translate"), juce::JSON::toString (tBody, true),
                          "Content-Type: application/json", 60000, tResponse, tStatus) && tStatus == 200)
            {
                const auto translated = juce::JSON::parse (tResponse.toString()).getProperty ("text", {}).toString();

                if (translated.isNotEmpty())
                    englishPrompt = translated;
            }
        }

        const int count = (int) request.seeds.size();

        for (int i = 0; i < count; ++i)
        {
            if (threadShouldExit())
                return false;

            owner.setStatus (Status::working, tr ("Bulutta üretiliyor… (") + juce::String (i + 1) + "/" + juce::String (count) + ")");

            auto* obj = new juce::DynamicObject();
            juce::var body (obj);
            obj->setProperty ("text", englishPrompt + cloudSuffix);
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
        synth.addVoice (new juce::SamplerVoice());

    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    baseSeed = juce::Random::getSystemRandom().nextInt ({ 1000, 9999 });

    monitor = std::make_unique<EngineMonitor> (*this);
    monitor->startThread();
}

WidiKeyProcessor::~WidiKeyProcessor()
{
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
    layout.add (std::make_unique<Param> (juce::ParameterID { "attack", 1 },    "Attack",   timeRange (0.001f, 5.0f), 0.005f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "decay", 1 },     "Decay",    timeRange (0.001f, 5.0f), 0.4f,   Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "sustain", 1 },   "Sustain",  juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "release", 1 },   "Release",  timeRange (0.01f, 8.0f), 1.2f,    Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "cutoff", 1 },    "Kesim",    cutoffRange, 20000.0f,            Attr().withStringFromValueFunction (formatHz)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "resonance", 1 }, "Rezonans", juce::NormalisableRange<float> (0.5f, 5.0f), 0.707f, Attr().withStringFromValueFunction (formatPlain)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverb", 1 },    "Reverb",   juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "gain", 1 },      "Ses",      juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, Attr().withStringFromValueFunction (formatDb)));

    // Üretim ayarları
    layout.add (std::make_unique<Param> (juce::ParameterID { "length", 1 },   tr ("Süre"),  juce::NormalisableRange<float> (0.5f, 11.0f, 0.1f), 3.0f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "strength", 1 }, tr ("Tarife bağlılık"), juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<IntParam> (juce::ParameterID { "steps", 1 },      "Kalite",    4, 32, 8));
    layout.add (std::make_unique<IntParam> (juce::ParameterID { "variations", 1 }, "Varyasyon", 1, maxVariations, 4));
    return layout;
}

//==============================================================================
void WidiKeyProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);

    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };
    filter.prepare (spec);
    reverb.prepare (spec);
    filter.reset();
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

    const juce::ADSR::Parameters env { apvts.getRawParameterValue ("attack")->load(),
                                       apvts.getRawParameterValue ("decay")->load(),
                                       apvts.getRawParameterValue ("sustain")->load(),
                                       apvts.getRawParameterValue ("release")->load() };
    {
        const juce::ScopedTryLock sl (soundLock);

        if (sl.isLocked())
            if (auto* sampler = dynamic_cast<juce::SamplerSound*> (currentSound.get()))
                sampler->setEnvelopeParameters (env);
    }

    synth.renderNextBlock (buffer, midi, 0, numSamples);

    filter.setCutoffFrequency (apvts.getRawParameterValue ("cutoff")->load());
    filter.setResonance (apvts.getRawParameterValue ("resonance")->load());

    const float mix = apvts.getRawParameterValue ("reverb")->load();
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = 0.75f;
    rp.damping  = 0.4f;
    rp.width    = 1.0f;
    rp.wetLevel = mix * 0.6f;
    rp.dryLevel = 1.0f - mix * 0.4f;
    reverb.setParameters (rp);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);
    filter.process (context);
    reverb.process (context);

    buffer.applyGain (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load()));
}

//==============================================================================
void WidiKeyProcessor::startGeneration (const juce::String& prompt)
{
    if (generator != nullptr && generator->isThreadRunning())
        return;

    const auto cleaned = prompt.trim();

    if (cleaned.isEmpty())
    {
        setStatus (Status::error, tr ("Önce sesi tarif et."));
        return;
    }

    {
        const juce::ScopedLock sl (dataLock);
        lastPrompt = cleaned;
    }

    if (! seedLocked.load())
        rerollSeed();

    GenerateThread::Request request;
    request.prompt   = cleaned;
    request.engine   = engine.load();
    request.seconds  = apvts.getRawParameterValue ("length")->load();
    request.steps    = (int) apvts.getRawParameterValue ("steps")->load();
    request.strength = apvts.getRawParameterValue ("strength")->load();

    const int count = juce::jlimit (1, maxVariations, (int) apvts.getRawParameterValue ("variations")->load());

    for (int i = 0; i < count; ++i)
        request.seeds.push_back (baseSeed.load() + i * 1009);

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

bool WidiKeyProcessor::decodeVariation (Variation& variation, const juce::String& name, juce::String& error)
{
    auto reader = createReader (variation.bytes);

    if (reader == nullptr)
    {
        error = tr ("Gelen ses dosyası okunamadı.");
        return false;
    }

    juce::BigInteger allNotes;
    allNotes.setRange (0, 128, true);

    auto* sound = new juce::SamplerSound (name, *reader, allNotes, rootNote, 0.005, 1.0, 30.0);
    variation.sound = sound;
    variation.peaks.assign (160, 0.0f);
    variation.seconds = 0.0;

    if (auto* audio = sound->getAudioData())
    {
        const int length = audio->getNumSamples();
        variation.seconds = reader->sampleRate > 0 ? length / reader->sampleRate : 0.0;

        if (length > 0)
        {
            float maxPeak = 0.0f;
            const auto count = variation.peaks.size();

            for (size_t i = 0; i < count; ++i)
            {
                const int start = (int) ((i * (size_t) length) / count);
                const int end   = juce::jmin (length, juce::jmax (start + 1, (int) (((i + 1) * (size_t) length) / count)));
                float peak = 0.0f;

                for (int ch = 0; ch < audio->getNumChannels(); ++ch)
                    peak = juce::jmax (peak, audio->getMagnitude (ch, start, end - start));

                variation.peaks[i] = peak;
                maxPeak = juce::jmax (maxPeak, peak);
            }

            if (maxPeak > 0.0f)
                for (auto& p : variation.peaks)
                    p /= maxPeak;
        }
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

    applySelected();
}

void WidiKeyProcessor::selectVariation (int index)
{
    {
        const juce::ScopedLock sl (dataLock);

        if (index < 0 || index >= (int) variations.size() || index == selected)
            return;

        selected = index;
    }

    applySelected();
}

void WidiKeyProcessor::applySelected()
{
    juce::SynthesiserSound::Ptr sound;

    {
        const juce::ScopedLock sl (dataLock);

        if (selected >= 0 && selected < (int) variations.size())
            sound = variations[(size_t) selected].sound;
    }

    {
        const juce::ScopedLock sl (soundLock);
        currentSound = sound;
    }

    synth.clearSounds();

    if (sound != nullptr)
        synth.addSound (sound);

    ++sampleVersion;
}

std::vector<WidiKeyProcessor::VariationInfo> WidiKeyProcessor::getVariationInfo() const
{
    const juce::ScopedLock sl (dataLock);
    std::vector<VariationInfo> info;

    for (const auto& v : variations)
        info.push_back ({ v.peaks, v.seconds, v.seed });

    return info;
}

int WidiKeyProcessor::getSelectedVariation() const { const juce::ScopedLock sl (dataLock); return selected; }

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
juce::String WidiKeyProcessor::getSampleName() const    { const juce::ScopedLock sl (dataLock); return sampleName; }

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
        apvts.replaceState (params);

    engine = root["engine"].toString() == "cloud" ? Engine::cloud : Engine::local;
    seedLocked = (bool) root.getProperty ("seedLocked", false);
    baseSeed = (int) root.getProperty ("baseSeed", baseSeed.load());

    const auto name = root["name"].toString();

    {
        const juce::ScopedLock sl (dataLock);
        lastPrompt = root["prompt"].toString();
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

        if (decodeVariation (v, name, error))
            restored.push_back (std::move (v));
    }

    if (! restored.empty())
    {
        const int wanted = (int) root.getProperty ("selected", 0);
        setVariations (std::move (restored), name);
        selectVariation (wanted);
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
