#pragma once

// Render interpolation for races.

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <safetyhook.hpp>

#include "../../core/config.h"

#include "../../game/structs.h"

namespace nfs5
{
    class Interpolation
    {
    private:

        static constexpr std::int32_t HISTORY_SIZE = 6;
        static constexpr std::int32_t MAX_SPACING = 4;

        struct Sample
        {
            game::Transform m_State = {};
            std::int32_t m_Step = 0;
        };

        // Recorded states, oldest first, a new entry is added only when the state changes.
        struct Track
        {
            float * m_Position = nullptr;
            float * m_Rotation = nullptr;
            std::array<Sample, HISTORY_SIZE> m_History = {};
            std::int32_t m_Count = 0;
            std::int32_t m_LastSeenStep = 0;

            // Steps between changes, stable once two changes in a row had the same spacing.
            std::int32_t m_LastSpacing = 0;
            std::int32_t m_StableSpacing = 0;

            // Frames left in which the renderer is treated as the owner of this value.
            // While owned nothing is blended, the value is only watched for further writes.
            std::int32_t m_RendererOwnedFrames = 0;
            bool m_ResetPending = false;

            [[nodiscard]] const Sample & Newest() const
            {
                return m_History[m_Count - 1];
            }

            void Push(
                const Sample & sample
            );

            void Reset(
                const Sample & sample
            );
        };

        struct AppliedEntry
        {
            Track * m_Track = nullptr;
            bool m_WatchOnly = false; // Watch only entries record the value before the frame and write nothing.
            float * m_Position = nullptr;
            float * m_Rotation = nullptr;
            game::Transform m_Saved = {};
            game::Transform m_Written = {};
        };

        std::unique_ptr<safetyhook::InlineHook> m_SimStepHook;
        std::unique_ptr<safetyhook::MidHook> m_RenderCheckHook;
        std::unique_ptr<safetyhook::MidHook> m_AfterRenderHook;
        FramerateSettings m_Settings;
        std::unordered_map< const void *, Track > m_Tracks;
        std::vector<AppliedEntry> m_Applied;

        // Simulation clock, maps step numbers to 'QueryPerformanceCounter' time.
        double m_StepPeriod = 0.0;
        double m_LatenessMin = 0.0;
        std::int64_t m_AnchorTime = 0;
        std::int64_t m_LastStepTime = 0;
        std::int32_t m_AnchorStep = 0;
        std::int32_t m_LastStep = 0;
        bool m_ClockValid = false;
        bool m_PeriodLocked = false;

        // All entities are drawn this many steps behind the newest step, the slowest regular
        // updater decides it so every entity has a recorded state on both sides.
        // Cars and camera positions change every step, camera rotations every second step.
        std::int32_t m_RenderDelay = 1;
        std::int32_t m_DelayNeededAt = 0;

        // Render time of the previous frame, render time never moves backwards.
        double m_LastRenderStep = 0.0;
        bool m_HaveLastRenderStep = false;
        bool m_Installed = false;

        Interpolation() = default;

        static int __cdecl SimStepHook();

        static void RenderCheckMidHook(
            safetyhook::Context & context
        );

        static void AfterRenderMidHook(
            safetyhook::Context & context
        );

        [[nodiscard]] static bool IsRaceRunning();

        void OnSimStep();

        void UpdateClock(
            std::int32_t step,
            std::int64_t now
        );

        void ResetClock(
            std::int32_t step,
            std::int64_t now
        );

        [[nodiscard]] double CurrentStep(
            std::int64_t now
        ) const;

        void Capture(
            float * position,
            float * rotation,
            std::int32_t step
        );

        void CaptureAll( std::int32_t step );

        void Apply( std::int64_t now );

        void Restore();

        void UpdateRenderDelay( std::int32_t step );

    public:

        Interpolation( const Interpolation & ) = delete;
        Interpolation & operator=( const Interpolation & ) = delete;

        static Interpolation & GetInstance();

        bool Install(
            const FramerateSettings & settings
        );

        void Uninstall();
    };
}