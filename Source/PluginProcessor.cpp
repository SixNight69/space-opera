#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    // FDN line lengths (ms) at SIZE = 0 scale 1.0 — mutually prime-ish
    constexpr float lineMs[8]  = { 31.3f, 37.9f, 41.7f, 47.3f, 53.9f, 61.1f, 67.3f, 73.7f };
    constexpr float lfoHz[8]   = { 0.13f, 0.17f, 0.21f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f };
    constexpr float diffMs[4]  = { 4.77f, 3.59f, 12.73f, 9.31f };
    constexpr float diffGain   = 0.62f;
    constexpr double noteBeats[] = { 0.0625, 0.125, 0.25, 0.5, 1.0 };
    constexpr float wetScale   = 1.4f;
    constexpr int   chunk      = 16;

    inline float softClip (float x) noexcept     // ~tanh, cheap; keeps the tank bounded
    {
        x = jlimit (-3.0f, 3.0f, x);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }
    inline float safety (float x) noexcept       // soft knee above 0.7 so the wet can never clip hard
    {
        const float a = std::abs (x);
        if (a <= 0.7f) return x;
        return std::copysign (0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f), x);
    }
    inline float dbToGain (float db) noexcept { return std::exp (db * 0.115129255f); }
    inline float gainToDb (float g) noexcept  { return 8.68588964f * std::log (g + 1.0e-9f); }
}

const StringArray& SpaceOperaProcessor::noteNames()
{
    static const StringArray n { "1/64", "1/32", "1/16", "1/8", "1/4" };
    return n;
}

SpaceOperaProcessor::SpaceOperaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpaceOperaState", createLayout())
{
    pPre     = apvts.getRawParameterValue ("predelay");
    pSync    = apvts.getRawParameterValue ("presync");
    pNote    = apvts.getRawParameterValue ("prenote");
    pDecay   = apvts.getRawParameterValue ("decay");
    pSize    = apvts.getRawParameterValue ("size");
    pCold    = apvts.getRawParameterValue ("cold");
    pWidth   = apvts.getRawParameterValue ("width");
    pDuck    = apvts.getRawParameterValue ("duck");
    pMix     = apvts.getRawParameterValue ("mix");
    pShimmer = apvts.getRawParameterValue ("shimmer");
}

