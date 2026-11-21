#pragma once

#include "PluginProcessor.h"

class SpaceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpaceLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    juce::Font labelFont (float size) const;

private:
    juce::Typeface::Ptr michroma;
};

class SpaceOperaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpaceOperaEditor (SpaceOperaProcessor&);
    ~SpaceOperaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Star { float angle, radius, depth, twinkle; };
    struct Spark { juce::Point<float> pos, vel; float life, maxLife, size; juce::Colour colour; };

    struct Knob
    {
        juce::Slider slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    float norm (const char* id) const;
    void drawScene (juce::Graphics&);
    void drawControls (juce::Graphics&);

    SpaceLookAndFeel lnf;                 // destroyed last
    SpaceOperaProcessor& proc;

    // 0 pre-delay(ms) 1 decay 2 size 3 cold 4 width 5 duck 6 mix ; + pre-delay note knob
    std::array<Knob, 7> knobs;
    Knob noteKnob;
    juce::ToggleButton shimmerButton, syncButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> shimmerAttach, syncAttach;

    juce::Image bg, astronaut, shuttle, logo;
    juce::Rectangle<float> bar;

    std::vector<Star> stars;
    std::vector<Spark> sparks;
    std::vector<std::pair<float, float>> frost;   // angle, length
    juce::Random rng;

    // smoothed visual state
    float animTime = 0.0f;
    float vPre = 0, vDecay = 0, vSize = 0, vCold = 0, vWidth = 0, vMix = 0, vShimmer = 0, vFreeze = 0;
    float vDuck = 0, vWet = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceOperaEditor)
};
