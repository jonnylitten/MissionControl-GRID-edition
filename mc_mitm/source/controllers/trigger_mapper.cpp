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
#include "../mcmitm_process_monitor.hpp"
#include <stratosphere.hpp>
#include <algorithm>
#include <cstring>

namespace ams::controller {

    namespace {

        constexpr const char ControllersDir[] = "sdmc:/config/MissionControl/controllers";
        constexpr const char TitlesDir[]      = "sdmc:/config/MissionControl/titles";

        float NormalizeWithDeadzone(u16 raw, u8 deadzone_pct) {
            const float v  = raw / static_cast<float>(0xFFFF);
            const float dz = std::min(deadzone_pct, u8(99)) / 100.0f;
            if (v <= dz) {
                return 0.0f;
            }
            return (v - dz) / (1.0f - dz);
        }

        u16 DeflectionToY12Bit(float deflection /* in [-1, +1] */) {
            constexpr int Center   = SwitchAnalogStick::Center;
            constexpr int HalfSpan = SwitchAnalogStick::Max - Center;
            const int y = Center + static_cast<int>(deflection * HalfSpan);
            return static_cast<u16>(std::clamp(y, int(SwitchAnalogStick::Min), int(SwitchAnalogStick::Max)));
        }

        int ProfileIniHandler(void *user, const char *section, const char *name, const char *value) {
            if (strcasecmp(section, "trigger_map") != 0) {
                return 1;  // ignore other sections, don't fail the parse
            }
            auto *p = reinterpret_cast<TriggerProfile *>(user);

            auto parse_threshold_or_off = [&](u8 *out) {
                if (strcasecmp(value, "off") == 0) {
                    *out = 101;
                } else {
                    int tmp = std::strtol(value, nullptr, 10);
                    if (tmp >= 0 && tmp <= 100) {
                        *out = static_cast<u8>(tmp);
                    }
                }
            };

            if (strcasecmp(name, "mode") == 0) {
                if (strcasecmp(value, "off") == 0) {
                    p->mode = TriggerMode::Off;
                } else if (strcasecmp(value, "rstick_y_split") == 0) {
                    p->mode = TriggerMode::RstickYSplit;
                }
            } else if (strcasecmp(name, "zr_threshold") == 0) {
                parse_threshold_or_off(&p->zr_threshold);
            } else if (strcasecmp(name, "zl_threshold") == 0) {
                parse_threshold_or_off(&p->zl_threshold);
            } else if (strcasecmp(name, "stick_y_to_buttons_threshold") == 0) {
                parse_threshold_or_off(&p->stick_y_to_buttons_threshold);
            } else if (strcasecmp(name, "deadzone") == 0) {
                int tmp = std::strtol(value, nullptr, 10);
                if (tmp >= 0 && tmp <= 100) {
                    p->deadzone = static_cast<u8>(tmp);
                }
            } else if (strcasecmp(name, "invert_y") == 0) {
                if (strcasecmp(value, "true") == 0)  p->invert_y = true;
                if (strcasecmp(value, "false") == 0) p->invert_y = false;
            }
            return 1;
        }

        bool ParseProfileFile(const char *path, TriggerProfile *out) {
            fs::FileHandle file;
            if (R_FAILED(fs::OpenFile(std::addressof(file), path, fs::OpenMode_Read))) {
                return false;
            }
            ON_SCOPE_EXIT { fs::CloseFile(file); };
            return R_SUCCEEDED(util::ini::ParseFile(file, out, ProfileIniHandler));
        }

        // Filename without ".ini" suffix → e.g. "aabbccddeeff" or "0100b00021c70000".
        // Returns false if the filename doesn't end in .ini or the stem doesn't match
        // the expected hex length.
        bool StripIniExtension(const char *name, char *stem_out, size_t stem_capacity, size_t expected_len) {
            const size_t name_len = std::strlen(name);
            constexpr size_t suffix_len = 4;  // ".ini"
            if (name_len < suffix_len + expected_len) return false;
            if (strcasecmp(name + name_len - suffix_len, ".ini") != 0) return false;
            const size_t stem_len = name_len - suffix_len;
            if (stem_len != expected_len) return false;
            if (stem_len + 1 > stem_capacity) return false;
            std::memcpy(stem_out, name, stem_len);
            stem_out[stem_len] = '\0';
            return true;
        }

        bool ParseHexMac(const char *stem, bluetooth::Address *out) {
            // Expected: 12 lowercase hex chars (matches GetControllerDirectory format).
            char pair[3] = {};
            for (size_t i = 0; i < sizeof(out->address); ++i) {
                pair[0] = stem[i*2];
                pair[1] = stem[i*2 + 1];
                char *endp = nullptr;
                unsigned long byte = std::strtoul(pair, &endp, 16);
                if (endp != pair + 2) return false;
                out->address[i] = static_cast<u8>(byte);
            }
            return true;
        }

        bool ParseHexU64(const char *stem, u64 *out) {
            char *endp = nullptr;
            *out = std::strtoull(stem, &endp, 16);
            return endp == stem + 16;
        }

        // Iterate `dir_path` and call `on_each(stem)` for every file matching `*.ini`
        // with a stem of length `stem_len`. Silently no-ops if the directory can't be
        // opened.
        template<typename F>
        void ForEachIniFile(const char *dir_path, size_t stem_len, F&& on_each) {
            fs::DirectoryHandle dir;
            if (R_FAILED(fs::OpenDirectory(std::addressof(dir), dir_path, fs::OpenDirectoryMode_File))) {
                return;
            }
            ON_SCOPE_EXIT { fs::CloseDirectory(dir); };

            // ReadDirectory one-at-a-time so we don't blow the stack on a large entry.
            fs::DirectoryEntry entry;
            s64 read_count = 0;
            while (R_SUCCEEDED(fs::ReadDirectory(&read_count, &entry, dir, 1)) && read_count > 0) {
                if (entry.type != fs::DirectoryEntryType_File) continue;
                char stem[64];
                if (!StripIniExtension(entry.name, stem, sizeof(stem), stem_len)) continue;

                char full_path[fs::EntryNameLengthMax + 64];
                util::SNPrintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry.name);
                on_each(stem, full_path);
            }
        }

