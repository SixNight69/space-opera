#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// SpaceOpera: a cold, huge digital-space reverb.
//   8-line feedback delay network (Householder matrix) + input diffusion + slow modulation,
//   optional +12 st shimmer in the loop, infinite (freeze) decay, self-ducking, wet low cut.
class SpaceOperaProcessor : public juce::AudioProcessor
{
public:
    SpaceOperaProcessor();
    ~SpaceOperaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    static constexpr float infiniteDecay = 20.5f;   // decay knob value at/above this = freeze
    static const juce::StringArray& noteNames();

    // live levels for the GUI animation (0..1-ish)
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> wetLevel   { 0.0f };
    std::atomic<float> duckAmount { 0.0f };

private:
    struct Delay
    {
        std::vector<float> buf;
        int mask = 0, w = 0;
        void prepare (int maxSamples)
        {
            const int size = juce::nextPowerOfTwo (maxSamples + 4);
            buf.assign ((size_t) size, 0.0f);
            mask = size - 1;
            w = 0;
        }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
        float read (float d) const noexcept          // d >= 1, linear interpolation
        {
            const float rp = (float) w - d;
            const int i0 = (int) std::floor (rp);
            const float f = rp - (float) i0;
            const float a = buf[(size_t) (i0 & mask)];
            const float b = buf[(size_t) ((i0 + 1) & mask)];
            return a + f * (b - a);
        }
        float readInt (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
    };

    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;

    std::atomic<float>* pPre = nullptr;
    std::atomic<float>* pSync = nullptr;
    std::atomic<float>* pNote = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pCold = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pDuck = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pShimmer = nullptr;

    double sr = 44100.0;

    Delay preDelay[2];
    Delay diff[2][numDiffusers];
    Delay lines[numLines];
    Delay shimmerBuf;

    float lineLen[numLines] {};        // smoothed lengths (samples)
    float damp[numLines] {};           // one-pole low-pass state per line
    float lfoPhase[numLines] {};
    float lfoInc[numLines] {};
    float preSmoothed = 0.0f;
    float inGain = 1.0f;               // fades the input out when DECAY is at infinity (freeze)
    float shimmerPhase = 0.0f, shimmerWindow = 2000.0f, shimmerSmoothed = 0.0f, shimmerLp = 0.0f, shimmerLpA = 0.5f, shimmerHp = 0.0f, shimmerHpA = 0.02f;

    float duckEnv = 0.0f, duckAtt = 0.9f, duckRel = 0.999f;
    float levelEnvIn = 0.0f, levelEnvWet = 0.0f;

    juce::dsp::StateVariableTPTFilter<float> wetLowCut, wetHighCut;
    juce::SmoothedValue<float> mixSmooth, widthSmooth;

    int silentBlocks = 1000;
    int silentSamples = 1 << 30;
    bool tankAwake = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceOperaProcessor)
};
