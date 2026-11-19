#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "WidiSampler.h"

//==============================================================================
// Tarif geçmişi ve favoriler (tüm projeler arasında ortak)
namespace PromptStore
{
    juce::StringArray getHistory();
    juce::StringArray getFavorites();
    void addToHistory (const juce::String& prompt);
    bool isFavorite (const juce::String& prompt);
    void toggleFavorite (const juce::String& prompt);
    void clearHistory();
}

//==============================================================================
class WidiKeyProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    enum class Status { idle, working, ok, error };
    enum class Engine { cloud, local };

    struct VariationInfo
    {
        std::vector<float> peaks;
        double seconds = 0.0;
        int seed = -1;
        bool pitched = false;
        double root = 60.0;
    };

    static constexpr int maxVariations = 4;
    static constexpr int enginePort = 7865;

    WidiKeyProcessor();
    ~WidiKeyProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "widi-key"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Üretim ---
    void startGeneration (const juce::String& prompt, const juce::String& negative, bool similarToSelected);
    Status getStatus() const { return status.load(); }
    juce::String getStatusMessage() const;
    juce::String getPrompt() const;
    juce::String getNegative() const;
    juce::String getSampleName() const;
    bool hasSample() const;

    // --- Varyasyonlar ---
    std::vector<VariationInfo> getVariationInfo() const;
    int getSelectedVariation() const;
    void selectVariation (int index);
    int getSampleVersion() const { return sampleVersion.load(); }
    bool isDrumMode() const;
    bool isMultiMode() const;
    void previewVariation (int index);

    // --- Dışa aktarma ve kütüphane ---
    juce::File exportSelectedToWav();
    bool saveSelectedToLibrary (const juce::String& name);
    bool loadFromLibrary (const juce::File& file);
    static juce::File getLibraryFolder();
    static juce::File getExportFolder();
    static juce::Array<juce::File> listLibrary();

    // --- Motor ve seed ---
    Engine getEngine() const { return engine.load(); }
    void setEngine (Engine e) { engine = e; }
    bool isEngineOnline() const { return engineOnline.load(); }
    bool isSeedLocked() const { return seedLocked.load(); }
    void setSeedLocked (bool shouldLock) { seedLocked = shouldLock; }
    int getBaseSeed() const { return baseSeed.load(); }
    void rerollSeed();

    static juce::String loadApiKey();
    static void saveApiKey (const juce::String& key);

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

private:
    class GenerateThread;
    class EngineMonitor;

    struct Variation
    {
        juce::MemoryBlock bytes;
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        double sourceRate = 44100.0;
        std::vector<float> peaks;
        double seconds = 0.0;
        int seed = -1;
        widi::PitchResult pitch;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::MemoryBlock& bytes);
    bool decodeVariation (Variation& variation, juce::String& error);
    void setVariations (std::vector<Variation>&& newVariations, const juce::String& name);
    void rebuildSounds();
    void setStatus (Status newStatus, const juce::String& message);
    juce::MemoryBlock selectedAsWav() const;
    static juce::MemoryBlock bufferToWav (const juce::AudioBuffer<float>& audio, double rate);

    juce::Synthesiser synth;
    widi::VoiceSettings voiceSettings;

    juce::AudioFormatManager formatManager;
    juce::dsp::Reverb reverb;

    mutable juce::CriticalSection dataLock;
    std::vector<Variation> variations;
    int selected = 0;
    juce::String sampleName, lastPrompt, lastNegative, statusMessage;

    std::atomic<Status> status { Status::idle };
    std::atomic<Engine> engine { Engine::local };
    std::atomic<bool> engineOnline { false };
    std::atomic<bool> seedLocked { false };
    std::atomic<int> baseSeed { 4821 };
    std::atomic<int> sampleVersion { 0 };

    std::unique_ptr<GenerateThread> generator;
    std::unique_ptr<EngineMonitor> monitor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WidiKeyProcessor)
};