        constinit TriggerMapper g_mapper;

        // Background thread that watches the existing process-switch event and
        // re-runs LoadDirectoryProfiles on each title transition. Runs entirely
        // off the bluetooth input path — fs::OpenDirectory / fs::ReadDirectory
        // happen here, never inside TriggerMapper::Apply.
        constexpr s32    HotReloadThreadPriority  = 21;
        constexpr size_t HotReloadThreadStackSize = 0x4000;
        alignas(os::ThreadStackAlignment) constinit u8 g_hot_reload_stack[HotReloadThreadStackSize];
        constinit os::ThreadType g_hot_reload_thread;
        constinit bool           g_hot_reload_started = false;

        void HotReloadThreadFn(void *) {
            os::Event *event = mc::GetProcessSwitchEvent();
            for (;;) {
                event->Wait();
                TriggerMapper::Instance().LoadDirectoryProfiles();
            }
        }

    }

    TriggerMapper& TriggerMapper::Instance() {
        return g_mapper;
    }

    void TriggerMapper::Initialize(const TriggerProfile& global_profile) {
        std::scoped_lock lk(m_mutex);
        m_global = global_profile;
    }

    void TriggerMapper::LoadDirectoryProfiles() {
        std::scoped_lock lk(m_mutex);
        this->LoadDirectoryProfilesUnsafe();
    }

    void TriggerMapper::LoadDirectoryProfilesUnsafe() {
        m_controllers.clear();
        m_titles.clear();

        ForEachIniFile(ControllersDir, 12 /* hex chars in a MAC */,
            [this](const char *stem, const char *full_path) {
                bluetooth::Address addr;
                if (!ParseHexMac(stem, &addr)) return;
                TriggerProfile profile = m_global;  // start from global, then override
                if (!ParseProfileFile(full_path, &profile)) return;
                m_controllers.push_back({addr, profile});
            });

        ForEachIniFile(TitlesDir, 16 /* hex chars in a Switch programID */,
            [this](const char *stem, const char *full_path) {
                u64 title_id;
                if (!ParseHexU64(stem, &title_id)) return;
                TriggerProfile profile = m_global;
                if (!ParseProfileFile(full_path, &profile)) return;
                m_titles.push_back({title_id, profile});
            });
    }

    void TriggerMapper::StartHotReloadThread() {
        std::scoped_lock lk(m_mutex);
        if (g_hot_reload_started) {
            return;
        }
        g_hot_reload_started = true;
        R_ABORT_UNLESS(os::CreateThread(&g_hot_reload_thread,
            HotReloadThreadFn,
            nullptr,
            g_hot_reload_stack,
            HotReloadThreadStackSize,
            HotReloadThreadPriority
        ));
        os::SetThreadNamePointer(&g_hot_reload_thread, "mc::TriggerMapHotReload");
        os::StartThread(&g_hot_reload_thread);
    }

    const TriggerProfile& TriggerMapper::Resolve(const bluetooth::Address& addr, u64 current_title) const {
        if (current_title != 0) {
            for (const auto& t : m_titles) {
                if (t.title_id == current_title) {
                    return t.profile;
                }
            }
        }
        for (const auto& c : m_controllers) {
            if (std::memcmp(&c.addr, &addr, sizeof(addr)) == 0) {
                return c.profile;
            }
        }
        return m_global;
    }

    void TriggerMapper::Apply(const bluetooth::Address& addr,
                              SwitchButtonData& buttons,
                              SwitchAnalogStick& /* lstick */,
                              SwitchAnalogStick& rstick,
                              u16 left_trigger_norm,
                              u16 right_trigger_norm) {
        std::scoped_lock lk(m_mutex);
        const u64 current_title = mc::GetCurrentProgramId().value;
        const TriggerProfile& p = this->Resolve(addr, current_title);
        if (p.mode == TriggerMode::Off) {
            return;
        }

        const float lt = NormalizeWithDeadzone(left_trigger_norm,  p.deadzone);
        const float rt = NormalizeWithDeadzone(right_trigger_norm, p.deadzone);

        switch (p.mode) {
            case TriggerMode::RstickYSplit: {
                // Capture the physical right-stick Y *before* the trigger-derived
                // value overwrites it — the user can still meaningfully push the
                // stick up/down, we just need to grab it now.
                const u16 physical_y = rstick.GetY();

                float deflection = rt - lt;
                if (p.invert_y) {
                    deflection = -deflection;
                }
                rstick.SetY(DeflectionToY12Bit(deflection));

                bool zr = (rt * 100.0f) >= p.zr_threshold;
                bool zl = (lt * 100.0f) >= p.zl_threshold;

                if (p.stick_y_to_buttons_threshold <= 100) {
                    const float py_norm = (static_cast<int>(physical_y) - static_cast<int>(SwitchAnalogStick::Center))
                                        / static_cast<float>(SwitchAnalogStick::Max - SwitchAnalogStick::Center);
                    if ( py_norm * 100.0f >= p.stick_y_to_buttons_threshold) zr = true;
                    if (-py_norm * 100.0f >= p.stick_y_to_buttons_threshold) zl = true;
                }

                buttons.ZR = zr ? 1 : 0;
                buttons.ZL = zl ? 1 : 0;
                break;
            }
            case TriggerMode::Off:
            default:
                break;
        }
    }

}
