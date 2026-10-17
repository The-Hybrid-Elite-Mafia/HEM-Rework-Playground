#pragma once

#include "EliteEditor.h"
#include "Analysis/Waterfall.h"

namespace pg::ui
{
/**
    Elite EQ window: the standard Elite layout plus the live analyzer the plugin promises.
      - the spectrum of what comes OUT of the EQ (filled, falling slowly so peaks are readable)
      - the EQ curve (all six bands + OUTPUT added up), drawn on top
      - one numbered dot per band: drag it left / right for FREQ, up / down for GAIN
        (Low Cut / High Cut / Notch only move left / right). Double-click a dot: GAIN back to 0 dB.
    Message thread only: audio comes from the plugin's lock-free analyzer FIFO in tick().
*/
class EqEditor final : public EliteEditor
{
public:
    explicit EqEditor (EliteEq&);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    int displayHeight() const override { return 230; }
    void tick() override;

private:
    juce::Rectangle<float> plotArea() const;
    float xOf (float hz, juce::Rectangle<float> r) const noexcept;
    float hzOf (float x, juce::Rectangle<float> r) const noexcept;
    float yOfDb (float db, juce::Rectangle<float> r) const noexcept;     // curve scale  +-18 dB
    float dbOfY (float y, juce::Rectangle<float> r) const noexcept;
    float yOfLevel (float db, juce::Rectangle<float> r) const noexcept;  // spectrum scale -84..0 dB
    juce::Point<float> dotOf (int band, juce::Rectangle<float> r) const;
    int bandAt (juce::Point<float>) const;
    bool movesGain (int band) const;
    juce::RangedAudioParameter* param (int band, const char* what) const;

    EliteEq& eq;
    analysis::Waterfall analyzer;
    std::vector<float> scratch = std::vector<float> ((size_t) ElitePlugin::kAnalyzerFifoSize, 0.0f);
    std::array<float, analysis::Waterfall::kPoints> shown {};
    double preparedRate = 0.0;
    int dragBand = -1, hoverBand = -1;
};
} // namespace pg::ui
