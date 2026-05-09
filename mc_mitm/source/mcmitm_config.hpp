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
#include "bluetooth_mitm/bluetooth/bluetooth_types.hpp"

namespace ams::mitm {

    struct MissionControlConfig {
        struct {
            bool enable_rumble;
            bool enable_motion;
        } general;

        struct {
            char host_name[0x20];
            bluetooth::Address host_address;
        } bluetooth;

        struct {
            int analog_trigger_activation_threshold;
            bool dualshock3_enable_usb_pairing;
            int dualshock3_led_mode;
            int dualshock4_polling_rate;
            int dualshock4_lightbar_brightness;
            int dualsense_lightbar_brightness;
            bool dualsense_enable_player_leds;
            int dualsense_vibration_intensity;
        } misc;

        struct {
            int  mode;                          // 0=off, 1=rstick_y_split
            int  zr_threshold;                  // 0..100, >100 = off
            int  zl_threshold;                  // 0..100, >100 = off
            int  stick_y_to_buttons_threshold;  // 0..100, >100 = off
            int  deadzone;                      // 0..100
            bool invert_y;
        } trigger_map;
    };

    void LoadConfiguration();
    MissionControlConfig *GetGlobalConfig();
    SetLanguage GetSystemLanguage();

}
