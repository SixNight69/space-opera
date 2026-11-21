#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    const Colour ice    { 0xff82d7ff };
    const Colour labelC { 0xffbcd2f0 };

    // anchor points inside the artwork (fractions of the image size)
    const Point<float> waistFrac { 0.36f, 0.53f };
    const Point<float> visorFrac { 0.32f, 0.133f };
    constexpr float     visorRFrac = 0.113f;
    const Point<float> hatchFrac { 0.788f, 0.64f };

    constexpr int W = 760, H = 540;
    constexpr float fps = 30.0f;

    float lerp (float a, float b, float t) { return a + (b - a) * t; }
}

//==============================================================================
SpaceLookAndFeel::SpaceLookAndFeel()
{
    michroma = Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
}

Font SpaceLookAndFeel::labelFont (float size) const
{
    return Font (FontOptions (michroma).withHeight (size));
}

void SpaceLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h,
                                         float pos, float startAngle, float endAngle, Slider&)
{
    const auto area = Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float r = jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const float arcR = r - 2.0f, body = r * 0.80f;
    const float ang = startAngle + pos * (endAngle - startAngle);

    Path track, value;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, ang, true);
    g.setColour (Colour (0xff3c5078).withAlpha (0.8f));
    g.strokePath (track, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (ice);
    g.strokePath (value, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));

    auto b = Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c);
    g.setGradientFill (ColourGradient (Colour (0xff26324e), c.x, b.getY(), Colour (0xff141b2e), c.x, b.getBottom(), false));
    g.fillEllipse (b);
    g.setColour (Colour (0xff7896c8).withAlpha (0.55f));
    g.drawEllipse (b, 1.2f);

    g.setColour (Colours::white);
    g.drawLine (Line<float> (c.getPointOnCircumference (body * 0.30f, ang),
                             c.getPointOnCircumference (body * 0.88f, ang)), 2.4f);
}

void SpaceLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool, bool)
{
    auto area = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();

    if (b.getButtonText() == "SYNC" || b.getButtonText() == "MS")
    {
        // small pill: MS / SYNC
        g.setColour (on ? ice.withAlpha (0.85f) : Colour (0xff1a2440).withAlpha (0.9f));
        g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
        g.setColour (ice.withAlpha (0.6f));
        g.drawRoundedRectangle (area, area.getHeight() * 0.5f, 1.0f);
        g.setColour (on ? Colour (0xff0b1226) : labelC);
        g.setFont (labelFont (7.5f));
        g.drawText (on ? "SYNC" : "MS", area, Justification::centred);
        return;
    }

    // shimmer switch
    auto pill = area.withSizeKeepingCentre (52.0f, 26.0f);
    g.setColour (on ? Colour (0xff5aaaff) : Colour (0xff1a2440));
    g.fillRoundedRectangle (pill, 13.0f);
    g.setColour (ice.withAlpha (0.5f));
    g.drawRoundedRectangle (pill, 13.0f, 1.0f);
    const float kx = on ? pill.getRight() - 23.0f : pill.getX() + 3.0f;
    g.setColour (Colours::white.withAlpha (on ? 1.0f : 0.7f));
    g.fillEllipse (kx, pill.getY() + 3.0f, 20.0f, 20.0f);
}

