#pragma once
// The 3D meter on the MASTER page: a spectrum landscape of the master output (newest sound in
// front, about 2.7 s going back; lows left, highs right; height and colour = level).
// Drag to turn it, double-click to put it back. Reads the engine's master tap only while shown.
#include "../App.hpp"
#include "Waterfall.hpp"

namespace hem::app
{
class Spectrum3D
{
public:
    explicit Spectrum3D (pg::PlaygroundEngine& e) : engine (e) {}
    ~Spectrum3D() { setShowing (false); }

    void setShowing (bool on);
    void draw (Ui&, Rect area);

    static constexpr float kDefaultYaw = -0.35f, kDefaultPitch = 0.55f;

private:
    void pull();
    struct P { float x, y; };
    P project (float x, float y, float z) const noexcept;

    pg::PlaygroundEngine& engine;
    pg::analysis::Waterfall waterfall;
    std::vector<float> scratch = std::vector<float> ((std::size_t) pg::PlaygroundEngine::kTapSize, 0.0f);
    float yaw = kDefaultYaw, pitch = kDefaultPitch, dragYaw = 0.0f, dragPitch = 0.0f;
    double preparedRate = 0.0;
    bool tapping = false;
};
} // namespace hem::app
