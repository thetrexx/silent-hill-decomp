# Kenney Input Glyphs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the OPTION2-based gamepad icons and the keyboard text on the native Controls screen with Kenney "Input Prompts Pixel" glyphs: five controller sets plus Auto-detect, and keyboard/mouse glyphs, with light (selected) and dark (unselected) variants.

**Architecture:** A Python script turns Kenney's vendored tilemap into our own 256x512 atlas PNG and a generated C table. A renderer-free, SDL-free module (`pc_input_glyphs.c`) maps bind names, sets and SDL controller info to atlas cells and is unit-tested. A small stateful module (`pc_input_glyphs_apply.c`) registers the atlas as a pinned hi-res override on a CLUT key nobody else uses, detects the controller for Auto, and steps the setting. The Controls screen draws each glyph as a POLY_FT4.

**Tech Stack:** C (C99, MinGW gcc 16 via MSYS2), PsyCross PSX HAL, SDL2 2.32, CMake + Ninja, CTest; Python 3 + Pillow for the atlas script.

**Spec:** `docs/superpowers/specs/2026-10-19-kenney-input-glyphs-design.md` (read it first; this plan argues from it).

## Global Constraints

- Branch `feat/native-menus`. One commit per task, message style `Input glyphs: <what>`, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push to `upstream`.
- Build: `MSYSTEM=MINGW64 "C:/msys64/usr/bin/bash.exe" -lc 'cd /d/GitHub/silent-hill-decomp/pc_port/build && cmake --build . 2>&1'`. Tests: same shell, `cmake .. -DBUILD_TESTING=ON && cmake --build . --target <test> && ctest -R <test> --output-on-failure`.
- Log: newest `pc_port/build/SilentHill_<timestamp>.log` (grep with `-a`). The user runs the game; agents build only.
- Logging only through `SH_DBG(...)`. Comments only for a non-obvious why. No features beyond the spec.
- Glyphs appear only on the native Controls screen. No change to gameplay, title, Achievements, or Quick Options (F10).
- No brand logo glyph anywhere. Guide and any bindable input without a glyph draw the empty round button.
- Selector order and stored values: `0 Auto, 1 PS Classic, 2 PS Modern, 3 Xbox, 4 Nintendo, 5 Steam` in `config.cfg` key `gamepad_icons`. Default Auto. No migration from the old 0..2 numbering (never released).
- Glyph size 16x16 in 320x240 screen space (16x32 in the screen's 448-line mode), long keys 32x16. Never scale the art by a non-integer factor.
- FONT16 dialect for drawn literals: space written `_`, labels must be exact localization keys (`Pc_LangMenuText`).
- Kenney credit: `pc_port/docs/InputGlyphs.md` and his `License.txt` beside the atlas.

## Review Focus

1. **Bind values the tables don't know** (`F13`, `Left GUI`, `Keypad` with nothing after it, empty string, NULL, odd case like `escape`): keyboard cells fall back to today's text, never a wrong glyph, never a crash. Test: `test_glyphs_key_unknown` in Task 2.
2. **Out-of-range `gamepad_icons`** (`-1`, `9`, non-numeric) from a hand-edited or old config: treated as Auto. Test: `test_glyphs_clamp_set` in Task 2; `pc_config.c` uses `InputGlyphs_ClampSet` in Task 4.
3. **Steam Deck with an external pad, and Steam Input's virtual pad** (`0x28DE:0x11FF`): Deck's own controls show Steam glyphs; a PS5 pad behind Steam Input shows PS Modern. Test: `test_glyphs_auto_steam` in Task 2.
4. **Atlas PNG missing or undecodable**: the screen draws text (key names / SDL bind names), never garbage-textured quads from the unbacked VRAM key. Manual check in Task 4 Step 6.
5. **Controller unplugged or swapped while the Controls screen is open in Auto**: glyphs follow on the next frame, no stale set; no controller falls back to Xbox. Test: `test_glyphs_auto_none` in Task 2; manual check in Task 4 Step 6.

---

### Task 1: Atlas script, vendored Kenney art, generated atlas

**Files:**
- Create: `pc_port/tools/kenney_input_prompts_pixel/tilemap_packed.png` (copy of `C:\Users\trexx\Desktop\Temp\kenneys\Icons\Input Prompts Pixel\Tilemap\tilemap_packed.png`)
- Create: `pc_port/tools/kenney_input_prompts_pixel/License.txt` (copy of the pack's `License.txt`)
- Create: `pc_port/tools/build_input_glyphs.py`
- Create (generated, committed): `pc_port/assets/gamedata/icons/input_glyphs.png`, `pc_port/assets/gamedata/icons/License_Kenney.txt`, `pc_port/src/pc_input_glyphs_atlas.inc`

**Interfaces:**
- Produces `pc_input_glyphs_atlas.inc`, included only by `pc_input_glyphs.c`:
  - `enum e_InputGlyph { IG_<NAME>, ..., IG_COUNT };`
  - `static const struct { unsigned char col, row, wide; } s_igAtlas[IG_COUNT];` — `col` 0..7 and `row` 0..31 are the light cell in the atlas's 16x16-pixel grid; `wide` is 1 for a 32x16 glyph (occupies `col` and `col+1`). The dark variant sits at `col + 8`, same row.
- Produces `input_glyphs.png`: exactly 256x512 RGBA, transparent outside glyphs. Columns 0-7 light, 8-15 dark.
- Glyph names (the C code and tests in Task 2 use exactly these; the script defines them):
  - Keyboard: `IG_KEY_A`..`IG_KEY_Z`, `IG_KEY_0`..`IG_KEY_9`, `IG_KEY_F1`..`IG_KEY_F12`, `IG_KEY_ESC`, `IG_KEY_TAB`, `IG_KEY_CAPS`, `IG_KEY_SHIFT`, `IG_KEY_CTRL`, `IG_KEY_ALT`, `IG_KEY_SPACE`, `IG_KEY_ENTER`, `IG_KEY_BACKSPACE`, `IG_KEY_DELETE`, `IG_KEY_INSERT`, `IG_KEY_HOME`, `IG_KEY_END`, `IG_KEY_PAGEUP`, `IG_KEY_PAGEDOWN`, `IG_KEY_PRINT`, `IG_KEY_SCROLL`, `IG_KEY_PAUSE`, `IG_KEY_NUMLOCK`, `IG_KEY_UP`, `IG_KEY_DOWN`, `IG_KEY_LEFT`, `IG_KEY_RIGHT`, and one `IG_KEY_<PUNCT>` per punctuation key the pack has (`MINUS`, `EQUALS`, `PLUS`, `LBRACKET`, `RBRACKET`, `BACKSLASH`, `SEMICOLON`, `APOSTROPHE`, `GRAVE`, `COMMA`, `PERIOD`, `SLASH`, `ASTERISK` — omit any the pack lacks).
  - Mouse: `IG_MOUSE`, `IG_MOUSE_LEFT`, `IG_MOUSE_RIGHT`, `IG_MOUSE_MIDDLE`, `IG_MOUSE_WHEEL_UP`, `IG_MOUSE_WHEEL_DOWN`.
  - Shared pad: `IG_PAD_EMPTY`, `IG_DPAD_UP/RIGHT/DOWN/LEFT`, `IG_PSC_DPAD_UP/RIGHT/DOWN/LEFT` (PS1 4-button d-pad), `IG_LS_UP/RIGHT/DOWN/LEFT`, `IG_L3`, `IG_R3`.
  - PlayStation: `IG_PS_CROSS`, `IG_PS_CIRCLE`, `IG_PS_SQUARE`, `IG_PS_TRIANGLE`, `IG_PS_L1`, `IG_PS_R1`, `IG_PS_L2`, `IG_PS_R2`, `IG_PSC_SELECT`, `IG_PSC_START`, `IG_PS_SHARE`, `IG_PS_OPTIONS`.
  - Xbox: `IG_XBOX_A/B/X/Y`, `IG_XBOX_LB/RB/LT/RT`, `IG_XBOX_VIEW`, `IG_XBOX_MENU`.
  - Nintendo: `IG_NIN_A/B/X/Y`, `IG_NIN_L/R/ZL/ZR`, `IG_NIN_MINUS`, `IG_NIN_PLUS`.
  - Steam: `IG_DECK_A/B/X/Y`, `IG_DECK_L1/R1/L2/R2`, `IG_DECK_VIEW`, `IG_DECK_MENU`.

- [ ] **Step 1: Copy the Kenney tilemap and license into `pc_port/tools/kenney_input_prompts_pixel/`.**

- [ ] **Step 2: Write `build_input_glyphs.py`.**

  Usage: `python pc_port/tools/build_input_glyphs.py` (paths relative to the script). It holds one table: `name -> (light source, dark source, wide)` where a source is a list of `(col, row)` tiles in `tilemap_packed.png` (16x16 each, `col` 0..33, `row` 0..23), drawn over each other in order. The tile picks come from the spec's "Source art" table; pick the exact tiles by looking at the tilemap (render a labelled contact sheet with the script's `--preview <out.png>` flag). Rules the script enforces:
  - PS face buttons: light source `[(17,16),(18,16)]` style pairs (base, then the symbol-half tile), dark from row 17. Same for Circle (19/20), Square (21/22), Cross (23/24).
  - Wide glyphs copy two adjacent tiles as one 32x16 cell.
  - `IG_KEY_ENTER`: take the bottom half of the L-shaped Enter (light `(32,3),(33,3)`, dark `(32,11),(33,11)`), then copy the top outline rows from Backspace's tile of the same variant over its open top edge (the rows above the first fully-opaque row of Backspace's body). Inspect the result in the preview.
  - `IG_KEY_SPACE`: the 16x16 space-symbol key, light `(17,4)`, dark `(17,12)`.
  - Packing: glyphs in table order, left to right in columns 0-7, a wide glyph never straddles column 7/8; dark copy at `col + 8`.
  - Self-checks (script exits non-zero): every glyph's light and dark cell has at least one opaque pixel; no two glyphs share a cell; everything fits in 32 rows.
  - Writes `input_glyphs.png` (256x512), copies `License.txt` to `License_Kenney.txt`, writes `pc_input_glyphs_atlas.inc` with a `/* Generated by pc_port/tools/build_input_glyphs.py. Do not edit. */` header.

