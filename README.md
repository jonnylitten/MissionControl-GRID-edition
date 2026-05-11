# Mission Control — analog-trigger fork

A fork of [ndeadly/MissionControl](https://github.com/ndeadly/MissionControl) that adds **configurable analog-trigger → stick remapping** for Bluetooth controllers on Nintendo Switch.

It exists for one specific kind of pain: games like **Grid Autosport** read throttle / brake pressure off the right-stick analog Y axis, but third-party Bluetooth controllers don't natively expose their triggers as a stick. With this fork you can tell MissionControl "for *this* game, treat RT/LT as right-stick up/down" and the game just works.

## What's added

- A new `[trigger_map]` section in `missioncontrol.ini` with a `mode = rstick_y_split` setting that maps RT → +Y and LT → −Y on the right stick.
- Per-title and per-controller override files at `sdmc:/config/MissionControl/titles/<programID>.ini` and `sdmc:/config/MissionControl/controllers/<MAC>.ini`. Precedence is per-title > per-controller > global.
- An optional "physical right-stick Y → digital ZR / ZL" mode on top of `rstick_y_split` — since the trigger is now driving the stick Y, the physical stick's up/down is otherwise wasted, so it can be repurposed for things like gear shifts.
- All controllers that don't opt in fall through to **stock MissionControl behaviour**, so this should be a drop-in replacement for users who don't touch the new settings.

## Status

- **Hardware-verified**: DualShock 4, on a [GameSir G8 Galileo Plus](https://www.gamesir.hk/products/gamesir-g8-galileo) in handheld mode (the G8 emulates a DS4 — giving the Switch analog triggers in handheld, which it doesn't have natively).
- **Untested**: every other controller. They simply don't opt in, so they keep working exactly like vanilla MissionControl. PRs welcome.

## Install

1. Grab the latest `.zip` from [Releases](../../releases) and merge its contents onto your SD card (overwrites the MissionControl files under `/atmosphere/contents/010000000000bd00/`).
2. Reboot.
3. **Launch Grid Autosport — it just works.** The release ships a per-title profile for Grid (`sdmc:/config/MissionControl/titles/0100dc800a602000.ini`), so analog throttle/brake on the right-stick Y is enabled out of the box. Every other game falls through to stock MissionControl behaviour.

If you've never installed MissionControl before, follow the upstream install guide in [README-upstream.md](README-upstream.md) — this fork installs identically.

To add the feature for other games, drop more files under `sdmc:/config/MissionControl/titles/` — see [Configure](#configure) below for the schema and [`presets/titles/`](presets/titles/) for the bundled Grid example.

## Configure

### Global default

In `sdmc:/config/MissionControl/missioncontrol.ini`:

```ini
[trigger_map]
mode = rstick_y_split        ; off | rstick_y_split  (default off)
zr_threshold = off           ; 0..100 % of trigger before ZR fires digitally, or "off"
zl_threshold = off           ; same for ZL
deadzone = 0                 ; 0..100 % deadzone on raw trigger
invert_y = false             ; flip RT and LT directions
stick_y_to_buttons_threshold = off   ; 0..100 % stick deflection for "physical-stick → digital ZR/ZL", or "off"
```

Defaults are conservative — `mode = off` everywhere ships unchanged behaviour.

### Per-title overrides

`sdmc:/config/MissionControl/titles/<16-hex-program-id>.ini`. Same `[trigger_map]` schema. Profile-level precedence — a per-title file is used in full, fields are not merged with the global.

Example — Grid Autosport (`0100dc800a602000`):

```ini
[trigger_map]
mode = rstick_y_split
stick_y_to_buttons_threshold = 50
```

You can find a game's program ID in your overlay menu (Tesla / EdiZon) or via Goldleaf / DBI.

### Per-controller overrides

`sdmc:/config/MissionControl/controllers/<lowercase-mac-no-separators>.ini`, e.g. `aabbccddeeff.ini`. Same schema. Per-title beats per-controller if both match.

## How it works

Each per-controller subclass in MissionControl already has a `ProcessInputData` method that converts the raw HID report into a Switch Pro Controller report. This fork adds two raw-trigger member variables (`m_left_trigger_raw`, `m_right_trigger_raw`) and a base-class opt-in flag. Subclasses that opt in (currently just DS4) populate the trigger values; `EmulatedSwitchController::UpdateControllerState` then calls a new `TriggerMapper` after `ProcessInputData`, which resolves the active profile (per-title → per-controller → global) and applies it. The current title is read from MissionControl's existing `mcmitm_process_monitor`, so per-title profiles take effect on the next packet after a title switch with no extra event subscription. The override directories are also re-scanned on every title switch, so newly-added per-title or per-controller ini files take effect on next launch without a sysmodule restart.

The whole feature is contained in:
- `mc_mitm/source/controllers/trigger_mapper.{hpp,cpp}` (new module)
- A handful of additive lines in `emulated_switch_controller.{hpp,cpp}`, `dualshock4_controller.cpp`, `mcmitm_config.{hpp,cpp}`, `config.ini`, and the `Makefile` `dist` target.

## License

GPL-2.0, same as upstream MissionControl. See `LICENSE`.

## Disclaimer

Custom-firmware sysmodules like this can put your Switch in a bad state if misused. Use at your own risk. Not affiliated with Nintendo, the Atmosphère project, or upstream MissionControl. Distributed under GPL-2.0 (see [LICENSE](LICENSE)) — no warranty, express or implied.

## Credits

All of MissionControl's heavy lifting is **ndeadly's** — this fork is a thin feature on top. The trigger→stick idea originated in [ndeadly/MissionControl#1006](https://github.com/ndeadly/MissionControl/issues/1006) as a proof-of-concept patch; this fork makes it configurable per-game and per-controller, and adds the "stick Y → digital ZR/ZL" repurposing.

Built collaboratively with Claude (Anthropic).
