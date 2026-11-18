#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Common.h"

using widi::tr;

namespace
{
    constexpr int rootNote = 60; // Logic'te C3

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "widi-key";
        o.filenameSuffix      = ".settings";
        o.folderName          = "widi-key";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    juce::String formatSeconds (float v, int)
    {
        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                        : juce::String (v, 2) + " s";
    }

    juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    juce::String formatHz (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                            : juce::String (juce::roundToInt (v)) + " Hz";
    }

    juce::String formatDb (float v, int) { return juce::String (v, 1) + " dB"; }
}

//==============================================================================
// Arka planda ElevenLabs'a istek atan iş parçacığı
class WidiKeyProcessor::GenerateThread : public juce::Thread
{
public:
    GenerateThread (WidiKeyProcessor& o, juce::String p, float secs)
        : juce::Thread ("widi-key generator"), owner (o), prompt (std::move (p)), seconds (secs) {}

    void run() override
    {
        const auto key = WidiKeyProcessor::loadApiKey();

        if (key.isEmpty())
        {
            owner.setStatus (Status::error, tr ("Önce Ayarlar'dan ElevenLabs API anahtarını gir."));
            return;
        }

        auto* obj = new juce::DynamicObject();
        juce::var body (obj);
        obj->setProperty ("text", prompt + ", single isolated note, one shot, clean, no background noise");
        obj->setProperty ("duration_seconds", (double) seconds);
        obj->setProperty ("prompt_influence", 0.6);

        const auto json = juce::JSON::toString (body, true);
        const auto url = juce::URL ("https://api.elevenlabs.io/v1/sound-generation").withPOSTData (json);

        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                           .withExtraHeaders ("xi-api-key: " + key
                                              + "\r\nContent-Type: application/json"
                                              + "\r\nAccept: audio/mpeg")
                           .withConnectionTimeoutMs (90000)
                           .withStatusCode (&statusCode);

        auto stream = url.createInputStream (options);

        if (stream == nullptr)
        {
            owner.setStatus (Status::error, tr ("Sunucuya bağlanılamadı. İnternet bağlantını kontrol et."));
            return;
        }

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock (data);

        if (threadShouldExit())
            return;

        if (statusCode != 200)
        {
            owner.setStatus (Status::error, describeError (statusCode, data.toString()));
            return;
        }

        juce::String error;

        if (owner.loadSampleFromBytes (data, prompt, error))
            owner.setStatus (Status::ok, tr ("Hazır · klavyeden çalabilirsin"));
        else
            owner.setStatus (Status::error, error);
    }

private:
    static juce::String describeError (int code, const juce::String& responseText)
    {
        if (code == 401)
            return tr ("API anahtarı geçersiz. Ayarlar'dan kontrol et.");

        if (code == 429)
            return tr ("Çok fazla istek ya da kredin bitti. Biraz sonra tekrar dene.");

        juce::String message;
        const auto parsed = juce::JSON::parse (responseText);

        if (auto* detail = parsed.getProperty ("detail", {}).getDynamicObject())
            message = detail->getProperty ("message").toString();

        if (message.isEmpty())
            message = responseText.substring (0, 160);

        return tr ("Hata (") + juce::String (code) + "): " + message;
    }

    WidiKeyProcessor& owner;
    juce::String prompt;
    float seconds;
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
    filter.setResonance (0.7f);
}

WidiKeyProcessor::~WidiKeyProcessor()
{
    if (generator != nullptr)
        generator->stopThread (4000);
}

juce::AudioProcessorValueTreeState::ParameterLayout WidiKeyProcessor::createLayout()
{
    using Param = juce::AudioParameterFloat;
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
    layout.add (std::make_unique<Param> (juce::ParameterID { "attack", 1 },  "Attack",  timeRange (0.001f, 5.0f), 0.005f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "decay", 1 },   "Decay",   timeRange (0.001f, 5.0f), 0.4f,   Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "sustain", 1 }, "Sustain", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "release", 1 }, "Release", timeRange (0.01f, 8.0f), 1.2f,    Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "cutoff", 1 },  "Filtre",  cutoffRange, 20000.0f,            Attr().withStringFromValueFunction (formatHz)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "reverb", 1 },  "Reverb",  juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, Attr().withStringFromValueFunction (formatPercent)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "length", 1 },  tr ("Süre"), juce::NormalisableRange<float> (0.5f, 10.0f, 0.1f), 3.0f, Attr().withStringFromValueFunction (formatSeconds)));
    layout.add (std::make_unique<Param> (juce::ParameterID { "gain", 1 },    "Ses",     juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, Attr().withStringFromValueFunction (formatDb)));
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

    // Plugin'in kendi ekran klavyesinden gelen notalar
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
        setStatus (Status::error, tr ("Önce bir ses tarif et."));
        return;
    }

    {
        const juce::ScopedLock sl (dataLock);
        lastPrompt = cleaned;
    }

    setStatus (Status::working, tr ("Ses üretiliyor… (birkaç saniye sürer)"));

    generator.reset();
    generator = std::make_unique<GenerateThread> (*this, cleaned, apvts.getRawParameterValue ("length")->load());
    generator->startThread();
}

