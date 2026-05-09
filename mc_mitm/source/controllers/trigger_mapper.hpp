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
    };

    // Scale any unsigned-integer trigger value into a normalized 0..0xFFFF range.
    // Specialised for u8 (DS4/DS3/DualSense — 0xFF * 0x101 = 0xFFFF, exact endpoints).
    constexpr u16 NormalizeTriggerU8(u8 t) {
        return static_cast<u16>(t) * 0x101;
    }

    class TriggerMapper {
        public:
            static TriggerMapper& Instance();

            void Initialize(const TriggerProfile& global_profile);

            // Apply the active profile to the per-packet controller state.
            // No-op when the resolved profile's mode == Off (the hot-path common case).
            void Apply(SwitchButtonData& buttons,
                       SwitchAnalogStick& lstick,
                       SwitchAnalogStick& rstick,
                       u16 left_trigger_norm,
                       u16 right_trigger_norm);

        private:
            TriggerProfile m_global;
    };

}
