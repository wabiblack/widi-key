#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace widi
{
    //==========================================================================
    // Perde tespiti (YIN). Sesin gerçek notasını MIDI numarası olarak bulur.
    struct PitchResult
    {
        bool pitched = false;
        double midi = 60.0;   // kesirli olabilir (ör. 61.23)
    };

    inline PitchResult detectPitch (const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        PitchResult result;
        const int length = audio.getNumSamples();
        const int window = 2048;
        const int tauMin = juce::jmax (2, (int) (sampleRate / 1500.0));
        const int tauMax = juce::jmin (window - 1, (int) (sampleRate / 50.0));
        const int startAt = (int) (0.03 * sampleRate);
        const int hop = 1024;

        if (sampleRate <= 0 || length < startAt + window + tauMax + 1)
            return result;

        // Mono karışım
        std::vector<float> mono ((size_t) length, 0.0f);
        const int channels = audio.getNumChannels();

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* src = audio.getReadPointer (ch);

            for (int i = 0; i < length; ++i)
                mono[(size_t) i] += src[i] / (float) channels;
        }

        std::vector<float> diff ((size_t) tauMax + 1);
        std::vector<double> estimates;
        const int lastStart = juce::jmin (length - window - tauMax - 1, startAt + (int) (0.8 * sampleRate));

        for (int frame = startAt; frame <= lastStart && estimates.size() < 12; frame += hop)
        {
            const float* x = mono.data() + frame;

            double energy = 0.0;
            for (int j = 0; j < window; ++j)
                energy += x[j] * x[j];

            if (std::sqrt (energy / window) < 0.01)
                continue;

            diff[0] = 1.0f;
            double runningSum = 0.0;

            for (int tau = 1; tau <= tauMax; ++tau)
            {
                double d = 0.0;
                for (int j = 0; j < window; ++j)
                {
                    const float delta = x[j] - x[j + tau];
                    d += delta * delta;
                }

                runningSum += d;
                diff[(size_t) tau] = runningSum > 0.0 ? (float) (d * tau / runningSum) : 1.0f;
            }

            int best = -1;

            for (int tau = tauMin; tau < tauMax; ++tau)
            {
                if (diff[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 < tauMax && diff[(size_t) tau + 1] < diff[(size_t) tau])
                        ++tau;
                    best = tau;
                    break;
                }
            }

            if (best < 0)
                continue;

            // Parabolik iyileştirme
            double refined = best;
            if (best > 1 && best < tauMax)
            {
                const double a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
                const double denom = a - 2.0 * b + c;
                if (std::abs (denom) > 1e-9)
                    refined = best + 0.5 * (a - c) / denom;
            }

            estimates.push_back (sampleRate / refined);
        }

        if (estimates.size() < 2)
            return result;

        std::sort (estimates.begin(), estimates.end());
        const double f0 = estimates[estimates.size() / 2];

        if (f0 < 30.0 || f0 > 2000.0)
            return result;

        result.pitched = true;
        result.midi = 69.0 + 12.0 * std::log2 (f0 / 440.0);
        return result;
    }

    // Logic adlandırması: C3 = 60
    inline juce::String noteName (double midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = juce::roundToInt (midi);
        const int cents = juce::roundToInt ((midi - n) * 100.0);
        juce::String text = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2);

        if (cents != 0)
            text << (cents > 0 ? " +" : " ") << cents << "c";

        return text;
    }

    //==========================================================================
    // Davul modunda varyasyonların oturduğu nota sınıfları: C, D, E, F
    constexpr int drumNoteClasses[4] = { 0, 2, 4, 5 };

    struct VoiceSettings
    {
        juce::ADSR::Parameters amp;
        float velocityAmount = 0.3f;
        float glideSeconds = 0.0f;
        bool loop = false;
        float cutoff = 20000.0f;
        float resonance = 0.707f;
        float filterEnvOctaves = 0.0f;
        float filterEnvDecay = 0.3f;
        int lastNote = -1;
    };

    //==========================================================================
    class WidiSound : public juce::SynthesiserSound
    {
    public:
        WidiSound (std::shared_ptr<const juce::AudioBuffer<float>> audio, double rate, double rootMidi,
                   const juce::BigInteger& noteSet, bool fixed)
            : data (std::move (audio)), sourceRate (rate), root (rootMidi), notes (noteSet), fixedPitch (fixed)
        {
            const int length = data->getNumSamples();

            if (length > (int) (0.3 * sourceRate))
            {
                loopStart = (int) (length * 0.35);
                loopEnd   = (int) (length * 0.85);
                crossfade = juce::jmin ((int) (0.08 * sourceRate), (loopEnd - loopStart) / 3);
            }
        }

        bool appliesToNote (int midiNoteNumber) override { return notes[midiNoteNumber]; }
        bool appliesToChannel (int) override             { return true; }

        const juce::AudioBuffer<float>& getData() const { return *data; }
        double getSourceRate() const { return sourceRate; }
        double getRoot() const { return root; }
        bool isFixedPitch() const { return fixedPitch; }
        bool hasLoop() const { return loopEnd > loopStart && crossfade > 0; }
        int getLoopStart() const { return loopStart; }
        int getLoopEnd() const { return loopEnd; }
        int getCrossfade() const { return crossfade; }

    private:
        std::shared_ptr<const juce::AudioBuffer<float>> data;
        double sourceRate, root;
        juce::BigInteger notes;
        bool fixedPitch;
        int loopStart = 0, loopEnd = 0, crossfade = 0;
    };

    //==========================================================================
    class WidiVoice : public juce::SynthesiserVoice
    {
    public:
        explicit WidiVoice (VoiceSettings& s) : settings (s)
        {
            filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        }

        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<WidiSound*> (s) != nullptr; }

        void setCurrentPlaybackSampleRate (double newRate) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);

            if (newRate > 0)
            {
                filter.prepare ({ newRate, 512, 2 });
                adsr.setSampleRate (newRate);
            }
        }

        void startNote (int midiNote, float velocity, juce::SynthesiserSound* s, int) override
        {
            auto* sound = dynamic_cast<WidiSound*> (s);

            if (sound == nullptr)
                return;

            position = 0.0;
            elapsed = 0;
            gain = (1.0f - settings.velocityAmount) + settings.velocityAmount * velocity;

            targetSemis = sound->isFixedPitch() ? 0.0 : midiNote - sound->getRoot();
            currentSemis = targetSemis;
            glideStep = 0.0;

            const double rate = getSampleRate();

            if (! sound->isFixedPitch() && settings.glideSeconds > 0.001f && settings.lastNote >= 0 && rate > 0)
            {
                currentSemis = settings.lastNote - sound->getRoot();
                glideStep = (targetSemis - currentSemis) / (settings.glideSeconds * rate);
            }

            if (! sound->isFixedPitch())
                settings.lastNote = midiNote;

            adsr.setSampleRate (rate);
            adsr.setParameters (settings.amp);
            adsr.noteOn();
            filter.reset();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
                adsr.noteOff();
            else
            {
                adsr.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples) override
        {
            auto* sound = dynamic_cast<WidiSound*> (getCurrentlyPlayingSound().get());

            if (sound == nullptr || ! isVoiceActive())
                return;

            const auto& data = sound->getData();
            const int length = data.getNumSamples();
            const int srcChannels = data.getNumChannels();
            const double hostRate = getSampleRate();

            if (length < 2 || srcChannels == 0 || hostRate <= 0)
            {
                clearCurrentNote();
                return;
            }

            adsr.setParameters (settings.amp);

            const bool looping = settings.loop && ! sound->isFixedPitch() && sound->hasLoop();
            const int loopStart = sound->getLoopStart();
            const int loopEnd = sound->getLoopEnd();
            const int xfade = sound->getCrossfade();
            const int loopLength = loopEnd - loopStart;
            const double baseRatio = sound->getSourceRate() / hostRate;
            const bool useFilter = settings.cutoff < 19500.0f || settings.filterEnvOctaves > 0.01f;

            const float* left = data.getReadPointer (0);
            const float* right = data.getReadPointer (srcChannels > 1 ? 1 : 0);

            auto readAt = [&] (const float* src, int index) -> float
            {
                index = juce::jlimit (0, length - 1, index);

                if (looping && index >= loopEnd - xfade && index < loopEnd)
                {
                    const float w = (float) (index - (loopEnd - xfade)) / (float) xfade;
                    return src[index] * (1.0f - w) + src[index - loopLength] * w;
                }

                return src[index];
            };

            double ratio = baseRatio * std::pow (2.0, currentSemis / 12.0);

            for (int i = 0; i < numSamples; ++i)
            {
                if ((i & 31) == 0)
                {
                    if (glideStep != 0.0)
                        ratio = baseRatio * std::pow (2.0, currentSemis / 12.0);

                    if (useFilter)
                    {
                        const double t = elapsed / hostRate;
                        const double envelope = std::exp (-t / juce::jmax (0.01, (double) settings.filterEnvDecay));
                        const double cutoff = juce::jlimit (20.0, juce::jmin (20000.0, hostRate * 0.45),
                                                            settings.cutoff * std::pow (2.0, settings.filterEnvOctaves * envelope));
                        filter.setCutoffFrequency ((float) cutoff);
                        filter.setResonance (settings.resonance);
                    }
                }

                const int index = (int) position;
                const float frac = (float) (position - index);
                int next = index + 1;

                if (looping && next >= loopEnd)
                    next -= loopLength;

                float l = readAt (left, index) + frac * (readAt (left, next) - readAt (left, index));
                float r = readAt (right, index) + frac * (readAt (right, next) - readAt (right, index));

                const float env = adsr.getNextSample() * gain;
                l *= env;
                r *= env;

                if (useFilter)
                {
                    l = filter.processSample (0, l);
                    r = filter.processSample (1, r);
                }

                output.addSample (0, startSample + i, l);

                if (output.getNumChannels() > 1)
                    output.addSample (1, startSample + i, r);

                position += ratio;
                ++elapsed;

                if (glideStep != 0.0)
                {
                    currentSemis += glideStep;

                    if ((glideStep > 0 && currentSemis >= targetSemis) || (glideStep < 0 && currentSemis <= targetSemis))
                    {
                        currentSemis = targetSemis;
                        glideStep = 0.0;
                        ratio = baseRatio * std::pow (2.0, currentSemis / 12.0);
                    }
                }

                if (looping && position >= loopEnd)
                    position -= loopLength;

                if (position >= length - 1 || ! adsr.isActive())
                {
                    adsr.reset();
                    clearCurrentNote();
                    break;
                }
            }
        }

    private:
        VoiceSettings& settings;
        juce::ADSR adsr;
        juce::dsp::StateVariableTPTFilter<float> filter;
        double position = 0.0, currentSemis = 0.0, targetSemis = 0.0, glideStep = 0.0;
        juce::int64 elapsed = 0;
        float gain = 1.0f;
    };
}