//==============================================================================
SpaceOperaEditor::SpaceOperaEditor (SpaceOperaProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    bg        = ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);
    astronaut = ImageCache::getFromMemory (BinaryData::astronaut_png,  BinaryData::astronaut_pngSize);
    shuttle   = ImageCache::getFromMemory (BinaryData::shuttle_png,    BinaryData::shuttle_pngSize);
    logo      = ImageCache::getFromMemory (BinaryData::logo_png,       BinaryData::logo_pngSize);

    const std::array<std::pair<const char*, const char*>, 7> defs {{
        { "predelay", "PRE-DELAY" }, { "decay", "DECAY" }, { "size", "SIZE" }, { "cold", "COLD" },
        { "width", "WIDTH" }, { "duck", "DUCK" }, { "mix", "MIX" } }};

    auto setupKnob = [this] (Knob& k, const char* id, const char* label)
    {
        k.label = label;
        k.slider.setComponentID (id);
        k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        k.slider.setMouseDragSensitivity (220);
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    };
    for (size_t i = 0; i < knobs.size(); ++i)
        setupKnob (knobs[i], defs[i].first, defs[i].second);
    setupKnob (noteKnob, "prenote", "PRE-DELAY");

    shimmerButton.setButtonText ("SHIMMER");
    addAndMakeVisible (shimmerButton);
    shimmerAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "shimmer", shimmerButton);

    syncButton.setButtonText ("SYNC");
    addAndMakeVisible (syncButton);
    syncAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "presync", syncButton);

    // starfield + visor frost pattern (fixed seeds so it looks the same every time)
    Random r (2026);
    for (int i = 0; i < 170; ++i)
        stars.push_back ({ r.nextFloat() * MathConstants<float>::twoPi, 0.05f + r.nextFloat() * 1.15f,
                           0.3f + r.nextFloat() * 0.7f, r.nextFloat() * MathConstants<float>::twoPi });
    for (int i = 0; i < 18; ++i)
        frost.push_back ({ r.nextFloat() * MathConstants<float>::twoPi, 0.25f + r.nextFloat() * 0.55f });

    setSize (W, H);
    startTimerHz ((int) fps);
}

SpaceOperaEditor::~SpaceOperaEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpaceOperaEditor::resized()
{
    bar = Rectangle<float> (14.0f, 426.0f, (float) W - 28.0f, 100.0f);
    const float slot = bar.getWidth() / 8.0f;
    for (int i = 0; i < 7; ++i)
    {
        auto c = Point<float> (bar.getX() + slot * ((float) i + 0.5f), bar.getY() + 42.0f);
        knobs[(size_t) i].slider.setBounds (Rectangle<float> (62.0f, 62.0f).withCentre (c).toNearestInt());
    }
    noteKnob.slider.setBounds (knobs[0].slider.getBounds());
    syncButton.setBounds (Rectangle<int> (40, 15).withCentre (knobs[0].slider.getBounds().getTopLeft().translated (2, 2)));
    shimmerButton.setBounds (Rectangle<float> (70.0f, 40.0f)
                                 .withCentre ({ bar.getX() + slot * 7.5f, bar.getY() + 42.0f }).toNearestInt());
}

float SpaceOperaEditor::norm (const char* id) const
{
    if (auto* prm = proc.apvts.getParameter (id))
        return prm->getValue();
    return 0.0f;
}

