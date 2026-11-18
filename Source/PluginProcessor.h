#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

class WidiKeyProcessor : public juce::AudioProcessor
{
public:
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

    // --- Yapay zeka ile ses üretimi ---
    enum class Status { idle, working, ok, error };

    void startGeneration (const juce::String& prompt);
    Status getStatus() const { return status.load(); }
    juce::String getStatusMessage() const;
    juce::String getSampleName() const;
    juce::String getPrompt() const;
    std::vector<float> getPeaks() const;
    double getSampleLengthSeconds() const;
    int getSampleVersion() const { return sampleVersion.load(); }

    static juce::String loadApiKey();
    static void saveApiKey (const juce::String& key);

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

private:
    class GenerateThread;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    bool loadSampleFromBytes (const juce::MemoryBlock& bytes, const juce::String& name, juce::String& error);
    void setStatus (Status newStatus, const juce::String& message);

    juce::Synthesiser synth;
    juce::AudioFormatManager formatManager;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::Reverb reverb;

    mutable juce::CriticalSection dataLock;
    juce::MemoryBlock sampleBytes;
    juce::String sampleName, lastPrompt, statusMessage;
    std::vector<float> peaks;
    double sampleSeconds = 0.0;

    std::atomic<Status> status { Status::idle };
    std::atomic<int> sampleVersion { 0 };
    std::unique_ptr<GenerateThread> generator;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WidiKeyProcessor)
};