- [ ] **Step 3: Run the script and check the output.**

  Run: `python pc_port/tools/build_input_glyphs.py && python pc_port/tools/build_input_glyphs.py --preview "$SCRATCH/glyphs_preview.png"`
  Expected: exit 0; `input_glyphs.png` is 256x512; a second run produces byte-identical outputs (`git status` clean after the first commit). Look at the preview: composited PS buttons whole, Enter closed on top, dark half mirrors light half.

- [ ] **Step 4: Commit.**

```bash
git add pc_port/tools/build_input_glyphs.py pc_port/tools/kenney_input_prompts_pixel pc_port/assets/gamedata/icons/input_glyphs.png pc_port/assets/gamedata/icons/License_Kenney.txt pc_port/src/pc_input_glyphs_atlas.inc
git commit -m "Input glyphs: Kenney atlas and generator"
```

---

### Task 2: Pure glyph mapping module

**Files:**
- Create: `pc_port/include/pc_input_glyphs.h`, `pc_port/src/pc_input_glyphs.c`
- Create: `pc_port/tests/pc_input_glyphs_test.c`
- Modify: `pc_port/CMakeLists.txt` (add test target `pc_input_glyphs_test` next to `pc_pad_icons_test`, sources `tests/pc_input_glyphs_test.c src/pc_input_glyphs.c`, include dir `include`)