//==============================================================================
void SpaceOperaEditor::timerCallback()
{
    const float k = 0.15f;
    const bool freeze = proc.apvts.getRawParameterValue ("decay")->load() >= SpaceOperaProcessor::infiniteDecay;
    const bool sync = proc.apvts.getRawParameterValue ("presync")->load() > 0.5f;

    vPre     = lerp (vPre,     sync ? norm ("prenote") : norm ("predelay"), k);
    vDecay   = lerp (vDecay,   norm ("decay"), k);
    vSize    = lerp (vSize,    norm ("size"), k);
    vCold    = lerp (vCold,    norm ("cold"), k);
    vWidth   = lerp (vWidth,   norm ("width"), k);
    vMix     = lerp (vMix,     norm ("mix"), k);
    vShimmer = lerp (vShimmer, norm ("shimmer"), 0.08f);
    vFreeze  = lerp (vFreeze,  freeze ? 1.0f : 0.0f, 0.1f);
    vDuck    = lerp (vDuck,    proc.duckAmount.load(), 0.35f);
    vWet     = lerp (vWet,     jmin (1.0f, proc.wetLevel.load() * 2.5f), 0.3f);

    knobs[0].slider.setVisible (! sync);
    noteKnob.slider.setVisible (sync);

    // time slows to a stop when DECAY is at infinity
    const float dt = 1.0f / fps;
    const float timeScale = 1.0f - vFreeze;
    animTime += dt * timeScale;

    // stars drift outward; trails grow with DECAY
    for (auto& s : stars)
    {
        s.radius += dt * timeScale * 0.010f * s.depth * (0.4f + s.radius);
        if (s.radius > 1.25f) { s.radius = 0.04f + rng.nextFloat() * 0.1f; s.angle = rng.nextFloat() * MathConstants<float>::twoPi; }
        s.twinkle += dt * timeScale * 2.0f;
    }

    // shimmer: full-screen meteor shower (denser when the reverb tail is loud)
    if (vShimmer > 0.05f && timeScale > 0.05f)
    {
        const int count = (int) (vShimmer * (0.35f + 1.6f * vWet) + rng.nextFloat());
        for (int i = 0; i < count; ++i)
        {
            Spark m;
            const float ang = MathConstants<float>::pi * (0.80f + (rng.nextFloat() - 0.5f) * 0.05f); // down-left
            const float speed = 520.0f + rng.nextFloat() * 480.0f;
            m.vel = { std::cos (ang) * speed, std::sin (ang) * speed };   // screen y points down
            // enter from the top edge or the right edge
            if (rng.nextFloat() < 0.65f) m.pos = { rng.nextFloat() * (W + 260.0f), -20.0f - rng.nextFloat() * 60.0f };
            else                         m.pos = { W + 20.0f + rng.nextFloat() * 60.0f, rng.nextFloat() * H * 0.55f };
            m.maxLife = m.life = 1.6f;
            m.size = 50.0f + rng.nextFloat() * 130.0f;                    // tail length
            const float pick = rng.nextFloat();
            m.colour = pick < 0.55f ? Colours::white : pick < 0.85f ? ice : Colour (0xffb79cff);
            sparks.push_back (m);
        }
    }
    for (auto& sp : sparks) { sp.pos += sp.vel * dt * timeScale; sp.life -= dt * timeScale; }
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s)
                  { return s.life <= 0.0f || s.pos.x < -250.0f || s.pos.y > (float) H + 250.0f; }), sparks.end());
    if (sparks.size() > 400) sparks.erase (sparks.begin(), sparks.begin() + (long) (sparks.size() - 400));

    repaint();
}

