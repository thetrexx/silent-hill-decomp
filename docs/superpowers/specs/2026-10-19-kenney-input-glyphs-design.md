# Kenney Input Glyphs — Design

Date: 2026-10-19
Branch: `feat/native-menus` (follow-up to `2026-10-18-native-menus-design.md`)
Status: draft, awaiting user approval

## Intent

The native Controls screen shows controller binds as 12x12 icons laid out like the
disc's `TIM/OPTION2.TIM` button sheet, and keyboard and mouse binds as text. That
sheet only knows the twelve PlayStation buttons. Everything else (d-pad and stick
directions, other controller families) needed art the user would have to paint, and
long key names get cut off (`Esc...`, `L-S...`).

Kenney's **Input Prompts Pixel** pack (CC0) covers every controller family we care
about and the whole keyboard and mouse in one consistent pixel-art style. This
project replaces the OPTION2-based icon sets with glyphs from that pack, for both the
controller and the keyboard columns. The result should look cleaner and read faster,
and it removes the pending art work on the user's side.

## Goals

1. The Controls screen draws **every** bind cell as a Kenney glyph: keyboard keys,
   mouse buttons and the mouse wheel in the keyboard columns, controller buttons in the
   controller columns, and the left-stick / d-pad directions in the Move rows.
2. Controller glyph sets: **PS Classic, PS4, PS5, Xbox, Nintendo, Steam**.
3. The "Gamepad Icons" selector at the top of the Controls screen picks the set. It
   gains an **Auto** value that follows the connected controller.
4. **Light / dark variants:** the selected cell uses Kenney's light glyph and every
   other cell uses the dark glyph (user approved).

## Non-goals

- Glyphs anywhere outside the native Controls screen. Gameplay, the title screen,
  Achievements and Quick Options (F10, which keeps the GL bind panel) are unchanged.
- New art. Gaps are covered by the fallbacks below. The user may repaint later
  (PS Circle, separate left/right modifier keys).
- Keeping the disc OPTION2 icons as an option. Kenney's PS Classic set replaces
  "Original" (user decision: the disc sheet cannot cover the screen).

## Anti-goals

- Any brand logo glyph (PlayStation, Xbox, Steam, Nintendo Home logos). Avoid
  copyright trouble.
- Glyphs that blur, shimmer or get uneven pixels from non-integer scaling. They must
  read as crisp pixel art at the game's normal internal resolutions.
- Breaking what the Controls screen does today: selection, mouse hit-testing,
  duplicate warning, disabled rows, listening state, scroll.

## Source art

