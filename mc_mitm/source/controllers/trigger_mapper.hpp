/*
 * Copyright (c) 2020-2026 ndeadly
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include "switch_controller.hpp"
#include <stratosphere.hpp>
#include <vector>

namespace ams::controller {

    enum class TriggerMode : u8 {
        Off            = 0,
        RstickYSplit   = 1,   // RT → +Y, LT → −Y on the right stick
    };

    struct TriggerProfile {
        TriggerMode mode         = TriggerMode::Off;
        // Trigger-percent threshold above which the digital ZR/ZL bit also fires.
        // Any value > 100 disables digital firing — appropriate for the racing /
        // analog-water-pressure use cases where the trigger is now exclusively
        // an analog stick input.
        u8          zr_threshold = 101;  // off
        u8          zl_threshold = 101;  // off
        u8          deadzone     = 0;    // 0..100, on raw trigger
        bool        invert_y     = false;
        // In rstick_y_split mode the trigger overwrites the right-stick Y axis,
        // freeing the physical stick's up/down for repurposing as digital ZR/ZL.
        // 0..100 = stick-deflection percent threshold; > 100 = feature off.
        // Physical stick up past the threshold fires ZR; down fires ZL.
        u8          stick_y_to_buttons_threshold = 101;  // off
    };

    // Scale any unsigned-integer trigger value into a normalized 0..0xFFFF range.
    // Specialised for u8 (DS4/DS3/DualSense — 0xFF * 0x101 = 0xFFFF, exact endpoints).
    constexpr u16 NormalizeTriggerU8(u8 t) {
        return static_cast<u16>(t) * 0x101;
    }

    // Specialised for 10-bit triggers (Xbox One — 0..0x3FF). Bit-replicate so the
    // top 6 bits of t fill the bottom 6 bits of the u16, giving exact endpoints
    // (t=0x3FF → 0xFFFF, t=0 → 0).
    constexpr u16 NormalizeTriggerU10(u16 t) {
        return static_cast<u16>((t << 6) | (t >> 4));
    }

    class TriggerMapper {
        public:
            static TriggerMapper& Instance();

            // Set the global default profile (from the [trigger_map] section of
            // missioncontrol.ini). Always called once at boot.
            void Initialize(const TriggerProfile& global_profile);

            // Scan sdmc:/config/MissionControl/{controllers,titles}/ for per-MAC
            // and per-titleID overrides. Each filename is expected to be the key
            // (12-hex MAC for controllers/, 16-hex programID for titles/) with an
            // .ini extension; each file's [trigger_map] section is parsed into a
            // complete TriggerProfile. Called once at boot from LoadConfiguration,
            // and again from the hot-reload thread on every title switch.
            void LoadDirectoryProfiles();

            // Launch the background thread that waits on the existing process-switch
            // event and re-runs LoadDirectoryProfiles on each title transition, so
            // newly-added ini files take effect without a sysmodule restart.
            // No-ops on second call. The thread runs for the lifetime of the sysmodule.
            void StartHotReloadThread();

            // Apply the controller's currently-resolved profile to the per-packet
            // controller state. No-op when the resolved profile's mode == Off.
            // Called from the bluetooth input thread; takes m_mutex but never performs
            // filesystem I/O on this path.
            void Apply(const bluetooth::Address& addr,
                       SwitchButtonData& buttons,
                       SwitchAnalogStick& lstick,
                       SwitchAnalogStick& rstick,
                       u16 left_trigger_norm,
                       u16 right_trigger_norm);

        private:
            // Resolve the active profile for this controller given a specific title.
            // Precedence: per-title > per-controller > global. Profile-level — whichever
            // level matches first is returned in full, no field merging.
            const TriggerProfile& Resolve(const bluetooth::Address& addr, u64 current_title) const;

            // LoadDirectoryProfiles without taking m_mutex; caller must hold it.
            void LoadDirectoryProfilesUnsafe();

            struct ControllerEntry { bluetooth::Address addr;     TriggerProfile profile; };
            struct TitleEntry      { u64                title_id; TriggerProfile profile; };

            os::SdkMutex                 m_mutex;
            TriggerProfile               m_global;
            std::vector<ControllerEntry> m_controllers;
            std::vector<TitleEntry>      m_titles;
    };

}