//==============================================================================
void SpaceOperaEditor::drawScene (Graphics& g)
{
    const float t = animTime;
    const float widthScale = 0.75f + 0.5f * vWidth;      // WIDTH spreads the sky sideways

    // 1. background, camera pulls back as SIZE grows
    {
        const float zoom = lerp (1.22f, 1.0f, vSize);
        const float bw = (float) W * zoom * 1.04f, bh = (float) H * zoom * 1.04f;
        const float drift = std::sin (t * 0.05f) * 6.0f;
        g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
        g.setOpacity (1.0f);
        g.drawImage (bg, Rectangle<float> (bw, bh).withCentre ({ W * 0.5f + drift, H * 0.5f }));
    }

    // 2. COLD: colour temperature, warm violet -> icy cyan
    g.setColour (Colour (0xff8a4cc8).withAlpha (0.16f * (1.0f - vCold)));
    g.fillAll();
    g.setColour (Colour (0xff5fd2ff).withAlpha (0.10f * vCold));
    g.fillAll();

    // 3. MIX: nebula glows, pulsing with the reverb tail
    {
        const float glow = (0.04f + 0.30f * vMix) * (0.65f + 0.35f * vWet);
        const Point<float> n1 { 170.0f, 120.0f }, n2 { 620.0f, 160.0f };
        g.setGradientFill (ColourGradient (Colour (0xff7a5cff).withAlpha (glow), n1, Colours::transparentBlack, n1.translated (260.0f, 0.0f), true));
        g.fillAll();
        g.setGradientFill (ColourGradient (ice.withAlpha (glow), n2, Colours::transparentBlack, n2.translated (240.0f, 0.0f), true));
        g.fillAll();
    }

    // 4. stars with DECAY-length trails
    {
        const Point<float> centre { W * 0.5f, 230.0f };
        const float D = 520.0f;
        const float trail = 0.004f + 0.075f * vDecay * vDecay;
        for (const auto& s : stars)
        {
            const float ca = std::cos (s.angle), sa = std::sin (s.angle);
            const auto p  = centre + Point<float> (ca * s.radius * D * widthScale, sa * s.radius * D * 0.62f);
            const float r0 = jmax (0.0f, s.radius - trail * s.depth * (0.3f + s.radius));
            const auto p0 = centre + Point<float> (ca * r0 * D * widthScale, sa * r0 * D * 0.62f);
            const float a = (0.35f + 0.45f * s.depth) * (0.75f + 0.25f * std::sin (s.twinkle)) * jmin (1.0f, s.radius * 6.0f);
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (a), p, Colours::white.withAlpha (0.0f), p0, false));
            g.drawLine (Line<float> (p0, p), 0.6f + 1.2f * s.depth);
            g.setColour (Colours::white.withAlpha (a));
            g.fillEllipse (Rectangle<float> (1.0f + 1.6f * s.depth, 1.0f + 1.6f * s.depth).withCentre (p));
        }
    }

    // 4b. SHIMMER: meteor shower across the whole sky
    for (const auto& m : sparks)
    {
        const float fade = jmin (1.0f, m.life / m.maxLife * 2.0f) * vShimmer;
        const auto dir = m.vel / jmax (1.0f, m.vel.getDistanceFromOrigin());
        const auto tail = m.pos - dir * m.size;
        g.setGradientFill (ColourGradient (m.colour.withAlpha (0.9f * fade), m.pos,
                                           m.colour.withAlpha (0.0f), tail, false));
        g.drawLine (Line<float> (tail, m.pos), 1.8f);
        g.setColour (m.colour.withAlpha (0.25f * fade));
        g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (m.pos));
        g.setColour (Colours::white.withAlpha (fade));
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (m.pos));
    }

    // 5. shuttle (far, upper-left); gets smaller as SIZE grows, parallax with WIDTH
    const float shuttleW = 270.0f * lerp (1.12f, 0.78f, vSize);
    const float shuttleH = shuttleW * (float) shuttle.getHeight() / (float) shuttle.getWidth();
    const Point<float> shuttlePos { 28.0f - (vWidth - 0.66f) * 30.0f, 110.0f + std::sin (t * 0.3f) * 3.0f };
    g.setOpacity (1.0f);
    g.drawImage (shuttle, Rectangle<float> (shuttlePos.x, shuttlePos.y, shuttleW, shuttleH));
    const auto hatch = shuttlePos + Point<float> (hatchFrac.x * shuttleW, hatchFrac.y * shuttleH);

    // 6. astronaut: PRE-DELAY = distance from the ship, DUCK = pushed away by the input, SIZE = farther away
    const float astroH = 250.0f * lerp (1.15f, 0.80f, vSize);
    const float astroW = astroH * (float) astronaut.getWidth() / (float) astronaut.getHeight();
    const Point<float> astroCentre { lerp (480.0f, 620.0f, vPre) + vDuck * 45.0f + (vWidth - 0.66f) * 20.0f,
                                     215.0f + std::sin (t * 0.42f) * 9.0f - vDuck * 10.0f };
    const float rot = -0.12f + std::sin (t * 0.13f) * 0.10f + vDuck * 0.08f;
    const auto xf = AffineTransform::scale (astroW / (float) astronaut.getWidth(), astroH / (float) astronaut.getHeight())
                        .translated (-astroW * 0.5f, -astroH * 0.5f)
                        .rotated (rot)
                        .translated (astroCentre);
    auto waist = Point<float> (waistFrac.x * (float) astronaut.getWidth(), waistFrac.y * (float) astronaut.getHeight());
    waist.applyTransform (xf);

    // soft cold rim light behind the astronaut
    g.setGradientFill (ColourGradient (ice.withAlpha (0.12f + 0.12f * vCold), astroCentre,
                                       Colours::transparentBlack, astroCentre.translated (astroH * 0.75f, 0.0f), true));
    g.fillEllipse (Rectangle<float> (astroH * 1.5f, astroH * 1.5f).withCentre (astroCentre));

    g.setOpacity (1.0f);
    g.drawImageTransformed (astronaut, xf);

    // 7. tether: slack when close, taut when PRE-DELAY pushes the astronaut out
    {
        const float sag = lerp (110.0f, 25.0f, vPre);
        Path tether;
        tether.startNewSubPath (hatch);
        tether.cubicTo (hatch.translated (70.0f, sag), waist.translated (-90.0f, sag * 0.8f), waist);
        g.setColour (Colour (0xff0d1428).withAlpha (0.9f));
        g.strokePath (tether, PathStrokeType (3.4f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xffe4ebf5));
        g.strokePath (tether, PathStrokeType (1.7f, PathStrokeType::curved, PathStrokeType::rounded));
        // clip ring on the suit belt
        g.setColour (Colour (0xff0d1428));
        g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre (waist));
        g.setColour (Colour (0xffc9d6ea));
        g.drawEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (waist), 1.4f);
    }

    // 8. COLD: frost creeping over the visor
    if (vCold > 0.02f)
    {
        auto vc = Point<float> (visorFrac.x * (float) astronaut.getWidth(), visorFrac.y * (float) astronaut.getHeight());
        vc.applyTransform (xf);
        const float vr = visorRFrac * astroW;
        g.setColour (Colours::white.withAlpha (0.22f * vCold));
        g.fillEllipse (Rectangle<float> (vr * 2.0f, vr * 2.0f).withCentre (vc));
        g.setColour (Colours::white.withAlpha (0.75f * vCold));
        for (const auto& [a, len] : frost)
        {
            const auto edge = vc.getPointOnCircumference (vr * 0.95f, a);
            const auto tip  = vc.getPointOnCircumference (vr * (0.95f - len * vCold * 0.6f), a + 0.15f);
            g.drawLine (Line<float> (edge, tip), 1.0f);
        }
    }

    // 10. freeze: time stops, the world turns to ice
    if (vFreeze > 0.01f)
    {
        g.setGradientFill (ColourGradient (Colours::transparentBlack, W * 0.5f, H * 0.45f,
                                           Colour (0xff9fe6ff).withAlpha (0.35f * vFreeze), 0.0f, 0.0f, true));
        g.fillAll();
    }

    // 11. logo
    const float lw = 430.0f, lh = lw * (float) logo.getHeight() / (float) logo.getWidth();
    const Point<float> lc { W * 0.5f, 34.0f };
    {
        // soft elliptical shade behind the title (a circle squashed vertically)
        Graphics::ScopedSaveState ss (g);
        g.addTransform (AffineTransform::scale (1.0f, 0.22f, lc.x, lc.y));
        g.setGradientFill (ColourGradient (Colour (0xff050914).withAlpha (0.55f), lc, Colours::transparentBlack, lc.translated (300.0f, 0.0f), true));
        g.fillEllipse (Rectangle<float> (600.0f, 600.0f).withCentre (lc));
    }
    g.setOpacity (1.0f);
    g.drawImage (logo, Rectangle<float> (lw, lh).withCentre (lc));
}