- Pack: `C:\Users\trexx\Desktop\Temp\kenneys\Icons\Input Prompts Pixel\`
  (`Tilemap/tilemap_packed.png`, 544x384, 34x24 tiles of 16x16, no spacing; also
  `Tiles/tile_NNNN.png`, numbered row-major, `index = row * 34 + col`).
- License: CC0. Credit to Kenney (www.kenney.nl) is optional; we credit him anyway in
  `pc_port/docs` and ship his `License.txt` next to the atlas.
- Every glyph exists in a light and a dark variant (usually the row directly below,
  or the dark keyboard block at rows 8-15 below the light block at rows 0-7).

Tile coordinates below are `col,row` in `tilemap_packed.png`.

| Area | Tiles |
|---|---|
| Xbox coloured A/B/X/Y | 0-3,0 (and alternatives 4-11,0) |
| Nintendo A/B/X/Y letters | 13-16,0-1 |
| D-pad, one direction highlighted | rows 1-4, cols 0-7 |
| PS1-style 4-button d-pad | rows 7-12, cols 0-7 |
| Left/right stick, directions and click | rows 4-15, cols 8-16 (L/R labelled) |
| Mouse: body, left, right, middle, wheel up/down | 8-14,2-3 |
| Keyboard, light | rows 0-7, cols 17-33 |
| Keyboard, dark | rows 8-15, cols 17-33 |
| PS face buttons (two halves) | 17-24,16-17 |
| Shoulders and triggers (LB/RB/LT/RT, L1/R1/L2/R2, ZL/ZR/L/R) | rows 16-19, cols 7-26 |
| Xbox View / Menu, PS Share / Options | 4-6,18-19 |
| Nintendo +/- and Home | 3-5,20-21 |
| PS Classic Select / Start (slanted pills) | 18-19,20-21 |
| Steam Deck View / Menu / `...` | 19-21,22-23 |
| Empty round button | 12,0 (light) / 12,1 (dark) |

The tile picks for each set are the builder's to finalise from the tilemap, within the
families above.

### Art preparation

- **PS face buttons** are split on purpose: a base tile with the left half of the
  symbol, and a symbol-only tile with the right half. Drawing tile 18 over 17 (20 over
  19, 22 over 21, 24 over 23) gives the whole button. Pre-composite them when the
  atlas is built. No manual edit is needed.
- **Long keys** (Shift, Ctrl, Alt, Tab, Caps, Del, End, Home, PgUp, PgDn, Insert,
  Print, Scroll, Pause, Num, Backspace) are two tiles wide in the packed tilemap.
  Keep them as one **32x16** atlas entry and draw them as one sprite. Glyph entries
  therefore carry a width (16 or 32).
- **Enter** is a 32x32 L-shaped key in Kenney's sheet (cols 32-33, rows 2-3, and the
  dark copy). Use only its **bottom half** (row 3; the 32x16 part with the return
  arrow) and have the atlas script close its open top edge by copying the top outline
  rows from Backspace's tile of the same variant. (User decision.)
- **Space** uses the 16x16 space-symbol key (col 17, row 4, and the dark copy), not the
  48x16 space bar, which would fill a keyboard column edge to edge. (My call.)
- The atlas we ship is generated from Kenney's tilemap by a small script checked in
  next to it, so the tile picks are reviewable and the atlas can be rebuilt. The
  generated PNG is committed; the build does not run the script.

## Glyph sets and the selector

Selector values, in this order: **Auto, PS Classic, PS4, PS5, Xbox, Nintendo, Steam**.
Auto is the default. The value is stored in `config.cfg` as `gamepad_icons` (new
numbering; the old Original/Xbox/Nintendo values were never released, so there is no
migration).

| Set | Face buttons | Shoulders / triggers | Select / Start | D-pad |
|---|---|---|---|---|
| PS Classic | composited △○□✕ | L1/R1/L2/R2 | slanted Select / Start pills | PS1 4-button d-pad |
| PS4 | composited △○□✕ | L1/R1/L2/R2 | Share / Options | cross d-pad |
| PS5 | composited △○□✕ | L1/R1/L2/R2 | Create (Share glyph) / Options | cross d-pad |
| Xbox | coloured A/B/X/Y | LB/RB/LT/RT | View / Menu | cross d-pad |
| Nintendo | A/B/X/Y letters | L/R/ZL/ZR | - / + | cross d-pad |
| Steam | Steam Deck style A/B/X/Y | L1/R1/L2/R2 (Deck labels) | Deck View / Menu | cross d-pad |

Face buttons for the PlayStation, Xbox and Steam sets map by position, as today: SDL
`a` (bottom) is Cross / Xbox A, `b` (right) is Circle / B, `x` (left) is Square / X,
`y` (top) is Triangle / Y.

The **Nintendo set follows SDL's button name** instead: `a` draws A, `b` draws B,
`x` draws X, `y` draws Y. PsyCross leaves `SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS` at
SDL2's default (on), so a real Switch pad reports its A button (right) as `a`, and the
glyph matches the button the player pressed. Accepted trade-off: picking Nintendo by
hand while holding an Xbox or PS pad shows letters that do not match that pad's
positions. No PsyCross change. (User decision.)

### Auto

- **Auto** shows the set for the controller the game is reading input from.
  `PsyX_Pad_ConnectedControllerType()` already returns the SDL type of the first
  attached controller.
- SDL type to set:
  - `PS4` to PS4, `PS5` to PS5, `PS3` to PS Classic.
  - `XBOX360`, `XBOXONE` to Xbox.
  - `NINTENDO_SWITCH_PRO` and the Joy-Con types to Nintendo.
  - **Real Valve hardware** to Steam: a Steam Controller (vendor `0x28DE`, its own
    product IDs), or a Steam Deck (the `SteamDeck=1` environment variable SteamOS sets
    in Game Mode). SDL has no Steam type, so this check comes before the type switch.
  - **Steam Input's virtual pad** (vendor `0x28DE`, product `0x11FF`) is not Valve
    hardware. Use the SDL type SDL reports for it, which reflects the physical pad when
    Steam provides that info, so a PS5 pad launched through Steam still gets PS5
    glyphs. (My call: Auto shows the physical controller's glyphs.)
  - Anything else, or no controller attached, to Xbox.
- The row shows `Auto (<set>)`, e.g. `Auto (PS5)`, so the player sees what was
  detected. (My call.)
- Plugging in a different controller while the screen is open switches the glyphs.
- Picking any other value by hand overrides detection until the player picks Auto
  again.

## Keyboard and mouse glyphs

The keyboard columns always use the keyboard / mouse glyphs, whatever the selector says.

- Every key with a Kenney glyph draws that glyph.
- **Numpad keys** (`Keypad ...`) reuse the normal key glyphs: `Keypad 5` draws 5,
  `Keypad Enter` draws Enter, `Keypad +` draws +. (User decision.)
- Enter and Space use the prepared glyphs from "Art preparation" above.
- **Left / Right Shift, Ctrl, Alt** share one glyph each. (User decision; new art may
  come later.)
- **Mouse1/2/3** draw the mouse with the left / right / middle button highlighted.
  **MouseWheelUp/Down** draw the wheel glyphs.
- **Mouse4 / Mouse5** draw the plain mouse glyph with a `4` or `5` beside it, in the
  game font. (User decision.)
- A key with no Kenney glyph (e.g. F13+, the Application key) falls back to today's
  text drawing. (My call.)

## Controller fallbacks

- Bindable controller inputs are the launcher's set: face buttons, back / guide /
  start, stick clicks, shoulders, d-pad, triggers.
- **Guide** has no logo glyph in any set. It draws the **empty round button**.
  (User decision.)
- Any other bindable input without a glyph in the chosen set also draws the empty round
  button. (My call, extending the user's rule.)
- The Move rows' stick cells draw the left-stick direction glyph and the d-pad
  direction glyph for their direction, from the active set.

## Drawing rules

- **Selected cell:** light glyph, no tint. The gold underline under the row still marks
  the selected row.
- **All other cells:** dark glyph.
- **Duplicate bind:** dark glyph tinted red, as today.
- **Disabled row** and the Move rows' fixed stick cells: dark glyph dimmed, as today.
- **Listening** (`...`) and **unbound** (`-`) stay as text.
- Glyph size: **16x16** in the screen's 320x240 coordinate space (32 tall in the
  448-line mode the screen runs in), so Kenney's square pixels stay square and each
  Kenney pixel is one game pixel. Long keys are 32x16. (My call; the builder may
  nudge row spacing or column positions if 16 px glyphs crowd the 16 px row pitch,
  but must not scale the art by a non-integer factor.)
- Keyboard columns are 48 px wide, so a long key plus nothing else fits. The
  controller columns (43 px) only hold 16 px glyphs.
- Glyphs are drawn with PSX primitives into the Controls screen's ordering table, like
  every other native-menu element, not as a GL overlay.

## What goes away

- `pc_port/assets/gamedata/icons/pad_xbox.png`, `pad_nintendo.png`, and the planned
  `pad_original.png`.
- The OPTION2 hi-res override in `pc_pad_icons_apply.c`, the OPTION2 cell map in
  `pc_pad_icons.c`, and the `CellHasArt` / text fallback for missing cells.
- `pc_port/docs/PadIcons.md` is rewritten to describe the new atlas and how to
  regenerate or repaint it.
- The user's pending art list (d-pad and stick icons, `pad_original.png`, Xbox
  Select/Start tips) is no longer needed.

## Constraints

- Code rules from `CLAUDE.md`: `SH_DBG` for logging, no what-comments, nothing beyond
  the task.
- Unit tests that cover `pc_pad_icons.c` today keep passing in spirit: the pure mapping
  half (bind name to glyph, set label, SDL type to set) stays SDL-free and testable.
- The localization of "Gamepad Icons", the set names and `Auto` follows the existing
  menu localization path (`extract_text.py`, the `.lang` packs). Set names that are
  brand names (PS4, Xbox, ...) are not translated.

## Left to the builder

- The exact tile picks per set, within the families above.
- Atlas packing, its file name and location, and how it reaches the GPU: a VRAM rect
  plus the hi-res override path, or another route that still draws with PSX
  primitives. It must not collide with VRAM the game or other overlays use.
- Whether to keep the old `pc_pad_icons*` files and rewrite them, or replace them.
- The `[PADICONS]` log line's new wording.