bool WidiKeyProcessor::loadSampleFromBytes (const juce::MemoryBlock& bytes, const juce::String& name, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader;

   #if JUCE_USE_MP3AUDIOFORMAT
    {
        juce::MP3AudioFormat mp3;
        reader.reset (mp3.createReaderFor (new juce::MemoryInputStream (bytes, true), true));
    }
   #endif

    if (reader == nullptr)
        reader.reset (formatManager.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, true)));

    if (reader == nullptr)
    {
        error = tr ("Gelen ses dosyası okunamadı.");
        return false;
    }

    juce::BigInteger allNotes;
    allNotes.setRange (0, 128, true);

    auto* sound = new juce::SamplerSound (name, *reader, allNotes, rootNote, 0.005, 1.0, 30.0);

    std::vector<float> newPeaks (160, 0.0f);
    double seconds = 0.0;

    if (auto* audio = sound->getAudioData())
    {
        const int length = audio->getNumSamples();
        seconds = reader->sampleRate > 0 ? length / reader->sampleRate : 0.0;

        if (length > 0)
        {
            float maxPeak = 0.0f;
            const auto count = newPeaks.size();

            for (size_t i = 0; i < count; ++i)
            {
                const int start = (int) ((i * (size_t) length) / count);
                const int end   = juce::jmin (length, juce::jmax (start + 1, (int) (((i + 1) * (size_t) length) / count)));
                float peak = 0.0f;

                for (int ch = 0; ch < audio->getNumChannels(); ++ch)
                    peak = juce::jmax (peak, audio->getMagnitude (ch, start, end - start));

                newPeaks[i] = peak;
                maxPeak = juce::jmax (maxPeak, peak);
            }

            if (maxPeak > 0.0f)
                for (auto& p : newPeaks)
                    p /= maxPeak;
        }
    }

    juce::SynthesiserSound::Ptr soundPtr (sound);

    {
        const juce::ScopedLock sl (soundLock);
        currentSound = soundPtr;
    }

    synth.clearSounds();
    synth.addSound (soundPtr);

    {
        const juce::ScopedLock sl (dataLock);
        sampleBytes   = bytes;
        sampleName    = name;
        peaks         = std::move (newPeaks);
        sampleSeconds = seconds;
    }

    ++sampleVersion;
    return true;
}

void WidiKeyProcessor::setStatus (Status newStatus, const juce::String& message)
{
    {
        const juce::ScopedLock sl (dataLock);
        statusMessage = message;
    }
    status = newStatus;
}

juce::String WidiKeyProcessor::getStatusMessage() const { const juce::ScopedLock sl (dataLock); return statusMessage; }
juce::String WidiKeyProcessor::getSampleName() const    { const juce::ScopedLock sl (dataLock); return sampleName; }
juce::String WidiKeyProcessor::getPrompt() const        { const juce::ScopedLock sl (dataLock); return lastPrompt; }
std::vector<float> WidiKeyProcessor::getPeaks() const   { const juce::ScopedLock sl (dataLock); return peaks; }
double WidiKeyProcessor::getSampleLengthSeconds() const { const juce::ScopedLock sl (dataLock); return sampleSeconds; }

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
// Proje kaydedilince ayarlar, tarif ve üretilen ses de projeyle birlikte saklanır.
void WidiKeyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("WIDIKEY");
    root.appendChild (apvts.copyState(), nullptr);

    {
        const juce::ScopedLock sl (dataLock);
        root.setProperty ("prompt", lastPrompt, nullptr);
        root.setProperty ("name", sampleName, nullptr);

        if (sampleBytes.getSize() > 0)
            root.setProperty ("audio", sampleBytes.toBase64Encoding(), nullptr);
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

    {
        const juce::ScopedLock sl (dataLock);
        lastPrompt = root["prompt"].toString();
    }

    const auto encoded = root["audio"].toString();

    if (encoded.isNotEmpty())
    {
        juce::MemoryBlock audio;

        if (audio.fromBase64Encoding (encoded))
        {
            juce::String error;
            loadSampleFromBytes (audio, root["name"].toString(), error);
        }
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
