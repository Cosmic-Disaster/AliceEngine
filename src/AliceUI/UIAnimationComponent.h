#pragma once

#include <string>
#include <vector>

namespace Alice
{
    enum class UIAnimProperty
    {
        PositionX = 0,
        PositionY,
        ScaleX,
        ScaleY,
        Rotation,
        ImageAlpha,
        TextAlpha,
        GlobalAlpha,
        OutlineThickness,
        RadialFill,
        GlowStrength,
        VitalAmplitude
    };

    struct UIAnimTrack
    {
        std::string name;
        UIAnimProperty property{ UIAnimProperty::PositionX };
        std::string curvePath;

        float duration{ 1.0f };
        float delay{ 0.0f };
        float from{ 0.0f };
        float to{ 1.0f };

        bool loop{ false };
        bool pingPong{ false };
        bool useNormalizedTime{ true };
        bool additive{ false };

        // runtime state (not serialized)
        bool playing{ false };
        bool reverse{ false };
        float time{ 0.0f };
        bool baseCaptured{ false };
        float baseValue{ 0.0f };
    };

    struct UIAnimationComponent
    {
        bool playOnStart{ false };
        std::vector<UIAnimTrack> tracks;
        bool started{ false };

        void Play(const std::string& trackName, bool restart = true)
        {
            for (auto& t : tracks)
            {
                if (t.name == trackName)
                {
                    t.playing = true;
                    if (restart)
                    {
                        t.time = -t.delay;
                        t.reverse = false;
                        t.baseCaptured = false;
                    }
                }
            }
        }

        void Stop(const std::string& trackName)
        {
            for (auto& t : tracks)
            {
                if (t.name == trackName)
                {
                    t.playing = false;
                    t.time = 0.0f;
                    t.reverse = false;
                    t.baseCaptured = false;
                }
            }
        }

        void PlayAll(bool restart = true)
        {
            for (auto& t : tracks)
            {
                t.playing = true;
                if (restart)
                {
                    t.time = -t.delay;
                    t.reverse = false;
                    t.baseCaptured = false;
                }
            }
        }

        void StopAll()
        {
            for (auto& t : tracks)
            {
                t.playing = false;
                t.time = 0.0f;
                t.reverse = false;
                t.baseCaptured = false;
            }
        }
    };
}
