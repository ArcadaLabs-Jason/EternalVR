# Haptics: how the game produces rumble

Retail `DOOMEternalx64vk.exe`, Steam build 25216728. Found with static analysis only (`analysis/haptics`:
Ghidra decompiles, import and caller scans); nothing here has been watched in a running game yet. All
addresses are RVAs in this build. Used by `docs/VR_CONTROLLERS.md` (vibration).

## 1. The pad path

- `XINPUT1_3.dll` is imported by ordinal only: 2 (`XInputGetState`, import slot 0x2A1C8A0) and 3
  (`XInputSetState`, 0x2A1C8A8). No other rumble route exists: no `GetProcAddress` of XInput, no
  DirectInput, Windows.Gaming.Input, SDL, HID output reports or Steam Input API.
- The only caller of `XInputSetState` is `idJoystickWin32::SetRumble(this, pad, low, high, left, right)`
  (0x1DC5850): only with `in_joystick` set, a pad index under 4, the pad connected and the
  `win_joystickRumbleFrameDelay` latch set; low and high are clamped to 0..65535, the trigger values are
  ignored on PC.
- It is fed by an engine job (0x439800) that loops over pads 0..3 and passes zeros unless the user-command
  generator's "joystick active" byte (`usercmdGen + 0xA46`, set by stick or pad events, cleared by keyboard
  or mouse input) is set, `in_joystickRumble` is on, the pad maps to a player and `com_skipJoystickRumble`
  is off (checks at 0x439908..0x43992A, the call at 0x4399C3).

So with keyboard and mouse, or with the layer's user-command input, nothing reaches XInput. The values are
computed all the same, upstream.

## 2. Where rumble is made

- `idRumbleComponent` (type info, 0x1A0 bytes), `idPlayer::rumbleComponent` at +0x48FC0: owner player id
  +0x10, up to 4 active rumbles (`idStaticList<rumbleEntry_t, 4>` at +0x18, 0x58 bytes each), and the
  mixed totals `totalHighMag` +0x190, `totalLowMag` +0x194, `totalLeftMag` +0x198, `totalRightMag` +0x19C
  (floats 0..1).
- Requests: `PlayRumble(this, const idDeclRumble*)` (0xAAE880, about 21 callers: weapons, hands animation
  events, damage, syncs), a positional one (0xAAEB30) and one by value (0xAAECD0: high, high ms, low, low
  ms, then the triggers). They refuse only for a dead player or one player flag; none of the roughly 40
  calling functions reads the input device.
- Mix: `idRumbleComponent::Update` (0xAAF3B0), called without conditions from the player think functions,
  takes the maximum of the active rumbles into the totals every frame.
- Frame output: `idRumbleComponent::GetMagnitudes(this, int* high, int* low, int* left, int* right)`
  (0xAAE730): high and low are the totals times 65535, the triggers times 255, plus
  `rumble_screenShakeToRumbleMultiplier` times the view shake (unless `view_skipShakes`); not clamped. Its
  only caller (0x6A31EE, in the render-view build at 0x6A2C10) runs it for the locally controlled player
  and stores the four values into `gameFrameReturn_t.players[i]` (+0x9BC high, +0x9B8 low, +0x9C0, +0x9C4).
  Just after (0x6A32D1) they are zeroed when the profile's Vibration option (`useVibrate`, +0xE549) is off,
  or a game-rules state is 5 or more (probably multiplayer after the match).

## 3. What the layer uses

An inline detour on `GetMagnitudes` (`src/vkcore/rumble_hook.cpp`), found by its whole 21-byte prologue
and the first read of `totalHighMag`
(`48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 30 F3 0F 10 81 90 01 00 00 49 8B D9`, one
match). It calls the game's function and reads the high and low motors it wrote: device independent, with
the screen shake, and ahead of the game's own Vibration option (the launcher's setting is the one that
counts). A different build fails the signature and the vibration keeps the layer's own events only.

Other points, not used: after `Update` (0xAAF3B0, `40 55 41 56 48 81 EC B8 00 00 00 8B 51 10 48 8B E9 48 8B
0D ?? ?? ?? ??`) the totals at +0x190 / +0x194 without the shake; the call site
(`E8 ?? ?? ?? ?? 41 8B 4E E8 85 C9 75 25 41 39 4E EC 75 1F`); `PlayRumble` (`48 85 D2 0F 84 ?? ?? ?? ?? 48 8B
C4 55 41 56 48 83 EC 48`) for the source of each rumble.

## 4. Open

- Whether the game keeps its last mix while a menu pauses it (the layer plays no game rumble while a menu
  holds gameplay back).
- How strong typical rumbles are: the first one is logged (`haptics: the game's first rumble: low L, high
  H`).