AudioProcessorValueTreeState::ParameterLayout SpaceOperaProcessor::createLayout()
{
    using P = AudioParameterFloat;
    using A = AudioParameterFloatAttributes;
    auto pct = [] (float v, int) { return String (roundToInt (v)) + "%"; };

    AudioProcessorValueTreeState::ParameterLayout l;

    NormalisableRange<float> preR (0.0f, 500.0f, 0.1f);
    preR.setSkewForCentre (80.0f);
    l.add (std::make_unique<P> (ParameterID { "predelay", 1 }, "Pre-Delay", preR, 20.0f,
                                A().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "presync", 1 }, "Pre-Delay Sync", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "prenote", 1 }, "Pre-Delay Note", noteNames(), 2));

    NormalisableRange<float> decR (0.3f, 21.0f, 0.01f);
    decR.setSkewForCentre (3.5f);
    l.add (std::make_unique<P> (ParameterID { "decay", 1 }, "Decay", decR, 4.0f,
                                A().withStringFromValueFunction ([] (float v, int)
                                { return v >= infiniteDecay ? String (CharPointer_UTF8 ("\xe2\x88\x9e")) : String (v, 1) + " s"; })));

    l.add (std::make_unique<P> (ParameterID { "size", 1 },  "Size",  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f,  A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<P> (ParameterID { "cold", 1 },  "Cold",  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f,  A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<P> (ParameterID { "width", 1 }, "Width", NormalisableRange<float> (0.0f, 150.0f, 0.1f), 100.0f, A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<P> (ParameterID { "duck", 1 },  "Duck",  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,   A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<P> (ParameterID { "mix", 1 },   "Mix",   NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,  A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "shimmer", 1 }, "Shimmer", false));
    return l;
}

bool SpaceOperaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SpaceOperaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const auto ms = [this] (float m) { return (int) std::ceil (m * 0.001 * sr); };

    for (auto& d : preDelay) d.prepare ((int) (2.1 * sr));
    for (auto& ch : diff)
        for (int k = 0; k < numDiffusers; ++k) ch[k].prepare (ms (diffMs[k] * 1.3f) + 2);
    for (int i = 0; i < numLines; ++i)
    {
        lines[i].prepare (ms (lineMs[i] * 2.3f + 4.0f));
        lineLen[i] = lineMs[i] * 0.001f * (float) sr;
        damp[i] = 0.0f;
        lfoPhase[i] = (float) i / numLines;
        lfoInc[i] = lfoHz[i] * (float) chunk / (float) sr;
    }
    shimmerWindow = (float) (0.09 * sr);
    shimmerBuf.prepare ((int) (shimmerWindow * 2.0f) + 8);
    shimmerPhase = 0.0f;
    shimmerSmoothed = 0.0f;
    shimmerLp = shimmerHp = 0.0f;
    shimmerHpA = (float) (1.0 - std::exp (-MathConstants<double>::twoPi * 200.0 / sr));
    shimmerLpA = (float) (1.0 - std::exp (-MathConstants<double>::twoPi * 5000.0 / sr));
    preSmoothed = (float) (0.02 * sr);

    dsp::ProcessSpec spec { sampleRate, (uint32) samplesPerBlock, 2 };
    wetLowCut.prepare (spec);  wetLowCut.setType (dsp::StateVariableTPTFilterType::highpass);  wetLowCut.setCutoffFrequency (180.0f);
    wetHighCut.prepare (spec); wetHighCut.setType (dsp::StateVariableTPTFilterType::lowpass);  wetHighCut.setCutoffFrequency (12000.0f);
    wetLowCut.reset(); wetHighCut.reset();

    mixSmooth.reset (sampleRate, 0.05);   mixSmooth.setCurrentAndTargetValue (pMix->load() / 100.0f);
    widthSmooth.reset (sampleRate, 0.05); widthSmooth.setCurrentAndTargetValue (pWidth->load() / 100.0f);

    duckAtt = (float) std::exp (-1.0 / (0.005 * sr));
    duckRel = (float) std::exp (-1.0 / (0.15 * sr));
    duckEnv = levelEnvIn = levelEnvWet = 0.0f;
    silentBlocks = 1000;
    silentSamples = 1 << 30;
    tankAwake = false;
}

void SpaceOperaProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, buffer.getNumSamples());
    const int n = buffer.getNumSamples();
    const int numCh = jmin (numIn, 2);
    if (numCh == 0 || n == 0) return;
    const bool stereo = numCh > 1;
    float* L = buffer.getWritePointer (0);
    float* R = stereo ? buffer.getWritePointer (1) : nullptr;

    // ---- parameters ----
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = jlimit (30.0, 400.0, *b);

    const float decay  = pDecay->load();
    const bool  freeze = decay >= infiniteDecay;
    const float size   = pSize->load() / 100.0f;
    const float cold   = pCold->load() / 100.0f;
    const float duck   = pDuck->load() / 100.0f;
    const bool  shimmerOn = pShimmer->load() > 0.5f;
    mixSmooth.setTargetValue (pMix->load() / 100.0f);
    widthSmooth.setTargetValue (pWidth->load() / 100.0f);

    // input peak for idle detection / GUI
    float inPeak = buffer.getMagnitude (0, 0, n);
    if (stereo) inPeak = jmax (inPeak, buffer.getMagnitude (1, 0, n));

    // ---- idle: nothing to add -> leave the dry signal untouched (near-zero CPU) ----
    const bool mixOff = mixSmooth.getTargetValue() <= 0.0f && ! mixSmooth.isSmoothing();
    if (mixOff || (! freeze && inPeak < 1.0e-6f && silentBlocks > 0 && silentSamples > (int) (preSmoothed + 0.3 * sr)))
    {
        tankAwake = false;
        levelEnvIn = levelEnvWet = 0.0f;
        inputLevel.store (0.0f); wetLevel.store (0.0f); duckAmount.store (0.0f);
        if (mixOff) silentBlocks = 1000;
        return;
    }
    if (! tankAwake)
    {
        tankAwake = true;
        silentBlocks = 0;
        silentSamples = 0;
    }

    float preTarget;
    if (pSync->load() > 0.5f)
        preTarget = (float) (noteBeats[jlimit (0, 4, (int) pNote->load())] * 60.0 / bpm * sr);
    else
        preTarget = pPre->load() * 0.001f * (float) sr;
    preTarget = jlimit (1.0f, (float) preDelay[0].mask - 8.0f, preTarget + 1.0f);

    const float sizeScale = 0.4f + 1.8f * size;
    float lenTarget[numLines], g[numLines];
    for (int i = 0; i < numLines; ++i)
    {
        lenTarget[i] = lineMs[i] * 0.001f * sizeScale * (float) sr;
        // damping shortens the audible tail, so stretch the target a little to land near the knob value
        const float decayEff = decay * (1.1f + 0.03f * decay);
        g[i] = freeze ? 1.0f : std::pow (10.0f, -3.0f * lenTarget[i] / (jmax (0.3f, decayEff) * (float) sr));
    }
    const float dampHz = 2500.0f * std::pow (16000.0f / 2500.0f, cold);
    const float dampA  = freeze ? 1.0f : (float) (1.0 - std::exp (-MathConstants<double>::twoPi * dampHz / sr));
    wetHighCut.setCutoffFrequency (6000.0f * std::pow (20000.0f / 6000.0f, cold) * 0.95f);
    const float modDepth = (0.25f + 1.0f * size) * 0.001f * (float) sr;
    const float diffScale = 0.6f + 0.6f * size;
    int diffLen[numDiffusers];
    for (int k = 0; k < numDiffusers; ++k)
        diffLen[k] = jmax (1, (int) (diffMs[k] * diffScale * 0.001f * (float) sr));

    const float shimmerTarget = (shimmerOn && ! freeze) ? 0.5f : 0.0f;
    const float inGainTarget  = freeze ? 0.0f : 1.0f;
    const float glide   = (float) std::exp (-1.0 / (0.05 * sr));   // pre-delay / size glide
    const float smooth5 = (float) std::exp (-1.0 / (0.02 * sr));
    const float shimInc = (1.0f - 2.0f) / shimmerWindow;           // +12 semitones
    float wetPeak = 0.0f, tankPeak = 0.0f;

    for (int start = 0; start < n; start += chunk)
    {
        const int len = jmin (chunk, n - start);

        // slow modulation, updated per chunk
        float mod[numLines];
        for (int i = 0; i < numLines; ++i)
        {
            lfoPhase[i] += lfoInc[i];
            if (lfoPhase[i] >= 1.0f) lfoPhase[i] -= 1.0f;
            mod[i] = modDepth * (0.5f + 0.5f * std::sin (MathConstants<float>::twoPi * lfoPhase[i]));
        }

        for (int k = 0; k < len; ++k)
        {
            const int s = start + k;
            const float dryL = L[s];
            const float dryR = stereo ? R[s] : dryL;

            // ducking envelope (driven by the dry input)
            const float lvl = jmax (std::abs (dryL), std::abs (dryR));
            duckEnv = lvl + (lvl > duckEnv ? duckAtt : duckRel) * (duckEnv - lvl);

            // pre-delay
            preSmoothed = preTarget + glide * (preSmoothed - preTarget);
            preDelay[0].push (dryL);
            preDelay[1].push (dryR);
            float xl = preDelay[0].read (preSmoothed);
            float xr = preDelay[1].read (preSmoothed);

            // input diffusion (Schroeder all-passes)
            for (int d = 0; d < numDiffusers; ++d)
            {
                const float dl = diff[0][d].readInt (diffLen[d]);
                const float vl = xl + diffGain * dl;
                diff[0][d].push (vl);
                xl = dl - diffGain * vl;
                const float dr = diff[1][d].readInt (diffLen[d]);
                const float vr = xr + diffGain * dr;
                diff[1][d].push (vr);
                xr = dr - diffGain * vr;
            }

            inGain = inGainTarget + smooth5 * (inGain - inGainTarget);
            xl *= inGain;
            xr *= inGain;

            // FDN read + damping
            float y[numLines];
            float sum = 0.0f;
            for (int i = 0; i < numLines; ++i)
            {
                lineLen[i] = lenTarget[i] + glide * (lineLen[i] - lenTarget[i]);
                const float v = lines[i].read (lineLen[i] + mod[i] + 1.0f);
                damp[i] += dampA * (v - damp[i]);
                y[i] = damp[i];
                sum += y[i];
                tankPeak = jmax (tankPeak, std::abs (y[i]));
            }

            const float outL = 0.5f * (y[0] - y[2] + y[4] - y[6]);
            const float outR = 0.5f * (y[1] - y[3] + y[5] - y[7]);

            // shimmer: +12 st pitch shift of the tank, fed back in
            shimmerSmoothed = shimmerTarget + smooth5 * (shimmerSmoothed - shimmerTarget);
            float sh = 0.0f;
            shimmerBuf.push (0.5f * (outL + outR));
            if (shimmerSmoothed > 1.0e-4f)
            {
                shimmerPhase += shimInc;
                if (shimmerPhase < 0.0f) shimmerPhase += 1.0f;
                float p2 = shimmerPhase + 0.5f;
                if (p2 >= 1.0f) p2 -= 1.0f;
                const float s1 = std::sin (MathConstants<float>::pi * shimmerPhase);
                const float g1 = s1 * s1;
                sh = (shimmerBuf.read (1.0f + shimmerPhase * shimmerWindow) * g1
                      + shimmerBuf.read (1.0f + p2 * shimmerWindow) * (1.0f - g1)) * shimmerSmoothed;
                // band-limit the octave cascade: 200 Hz high-pass (no DC latch-up) + 5 kHz low-pass
                shimmerHp += shimmerHpA * (sh - shimmerHp);
                sh -= shimmerHp;
                shimmerLp += shimmerLpA * (sh - shimmerLp);
                sh = shimmerLp;
            }

            // Householder feedback + injection
            const float h = 0.25f * sum;   // 2/N
            const float injL = 0.5f * (xl + sh), injR = 0.5f * (xr + sh);
            for (int i = 0; i < numLines; ++i)
            {
                const float inj = ((i & 1) ? injR : injL) * (((i >> 1) & 1) ? -1.0f : 1.0f);
                lines[i].push (softClip ((y[i] - h) * g[i] + inj));
            }

            // wet path: low cut (keeps 808/sub clean), high cut (cold), width, ducking
            float wl = wetLowCut.processSample (0, outL);
            float wr = wetLowCut.processSample (1, outR);
            wl = wetHighCut.processSample (0, wl);
            wr = wetHighCut.processSample (1, wr);

            const float w = widthSmooth.getNextValue();
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
            wl = mid + side;
            wr = mid - side;

            const float duckDb = -24.0f * duck * jlimit (0.0f, 1.0f, (gainToDb (duckEnv) + 40.0f) / 25.0f);
            const float dg = dbToGain (duckDb) * wetScale;
            wl = safety (wl * dg);
            wr = safety (wr * dg);
            wetPeak = jmax (wetPeak, std::abs (wl), std::abs (wr));

            const float m = mixSmooth.getNextValue();
            const float dryG = std::cos (m * MathConstants<float>::halfPi);
            const float wetG = std::sin (m * MathConstants<float>::halfPi);
            L[s] = dryL * dryG + wl * wetG;
            if (stereo) R[s] = dryR * dryG + wr * wetG;
        }
    }

    wetLowCut.snapToZero();
    wetHighCut.snapToZero();

    // GUI levels (smoothed per block)
    const float a = 0.6f;
    levelEnvIn  = jmax (inPeak,  levelEnvIn * a);
    levelEnvWet = jmax (wetPeak, levelEnvWet * a);
    inputLevel.store (levelEnvIn);
    wetLevel.store (levelEnvWet);
    duckAmount.store (duck * jlimit (0.0f, 1.0f, (gainToDb (duckEnv) + 40.0f) / 25.0f));

    // idle only once the input is silent long enough for the tank to have spoken, and the tank is empty
    if (inPeak < 1.0e-6f) silentSamples += n;
    else silentSamples = 0;
    silentBlocks = (inPeak < 1.0e-6f && tankPeak < 1.0e-5f && wetPeak < 1.0e-5f) ? silentBlocks + 1 : 0;
}

void SpaceOperaProcessor::getStateInformation (MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpaceOperaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessorEditor* SpaceOperaProcessor::createEditor()
{
    return new SpaceOperaEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpaceOperaProcessor();
}
