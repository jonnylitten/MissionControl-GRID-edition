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
#include "trigger_mapper.hpp"
#include "switch_analog_stick.hpp"
#include <algorithm>

namespace ams::controller {

    namespace {

        // Normalize raw 0..0xFFFF trigger into 0..1 after deadzone is applied.
        // Returns 0 when raw <= deadzone_floor; otherwise rescales the remaining
        // travel back to 0..1 so the deadzone doesn't compress the active range.
        float NormalizeWithDeadzone(u16 raw, u8 deadzone_pct) {
            const float v       = raw / static_cast<float>(0xFFFF);
            const float dz      = std::min(deadzone_pct, u8(99)) / 100.0f;
            if (v <= dz) {
                return 0.0f;
            }
            return (v - dz) / (1.0f - dz);
        }

        u16 DeflectionToY12Bit(float deflection /* in [-1, +1] */) {
            constexpr int Center   = SwitchAnalogStick::Center;             // 0x800
            constexpr int HalfSpan = SwitchAnalogStick::Max - Center;       // 0x7FF
            const int y = Center + static_cast<int>(deflection * HalfSpan);
            return static_cast<u16>(std::clamp(y, int(SwitchAnalogStick::Min), int(SwitchAnalogStick::Max)));
        }

        constinit TriggerMapper g_mapper;

    }

    TriggerMapper& TriggerMapper::Instance() {
        return g_mapper;
    }

    void TriggerMapper::Initialize(const TriggerProfile& global_profile) {
        m_global = global_profile;
    }

    void TriggerMapper::Apply(SwitchButtonData& buttons,
                              SwitchAnalogStick& /* lstick */,
                              SwitchAnalogStick& rstick,
                              u16 left_trigger_norm,
                              u16 right_trigger_norm) {
        const TriggerProfile& p = m_global;
        if (p.mode == TriggerMode::Off) {
            return;
        }

        const float lt = NormalizeWithDeadzone(left_trigger_norm,  p.deadzone);
        const float rt = NormalizeWithDeadzone(right_trigger_norm, p.deadzone);

        switch (p.mode) {
            case TriggerMode::RstickYSplit: {
                float deflection = rt - lt;                                  // [-1, +1]
                if (p.invert_y) {
                    deflection = -deflection;
                }
                rstick.SetY(DeflectionToY12Bit(deflection));

                // Override ZL/ZR — only fire after the configured threshold of trigger travel.
                buttons.ZR = (rt * 100.0f) >= p.zr_threshold ? 1 : 0;
                buttons.ZL = (lt * 100.0f) >= p.zl_threshold ? 1 : 0;
                break;
            }
            case TriggerMode::Off:
            default:
                break;
        }
    }

}
