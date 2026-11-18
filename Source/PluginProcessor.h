#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

class WidiKeyProcessor : public juce::AudioProcessor
{
public:
    enum class Status { idle, working, ok, error };
    enum class Engine { cloud, local };

    struct VariationInfo
    {
        std::vector<float> peaks;
        double seconds = 0.0;
        int seed = -1;
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
    void startGeneration (const juce::String& prompt);
    Status getStatus() const { return status.load(); }
    juce::String getStatusMessage() const;
    juce::String getPrompt() const;
    juce::String getSampleName() const;

    // --- Varyasyonlar ---
    std::vector<VariationInfo> getVariationInfo() const;
    int getSelectedVariation() const;
    void selectVariation (int index);
    int getSampleVersion() const { return sampleVersion.load(); }

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
        juce::SynthesiserSound::Ptr sound;
        std::vector<float> peaks;
        double seconds = 0.0;
        int seed = -1;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::MemoryBlock& bytes);
    bool decodeVariation (Variation& variation, const juce::String& name, juce::String& error);
    void setVariations (std::vector<Variation>&& newVariations, const juce::String& name);
    void applySelected();
    void setStatus (Status newStatus, const juce::String& message);

    juce::Synthesiser synth;
    juce::CriticalSection soundLock;
    juce::SynthesiserSound::Ptr currentSound;

    juce::AudioFormatManager formatManager;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::Reverb reverb;

    mutable juce::CriticalSection dataLock;
    std::vector<Variation> variations;
    int selected = 0;
    juce::String sampleName, lastPrompt, statusMessage;

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