void SpaceOperaEditor::drawControls (Graphics& g)
{
    g.setColour (Colour (0xff0a1022).withAlpha (0.62f));
    g.fillRoundedRectangle (bar, 14.0f);
    g.setColour (ice.withAlpha (0.35f));
    g.drawRoundedRectangle (bar, 14.0f, 1.0f);

    // labels; while hovering or dragging, show the value instead
    g.setFont (lnf.labelFont (8.5f));
    auto drawLabel = [&] (Slider& s, const String& label)
    {
        if (! s.isVisible()) return;
        auto area = Rectangle<int> (s.getX() - 20, s.getBottom() + 4, s.getWidth() + 40, 14);
        const bool active = s.isMouseOverOrDragging();
        String text = label;
        if (active)
            if (auto* prm = dynamic_cast<RangedAudioParameter*> (proc.apvts.getParameter (s.getComponentID())))
                text = prm->getCurrentValueAsText();
        g.setColour (active ? ice : labelC);
        g.drawText (text, area, Justification::centred);
    };
    for (auto& k : knobs) drawLabel (k.slider, k.label);
    drawLabel (noteKnob.slider, noteKnob.label);

    g.setColour (labelC);
    g.drawText ("SHIMMER", shimmerButton.getBounds().withY (knobs[0].slider.getBottom() + 4).withHeight (14).expanded (20, 0),
                Justification::centred);
}

void SpaceOperaEditor::paint (Graphics& g)
{
    drawScene (g);
    drawControls (g);
}