**Interfaces:**
- Consumes: `pc_input_glyphs_atlas.inc` (Task 1).
- Produces (header `pc_input_glyphs.h`; the `IG_*` enum is exported by including the `.inc` enum part, or by moving the enum into the header via the generator — implementer's choice, but `IG_*` names must be visible to `pc_controls_screen.c`):

```c
typedef enum { GLYPH_SET_AUTO, GLYPH_SET_PS_CLASSIC, GLYPH_SET_PS_MODERN,
               GLYPH_SET_XBOX, GLYPH_SET_NINTENDO, GLYPH_SET_STEAM, GLYPH_SET_COUNT } e_GlyphSet;
typedef enum { GLYPH_DIR_UP, GLYPH_DIR_RIGHT, GLYPH_DIR_DOWN, GLYPH_DIR_LEFT } e_GlyphDir;

#define INPUT_GLYPH_PX 16 /* one cell in 320x240 screen space */

int         InputGlyphs_ClampSet(int set);                 /* out of range -> GLYPH_SET_AUTO */
const char* InputGlyphs_SetLabel(int set);                 /* "Auto","PS_Classic","PS_Modern","Xbox","Nintendo","Steam" */
int         InputGlyphs_ResolveAuto(int sdlType, int vendor, int product, int steamDeck);
int         InputGlyphs_ForKey(const char* bindName);      /* IG_* or -1 (draw text) */
const char* InputGlyphs_KeySuffix(const char* bindName);   /* "4"/"5" for Mouse4/Mouse5, else NULL */
int         InputGlyphs_ForPad(int set, const char* sdlName); /* IG_*; IG_PAD_EMPTY if no glyph; -1 for NULL */
int         InputGlyphs_ForStick(int set, int dir, int dpad);  /* dpad 0: left stick, 1: d-pad */
void        InputGlyphs_UV(int glyph, int dark, int* u, int* v, int* texW, int* screenW);
```

- `InputGlyphs_ResolveAuto`: `sdlType` is the raw `SDL_GameControllerType` int, or -1 for no controller. Mirror the SDL2 values as local constants in the `.c` (`UNKNOWN 0, XBOX360 1, XBOXONE 2, PS3 3, PS4 4, SWITCH_PRO 5, VIRTUAL 6, PS5 7, ..., JOYCON_LEFT 11, JOYCON_RIGHT 12, JOYCON_PAIR 13`) so the module stays SDL-free. Order: Valve hardware first (vendor `0x28DE` with product `0x1102`, `0x1142` or `0x1205` gives Steam); then `steamDeck && vendor 0x28DE && product 0x11FF && type in {UNKNOWN, XBOX360, XBOXONE, VIRTUAL}` gives Steam; then the type switch (PS3 PS Classic, PS4 and PS5 PS Modern, Xbox 360/One Xbox, Switch Pro and Joy-Cons Nintendo); else Xbox.
- `InputGlyphs_ForKey`: case-insensitive on SDL scancode names (`SDL_GetScancodeName` output, as captured by `pc_bind_input.c:175`): `Return` and `Keypad Enter` are Enter; strip a `Keypad ` prefix and map the rest as the plain key; `Left `/`Right ` + `Shift`/`Ctrl`/`Alt` share the one glyph; `Escape`, `Backspace`, `Tab`, `Space`, `CapsLock`, `PageUp`, `PageDown`, `Delete`, `Insert`, `Home`, `End`, `PrintScreen`, `ScrollLock`, `Pause`, `Numlock`, `Up`/`Down`/`Left`/`Right`, single letters/digits, `F1`..`F12`, punctuation characters; `Mouse1/2/3` left/right/middle, `Mouse4/5` `IG_MOUSE`, `MouseWheelUp/Down` the wheel glyphs.
- `InputGlyphs_ForPad`: `set` Auto is treated as Xbox (callers resolve first). Face buttons positional for PS/Xbox/Steam (`a` bottom, `b` right, `x` left, `y` top); Nintendo by name (`a` draws `IG_NIN_A`, ...). `back`: PS Classic `IG_PSC_SELECT`, PS Modern `IG_PS_SHARE` (Share on PS4, Create on PS5; one glyph), Xbox `IG_XBOX_VIEW`, Nintendo `IG_NIN_MINUS`, Steam `IG_DECK_VIEW`; `start` likewise. `leftstick`/`rightstick` `IG_L3`/`IG_R3`. D-pad: PS Classic uses `IG_PSC_DPAD_*`, others `IG_DPAD_*`. `guide` and any other name: `IG_PAD_EMPTY`.
- `InputGlyphs_UV`: atlas is registered over tpage 7 starting at tpage u 128 (VRAM x 480) at 2x, so a cell is 8 texels: `u = 128 + (col + (dark ? 8 : 0)) * 8`, `v = row * 8`, `texW = wide ? 16 : 8`, `screenW = wide ? 32 : 16`.

- [ ] **Step 1: Write the failing tests in `pc_input_glyphs_test.c`** (same style as `tests/pc_pad_icons_test.c`: `#undef NDEBUG`, `assert`, `main` calls each and prints `ok`).

```c
static void test_glyphs_clamp_set(void)
{
    assert(InputGlyphs_ClampSet(0) == GLYPH_SET_AUTO);
    assert(InputGlyphs_ClampSet(5) == GLYPH_SET_STEAM);
    assert(InputGlyphs_ClampSet(6) == GLYPH_SET_AUTO);
    assert(InputGlyphs_ClampSet(-1) == GLYPH_SET_AUTO);
    assert(InputGlyphs_ClampSet(9) == GLYPH_SET_AUTO);
}
static void test_glyphs_set_label(void)
{
    assert(strcmp(InputGlyphs_SetLabel(GLYPH_SET_PS_CLASSIC), "PS_Classic") == 0);
    assert(strcmp(InputGlyphs_SetLabel(GLYPH_SET_AUTO), "Auto") == 0);
}
static void test_glyphs_auto_types(void)
{
    assert(InputGlyphs_ResolveAuto(4, 0x054C, 0x09CC, 0) == GLYPH_SET_PS_MODERN);
    assert(InputGlyphs_ResolveAuto(7, 0x054C, 0x0CE6, 0) == GLYPH_SET_PS_MODERN);
    assert(InputGlyphs_ResolveAuto(3, 0x054C, 0x0268, 0) == GLYPH_SET_PS_CLASSIC);
    assert(InputGlyphs_ResolveAuto(2, 0x045E, 0x0B12, 0) == GLYPH_SET_XBOX);
    assert(InputGlyphs_ResolveAuto(5, 0x057E, 0x2009, 0) == GLYPH_SET_NINTENDO);
    assert(InputGlyphs_ResolveAuto(13, 0x057E, 0x200E, 0) == GLYPH_SET_NINTENDO);
    assert(InputGlyphs_ResolveAuto(10, 0x0955, 0x7214, 0) == GLYPH_SET_XBOX);
}
static void test_glyphs_auto_steam(void)
{
    assert(InputGlyphs_ResolveAuto(0, 0x28DE, 0x1102, 0) == GLYPH_SET_STEAM);
    assert(InputGlyphs_ResolveAuto(1, 0x28DE, 0x1205, 0) == GLYPH_SET_STEAM);
    assert(InputGlyphs_ResolveAuto(1, 0x28DE, 0x11FF, 1) == GLYPH_SET_STEAM);
    assert(InputGlyphs_ResolveAuto(7, 0x28DE, 0x11FF, 1) == GLYPH_SET_PS_MODERN);
    assert(InputGlyphs_ResolveAuto(7, 0x28DE, 0x11FF, 0) == GLYPH_SET_PS_MODERN);
    assert(InputGlyphs_ResolveAuto(1, 0x28DE, 0x11FF, 0) == GLYPH_SET_XBOX);
}
static void test_glyphs_auto_none(void)
{
    assert(InputGlyphs_ResolveAuto(-1, 0, 0, 0) == GLYPH_SET_XBOX);
    assert(InputGlyphs_ResolveAuto(-1, 0, 0, 1) == GLYPH_SET_XBOX);
}
static void test_glyphs_key(void)
{
    assert(InputGlyphs_ForKey("A") == IG_KEY_A);
    assert(InputGlyphs_ForKey("Return") == IG_KEY_ENTER);
    assert(InputGlyphs_ForKey("Keypad Enter") == IG_KEY_ENTER);
    assert(InputGlyphs_ForKey("Keypad 5") == IG_KEY_5);
    assert(InputGlyphs_ForKey("Left Shift") == IG_KEY_SHIFT);
    assert(InputGlyphs_ForKey("Right Shift") == IG_KEY_SHIFT);
    assert(InputGlyphs_ForKey("Right Ctrl") == IG_KEY_CTRL);
    assert(InputGlyphs_ForKey("escape") == IG_KEY_ESC);
    assert(InputGlyphs_ForKey("Space") == IG_KEY_SPACE);
    assert(InputGlyphs_ForKey("Up") == IG_KEY_UP);
    assert(InputGlyphs_ForKey("F12") == IG_KEY_F12);
}
static void test_glyphs_key_unknown(void)
{
    assert(InputGlyphs_ForKey("F13") == -1);
    assert(InputGlyphs_ForKey("Left GUI") == -1);
    assert(InputGlyphs_ForKey("Keypad ") == -1);
    assert(InputGlyphs_ForKey("") == -1);
    assert(InputGlyphs_ForKey(NULL) == -1);
}
static void test_glyphs_mouse(void)
{
    assert(InputGlyphs_ForKey("Mouse1") == IG_MOUSE_LEFT);
    assert(InputGlyphs_ForKey("Mouse3") == IG_MOUSE_MIDDLE);
    assert(InputGlyphs_ForKey("MouseWheelDown") == IG_MOUSE_WHEEL_DOWN);
    assert(InputGlyphs_ForKey("Mouse4") == IG_MOUSE);
    assert(strcmp(InputGlyphs_KeySuffix("Mouse5"), "5") == 0);
    assert(InputGlyphs_KeySuffix("Mouse1") == NULL);
    assert(InputGlyphs_KeySuffix(NULL) == NULL);
}
static void test_glyphs_pad(void)
{
    assert(InputGlyphs_ForPad(GLYPH_SET_XBOX, "a") == IG_XBOX_A);
    assert(InputGlyphs_ForPad(GLYPH_SET_PS_MODERN, "a") == IG_PS_CROSS);
    assert(InputGlyphs_ForPad(GLYPH_SET_PS_MODERN, "b") == IG_PS_CIRCLE);
    assert(InputGlyphs_ForPad(GLYPH_SET_NINTENDO, "a") == IG_NIN_A);
    assert(InputGlyphs_ForPad(GLYPH_SET_NINTENDO, "b") == IG_NIN_B);
    assert(InputGlyphs_ForPad(GLYPH_SET_PS_CLASSIC, "back") == IG_PSC_SELECT);
    assert(InputGlyphs_ForPad(GLYPH_SET_PS_MODERN, "back") == IG_PS_SHARE);
    assert(InputGlyphs_ForPad(GLYPH_SET_NINTENDO, "start") == IG_NIN_PLUS);
    assert(InputGlyphs_ForPad(GLYPH_SET_STEAM, "lefttrigger") == IG_DECK_L2);
    assert(InputGlyphs_ForPad(GLYPH_SET_XBOX, "rightshoulder") == IG_XBOX_RB);
    assert(InputGlyphs_ForPad(GLYPH_SET_PS_CLASSIC, "dpup") == IG_PSC_DPAD_UP);
    assert(InputGlyphs_ForPad(GLYPH_SET_XBOX, "dpleft") == IG_DPAD_LEFT);
    assert(InputGlyphs_ForPad(GLYPH_SET_XBOX, "guide") == IG_PAD_EMPTY);
    assert(InputGlyphs_ForPad(GLYPH_SET_PS_MODERN, "guide") == IG_PAD_EMPTY);
    assert(InputGlyphs_ForPad(GLYPH_SET_AUTO, "a") == IG_XBOX_A);
    assert(InputGlyphs_ForPad(GLYPH_SET_XBOX, NULL) == -1);
}
static void test_glyphs_stick(void)
{
    assert(InputGlyphs_ForStick(GLYPH_SET_XBOX, GLYPH_DIR_UP, 0) == IG_LS_UP);
    assert(InputGlyphs_ForStick(GLYPH_SET_XBOX, GLYPH_DIR_LEFT, 1) == IG_DPAD_LEFT);
    assert(InputGlyphs_ForStick(GLYPH_SET_PS_CLASSIC, GLYPH_DIR_DOWN, 1) == IG_PSC_DPAD_DOWN);
}
static void test_glyphs_uv(void)
{
    int u, v, tw, sw, ud, vd;

    InputGlyphs_UV(IG_KEY_A, 0, &u, &v, &tw, &sw);
    InputGlyphs_UV(IG_KEY_A, 1, &ud, &vd, &tw, &sw);
    assert(ud == u + 64 && vd == v);
    assert(u >= 128 && u < 128 + 64 && v >= 0 && v < 256 && u % 8 == 0 && v % 8 == 0);
    assert(tw == 8 && sw == 16);
    InputGlyphs_UV(IG_KEY_SHIFT, 0, &u, &v, &tw, &sw);
    assert(tw == 16 && sw == 32);
    InputGlyphs_UV(IG_KEY_ENTER, 0, &u, &v, &tw, &sw);
    assert(tw == 16 && sw == 32);
    InputGlyphs_UV(IG_KEY_SPACE, 0, &u, &v, &tw, &sw);
    assert(tw == 8 && sw == 16);
}
```

- [ ] **Step 2: Run the test to verify it fails.**
  Run: `cmake .. -DBUILD_TESTING=ON && cmake --build . --target pc_input_glyphs_test`
  Expected: build error, `pc_input_glyphs.h` / `InputGlyphs_*` undefined.

- [ ] **Step 3: Implement `pc_input_glyphs.h` and `pc_input_glyphs.c`** per the Interfaces block. Tables, no SDL, no game state; header comment points to the spec and `pc_port/docs/InputGlyphs.md`.

- [ ] **Step 4: Run the test to verify it passes.**
  Run: `cmake --build . --target pc_input_glyphs_test && ctest -R pc_input_glyphs_test --output-on-failure`
  Expected: `100% tests passed`.

- [ ] **Step 5: Commit.**

```bash
git add pc_port/include/pc_input_glyphs.h pc_port/src/pc_input_glyphs.c pc_port/tests/pc_input_glyphs_test.c pc_port/CMakeLists.txt
git commit -m "Input glyphs: bind, set and auto-detect mapping"
```

---

### Task 3: Atlas registration, Auto detection, setting step

**Files:**
- Create: `pc_port/src/pc_input_glyphs_apply.c` (declarations go in `pc_input_glyphs.h` under a "game side" block)

**Interfaces:**
- Consumes: `InputGlyphs_ClampSet`, `InputGlyphs_ResolveAuto`, `InputGlyphs_SetLabel` (Task 2); `HiresOverride_DecodeToRGBA`, `HiresOverride_RegisterRGBAKeyed`, `HiresOverride_PinVramRect`, `HiresOverride_SetForceNearestUpload` (`include/hires_override.h`); `PcConfig_SaveKeyValue`; `g_PcConfig.gamepadIcons`.
- Produces:

```c
#define INPUT_GLYPHS_CLUT_X 1008
#define INPUT_GLYPHS_CLUT_Y 511
void InputGlyphs_Load(void);      /* idempotent; registers gamedata/icons/input_glyphs.png */
int  InputGlyphs_Ready(void);     /* 1 once the atlas is registered */
int  InputGlyphs_ActiveSet(void); /* concrete set: the setting, or Auto resolved now */
void InputGlyphs_Step(int dir);   /* cycle gamepad_icons, save */
```

- [ ] **Step 1: Implement `InputGlyphs_Load`.** Read and decode `gamedata/icons/input_glyphs.png`; require exactly 256x512. Register with `HiresOverride_RegisterRGBAKeyed("input glyphs", rgba, 256, 512, 480, 0, 32, 256, INPUT_GLYPHS_CLUT_X, INPUT_GLYPHS_CLUT_Y, 4, 0)` wrapped in `HiresOverride_SetForceNearestUpload(1/0)`, then `HiresOverride_PinVramRect(..., 1)`, following the PAL font grid at `src/font_region.c:1738-1743`. Why this key: the override is looked up by (tpage, CLUT), so a CLUT cell no TIM uses (the last cell in VRAM) gives our prims their own texture over OPTION2's pixel rect without writing VRAM and without touching OPTION2's own lookups; pinning keeps OPTION2 uploads from invalidating it. Log once: `SH_DBG("[GLYPHS] atlas %s", ok ? "registered" : "<reason>, drawing text")`. Load only once per run (success or failure).

- [ ] **Step 2: Implement `InputGlyphs_ActiveSet`.** If `InputGlyphs_ClampSet(g_PcConfig.gamepadIcons)` is not Auto, return it. Otherwise take the first `i < SDL_NumJoysticks()` with `SDL_IsGameController(i)` and pass `SDL_GameControllerTypeForIndex(i)`, `SDL_JoystickGetDeviceVendor(i)`, `SDL_JoystickGetDeviceProduct(i)`, and `getenv("SteamDeck")` equal to `"1"` into `InputGlyphs_ResolveAuto`; no such device passes `sdlType -1`. Called every frame by the screen; no caching, so hotplug just works. Log a `[GLYPHS] auto: <label>` line only when the resolved set changes.

- [ ] **Step 3: Implement `InputGlyphs_Step`** with the same wrap-around and `PcConfig_SaveKeyValue("gamepad_icons", ...)` as `PadIcons_Step` in `pc_pad_icons_apply.c:142`.

- [ ] **Step 4: Build.**
  Run the build command. Expected: links clean, no new warnings. (Nothing calls the module yet.)

- [ ] **Step 5: Commit.**

```bash
git add pc_port/include/pc_input_glyphs.h pc_port/src/pc_input_glyphs_apply.c
git commit -m "Input glyphs: atlas registration and controller detection"
```

---

### Task 4: Controls screen draws the glyphs; old icon sets removed

**Files:**
- Modify: `pc_port/src/pc_controls_screen.c` (`cs_icon_draw`, `cs_pad_draw`, `cs_stick_icon`, `cs_cell_draw`, the Gamepad Icons row in `Pc_ControlsScreen_Draw`, `cs_value_left`, the `PadIcons_Step` calls, `Pc_ControlsScreen_Open`)
- Modify: `pc_port/src/pc_config.c:7,64,761-764`, `pc_port/include/pc_config.h:183-184`
- Modify: `src/main/fsqueue_3.c:13,1836-1841` (remove the include and the `PadIcons_AtlasUploaded` hook)
- Modify: `pc_port/CMakeLists.txt` (remove `pc_pad_icons_test`)
- Delete: `pc_port/include/pc_pad_icons.h`, `pc_port/src/pc_pad_icons.c`, `pc_port/src/pc_pad_icons_apply.c`, `pc_port/tests/pc_pad_icons_test.c`, `pc_port/assets/gamedata/icons/pad_xbox.png`, `pc_port/assets/gamedata/icons/pad_nintendo.png`

**Interfaces:**
- Consumes: everything in Tasks 2-3.

- [ ] **Step 1: Open hook.** `Pc_ControlsScreen_Open` calls `InputGlyphs_Load()`.

- [ ] **Step 2: Glyph primitive.** Replace `cs_icon_draw` with `static void cs_glyph_draw(int glyph, int dark, int x, int y, int rgb)` drawing one `POLY_FT4` (not SPRT: the atlas is 2x, so 8 texels cover 16 px). UVs from `InputGlyphs_UV`; screen size `screenW x INPUT_GLYPH_PX` in 320x240 space, with the same 448-line y transform `cs_icon_draw` uses today (height doubles there). `tpage = getTPage(0, 0, 448, 0)` (tpage 7, 4bpp), `clut = getClut(INPUT_GLYPHS_CLUT_X, INPUT_GLYPHS_CLUT_Y)`, same OT slot `CS_OT_ICON`. Start with `CS_ICON_DY 0`; the user tunes spacing in playtest.

- [ ] **Step 3: Cell rules in `cs_cell_draw` / `cs_pad_draw`.**
  - Set for the controller columns: `InputGlyphs_ActiveSet()`, once per frame in `Pc_ControlsScreen_Draw`.
  - Selected cell: light glyph, rgb 128/128/128. Other cells: dark glyph; duplicate tint 128/0/0; disabled row and Move-row stick cells 64/64/64 (today's `CS_RGB_DIM`).
  - Keyboard columns: `InputGlyphs_ForKey(v)`; for `Mouse4/5` draw the glyph then `InputGlyphs_KeySuffix(v)` as `NM_TEXT_TALL` text 1 px right of it, same colour id as the cell. `-1` keeps today's text path (`BindCore_DisplayKey` + `cs_fit`).
  - Controller columns: `InputGlyphs_ForPad(set, v)`.
  - Move rows: `cs_stick_icon` returns `InputGlyphs_ForStick(set, dir, col != BC_PAD1)`.
  - `!InputGlyphs_Ready()`: keyboard as today's text; controller cells draw `v` itself as text (sanitized, fitted). Listening `...` and unbound `-` unchanged.

- [ ] **Step 4: Gamepad Icons row.** Value is `InputGlyphs_SetLabel(g_PcConfig.gamepadIcons)`; when that is Auto, draw `Auto` and then `(<resolved label>)` as two separate strings (so `Auto` stays an exact localization key), right-aligned together at `CS_VALUE_RX`, and make `cs_value_left` return the left edge of the pair so the arrows and mouse hit-test follow. `PadIcons_Step` calls become `InputGlyphs_Step`.

- [ ] **Step 5: Config and removals.** `pc_config.c` includes `pc_input_glyphs.h`, defaults `gamepadIcons` to `GLYPH_SET_AUTO`, parses with `InputGlyphs_ClampSet(atoi(...))`; update the `pc_config.h` comment (`e_GlyphSet: 0 Auto, 1 PS Classic, 2 PS Modern, 3 Xbox, 4 Nintendo, 5 Steam`). Remove the fsqueue hook and include, the old files, assets, and test target listed above. `grep -rn "PadIcons_\|pc_pad_icons\|PAD_ICON" pc_port/src pc_port/include src` returns nothing.

- [ ] **Step 6: Build, test, hand to the user.**
  Run the build and `ctest --output-on-failure` (all tests). Expected: clean build, all tests pass.
  Then ask the user to playtest the Controls screen and check: every set by hand; Auto with their pad (and unplugged: Xbox); selected light vs dark; red duplicate; dim Move-row stick cells; long keys and Enter; Mouse4/5 digit; spacing between rows; and once with `gamedata/icons/input_glyphs.png` renamed (text, no garbage quads). Check the log for `[GLYPHS] atlas registered`.

- [ ] **Step 7: Commit.**

```bash
git add -A pc_port/src pc_port/include pc_port/tests pc_port/CMakeLists.txt pc_port/assets/gamedata/icons src/main/fsqueue_3.c
git commit -m "Input glyphs: draw Kenney glyphs on the Controls screen"
```

---

### Task 5: Localization and docs

**Files:**
- Modify: `pc_port/src/lang_menu.c` (around line 413), `pc_port/src/lang_ru_menu.inc` (around 298), `pc_port/src/lang_jpn_pcopt.inc` (around 85), `pc_port/localization/extract_text.py` (around 503), `pc_port/localization/pl_supplement.py`, `pt_supplement.py`, `ru_supplement.py`, then regenerate `SilentHill_EN_for_translation.txt`, `SilentHill_EN_raw.json`, `SilentHill_RU_translation.txt`, `assets/gamedata/lang/{pl,pt,ru}.lang` the way commit `4ed8a28cc` did
- Create: `pc_port/docs/InputGlyphs.md`; Delete: `pc_port/docs/PadIcons.md`
- Modify: `docs/superpowers/native-menus-TODO.md`

- [ ] **Step 1: Keys.** Remove `Original`. Add `PS_Classic`, `PS_Modern`, `Steam` as brand names (untranslated, like `Xbox`/`Nintendo`), except Russian, which transliterates like `Иксбокс`: `ПС_Классик`, `ПС_Модерн`, `Стим`. Add `Auto` where the menu tables lack it (`lang_menu.c` has none; reuse the wording PC Options already uses in `lang_quick_pal.inc:74` and `lang_jpn_pcopt.inc:56`). Translator notes in `extract_text.py`: `Gamepad_Icons` value list updated; `Auto` "Gamepad Icons value: follows the connected controller".

- [ ] **Step 2: Regenerate and check.** Run the regeneration used in `4ed8a28cc` and `python pc_port/tools/check_langs.py`. Expected: no missing keys reported for the new strings.

- [ ] **Step 3: Docs.** `InputGlyphs.md`: what the setting does, Auto rules, the atlas layout (16x16 cells, light 0-7 / dark 8-15, 256x512), how to repaint (edit `input_glyphs.png` in place, or change picks in `build_input_glyphs.py` and rerun), fallbacks, the `[GLYPHS]` log lines, and the Kenney credit (CC0, www.kenney.nl). In the TODO doc, move item 3 (Kenney glyphs) to done, drop the pending pad art section, and drop the deferred items the glyphs made moot (long key names cut off, Xbox-to-Original override drop).

- [ ] **Step 4: Build.** Expected: clean.

- [ ] **Step 5: Commit.**

```bash
git add -A pc_port/src pc_port/localization pc_port/assets/gamedata/lang pc_port/docs docs/superpowers/native-menus-TODO.md
git commit -m "Input glyphs: localization and docs"
```
