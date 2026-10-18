# Native Menus — Design

Date: 2026-10-18
Branch: `feat/native-menus` (off `upstream/pc-port` 34166e074)
Status: draft, awaiting user approval

## Intent

The PC port added several menus that are raw OpenGL overlays with TrueType text
(`pc_bind_panel.c`, `pc_ra_browser.c`). They work, but look nothing like Silent Hill.
The achievements list is also opened from the title screen with the Map button, which
nobody would guess.

This project replaces them with menus that are drawn the way the game draws its own
menus: PSX primitives, the FONT16 bitmap font, the gold selection underline and bullets,
and the blue value arrows. A player should not be able to tell them apart from retail
screens. SlickAmogus has accepted the proposal.

It also builds a small, reusable menu framework. SlickAmogus is working on an Online
mode that will need its own menus. Those menus should be cheap to add on top of this.

## Goals

1. A reusable native menu framework: pages, rows, scrolling, scroll arrows, input
   (pad, keyboard, mouse), sounds, fades, and enter/leave handling.
2. Main menu: ACHIEVEMENTS becomes a real row. The row list scrolls when it has more
   visible rows than fit above the copyright line.
3. A native Achievements screen (list view and single view).
4. A native Controls (rebind) screen, laid out after Silent Hill 2 Enhanced Edition's
   controls screen, replacing the GL bind panel in Options > Controller Config.
5. A "Gamepad Icons" setting that switches the controller button icons between
   icon sets (Original / Xbox / Nintendo).

## Non-goals

- The Online menu itself. Only the framework needs to be able to host it later.
- The achievement unlock toast (`pc_ra_toast.c`). It stays as it is.
- Restyling Quick Options (F10), the randomizer settings, the console, or other GL panels.
  In particular, Quick Options > Key Binds keeps opening the GL bind panel over gameplay
  (user decision). Converting it is a later round.
- Migrating the existing PC Options pages onto the new framework.
- Changes to the launcher's Controls form.

## Anti-goals

- Anything that looks like the GL overlays: TrueType text, panels with borders,
  window-like boxes, colours the game never uses.
- Changing how the retail title menu looks when it does not need to scroll. With
  5 or fewer visible rows it must look exactly like it does today.
- Breaking `config.cfg` compatibility with the launcher.
- Losing any capability of the current rebind panel: two keyboard binds and two
  controller binds per action, mouse button and wheel binds, duplicate warning,
  reset to defaults, controller capture timeout, global binds.

## Mockups

All in `C:\Users\trexx\Pictures\Screenshots\`:

| File | Shows |
|---|---|
| `PulsingArrows ExampleInsideRedRectangle.png` | The blue arrows in the retail Options menu, to be reused as scroll arrows |
| `MainMenu Mockup ExampleInsideRedRectangles.png` | Title menu with a scroll arrow above and below the rows |
| `Mockup Controls.png` | Controls screen (SH2 Enhanced Edition screenshot plus a Gamepad Icons row on top) |
| `Achievement Mockup.png` | Achievements list: one selected unlocked entry, one unlocked, three locked |
| `Achievement Single-View Mockup.png` | One achievement opened |

The mockups are the reference for layout and look. Exact pixel positions are left to
the builder, within the constraints below.

## Constraints

### Rendering

- Everything is drawn with PSX primitives into the game's OT, the same way the PC
  Options pages do (`src/screens/options/options.c`, `Options_PcOptionsMenu_*`).
  No direct GL calls in the new screens.
- Text uses `Gfx_StringDraw` and friends (`src/bodyprog/text/text_draw.c`). Reusable
  helpers already exist: `Options_Selection_HighlightDraw`,
  `Options_Selection_BulletPointDraw`, `Options_Selection_ArrowDraw`,
  `Options_Menu_VignetteDraw` (prototypes in `include/screens/options.h`).
- Screen space for strings is 320 wide. Primitives use centred coordinates.
- Text colours are the existing `e_StringColorId` set (Gold, DarkGrey, Green, Red,
  LightGrey, White, ...). No new colours unless a mockup needs one that does not exist.

### Fonts

There is one UI font (FONT16, 12x16 cells, variable width). It shows at two sizes:

- **Tall**: the "high-res glyph" path (`g_SysWork.enableHighResGlyphs`), which draws
  each glyph as a double-height POLY_FT4 in a 448-line screen mode.
- **Small**: the normal SPRT path in a 448-line screen mode. Glyphs show at half
  height. This is what the inventory and title menu use, and it is the second font
  in the achievement mockups.

Both sizes are the same width. At 320 px wide, roughly 30–35 characters fit on a line
once a badge is beside the text. The small font is shorter, not narrower.

Glyph set: ASCII `'` (0x27) to `z`, upper and lower case. There are no glyphs for
`"`, `#`, `$`, `%` or anything below 0x27. A space must be written as `_`. `!` and `&`
are remapped in the atlas. EUR has accented letters for bytes 0x80 and up.

### Title screen

- The menu column sits at x 158, y 184 with 20 px row spacing.
- The US copyright is baked into the title art at about y 286 (EUR ~306–318).
  No row or arrow may overlap it.

### Code rules (from CLAUDE.md)

- `SH_DBG(...)` for logging, never `fprintf(stderr, ...)`.
- Comments only for non-obvious reasons, not to narrate code.
- Retail behaviour stays retail outside the screens being replaced.
- No features or abstractions beyond what this document asks for.

## Decided by the user

### Framework

- Lists that do not fit scroll inside a visible window that follows the cursor.
- Scrolling applies to Controls, Achievements and the main menu.
- Scroll arrows are the blue triangles from the retail Options menu, turned to point
  up and down. The top arrow pulses upward, the bottom arrow pulses downward.
- It must be easy to build the future Online menu on it. The current Online lobby
  (SlickAmogus's branch, GL overlay) has: a heading, label + value rows ("Room Code
  6154"), action rows ("Start Game", "Close Room"), a status line, a Back button and a
  key-hint footer.

### Main menu

- ACHIEVEMENTS is a listed row alongside LOAD / CONTINUE / START / OPTION / EXIT.
- When the visible rows do not fit above the copyright, the list scrolls with an arrow
  above and below it (5 visible rows, per the mockup).
- The Map-button shortcut to the achievements list and its "M Achievements" hint at the
  bottom of the title screen are removed.

### Achievements list (see `Achievement Mockup.png`)

- Heading "ACHIEVEMENTS" at the top, tall font, white.
- One entry per achievement: badge on the left, title in the tall font in capitals,
  description under it in the small font.
- Selected entry: gold title, gold underline (the retail selection underline).
- Unlocked entry: white title and description.
- Locked entry: dark grey title and description, greyscale badge.
- About 5 entries visible, scroll arrows above and below.
- Bottom left: "Back" with the retail bullet. Bottom centre: progress counter
  `42/66 (426/666 points)`, the count in white and the points part in gold.
- Descriptions too long for the line are cut with `...`.

### Achievement single view (see `Achievement Single-View Mockup.png`)

- Heading "ACHIEVEMENTS".
- Badge centred.
- Title in gold capitals, with a gold gradient line under it.
- `UNLOCKED: 31. 12. 2026. 18:19` (white), then `3 Points` (gold), then another gold
  gradient line.
- Full description, word-wrapped, below.
- "Back" with bullet at the bottom left.

### Controls (see `Mockup Controls.png`)

- Top row: "Gamepad Icons" with the gold bullet, value on the right with the blue
  left/right arrows: `Original` / `Xbox` / `Nintendo`.
- Column headers between two thin lines: `Function` | `Keyboard` | `Controller`.
  The header of the column being edited is highlighted.
- Action rows: label right-aligned with a colon, keyboard bind(s) as text, controller
  bind(s) as icons.
- Each action shows its secondary bind next to the primary one, in both the keyboard
  and the controller column.
- Every bind cell (primary keyboard, secondary keyboard, primary controller, secondary
  controller) can be selected with the d-pad / arrow keys and with the mouse.
- Starting a rebind prints a message through the top-of-screen console toast
  (`DbgOverlay_ToastLine`): `Changing <Primary/Secondary> input for <action>`.
- Bind cells that cannot be changed are shown greyed out (the mockup's example is Esc
  for Cancel; see builder's call 17 for which cells this covers).
- The selected row uses the SH1 gold highlight (`Options_Selection_HighlightDraw`),
  not SH2's blue glow.
- The list scrolls; "Reset" sits at the bottom, below a thin line.

### Controller icons

- The icon atlas is TIM/OPTION2.TIM (128x256, 4bpp; icons 12x12 in the column at
  u 244..255, 16 px pitch). It has L2, R2, L1, R1, Triangle, Circle, Cross, Square,
  Select, L3, R3, Start. It has no d-pad and no stick-direction art.
- New icons (d-pad directions, stick directions) may be added. The user paints them.
  The 4 blank slots at v 192..255 (originally reserved for the d-pad) and the empty
  area left of u 244 are free.
- The user can toggle between the original icons and the replacement PNG sets.
  `pc_port/Xbox_SH1.png` and `pc_port/Nintendo_SH1.png` already exist in the OPTION2
  layout.
- Any icon with no art in the active set is drawn as text instead: `D-PAD U`,
  `D-PAD D`, `D-PAD L`, `D-PAD R`, and the same pattern for sticks.

### Localization

- The native screens are localized from the start, at least in every language the
  current bind panel and achievements browser support. Strings move from the
  TrueType `QUICK.*` path (`lang_quick.c`) to the game-font `MENU.*` path
  (`lang_menu.c` / `lang_pack.c`), and `extract_text.py` is updated to match.
- Polish and Cyrillic text must render in FONT16 (`font_region.c` holds the extra
  Polish glyphs; the Russian disc has its own font) and must fit the layouts.
- Covered strings: action labels, "Gamepad Icons" and its values, column headers,
  "Reset", "Back", "ACHIEVEMENTS", the Yes/No prompt, the unlock date and points
  lines, the progress counter, and the rebind toast. Achievement titles and
  descriptions come from RetroAchievements and are shown as delivered.

## Builder's calls (made by Claude, not confirmed by the user)

These are reasonable defaults. Flag any you disagree with.

1. **Row order:** ACHIEVEMENTS goes directly under OPTION, above EXIT.
2. **Achievements row visibility:** the row is hidden when no achievement set is
   loaded (RetroAchievements off, not signed in, or the set failed to load), the
   same way CONTINUE is hidden without a save.
3. **Scroll arrow state:** an arrow is drawn dark grey and stops pulsing when the list
   cannot scroll further in that direction (the top arrow in `Achievement Mockup.png`
   is grey while the first entry is selected). Both arrows are hidden when the list
   fits without scrolling.
4. **Backgrounds:** Achievements and Controls use a plain black background, as in
   the mockups. The main menu keeps the title art.
5. **"Original" icon set:** the disc TIM cannot hold new art, so "Original" also gets
   a PNG: the retail icons plus the user's new d-pad and stick icons. If that PNG is
   missing, the disc TIM is used and the new icons fall back to text.
6. **Icon setting storage:** a new `config.cfg` key (e.g. `gamepad_icons = original`),
   written with the existing `PcConfig_SaveKeyValue` path so the launcher keeps working.
7. **Controller buttons with no PS equivalent** (guide, touchpad, paddles, misc):
   drawn as text using the existing pretty-name mapping (`bp_pretty_pad`).
8. **Duplicate binds:** shown in Red (the current panel uses orange, which is not in
   the palette).
9. **Rebind capture:** reuse the current panel's logic unchanged (wait for release,
   take the first press, Esc cancels, 8 s controller timeout, mouse only on rows that
   allow it). While waiting, the selected cell shows `...`.
10. **Which scheme is edited:** same as today, fixed at open from the camera mode
    (classic or alt-cam), same action list and flags (`s_rows[]` in `pc_bind_panel.c`).
11. **Reset confirmation:** a native Yes/No prompt in the framework, styled after the
    inventory's "Is it OK to save?" Yes/No, instead of the GL `Pc_ConfirmDialog`.
12. **Unlock date format:** `DD. MM. YYYY. HH:MM` as in the mockup, local time. A locked
    achievement shows `LOCKED` instead.
13. **Text cleanup for achievement strings:** RetroAchievements text is converted to
    the glyph set before drawing: spaces to `_`, `"` to `'`, characters with no glyph
    dropped or replaced by their closest ASCII letter. Titles are upper-cased.
14. **Live unlocks:** if an achievement unlocks while the screen is open, the list and
    counter update.
15. **Old GL screens:** once replaced, `pc_ra_browser.c` is removed. `pc_bind_panel.c`
    stays, because Quick Options (F10) keeps using it. Its capture and config-saving
    logic moves into a renderer-free module shared by the GL panel and the native
    screen, so both edit binds the same way. Options > Controller Config no longer
    opens the GL panel.
16. **Branch:** work happens on `feat/native-menus`, off `upstream/pc-port`.
17. **Fixed cells and rows:** no display-only rows are added (no "Cancel: Esc" row);
    the row list is the current `s_rows[]`. The only greyed, non-selectable cells are
    the controller cells of the four Move rows (`BPF_STICK`). They show the left-stick
    direction icon and the d-pad direction icon for that row. The right stick is not
    shown anywhere.

## Left to the builder

- How pages and rows are represented in code (the `s_PcOpt` tables in `options.c`
  are a working pattern to start from).
- How screens are hosted: a title sub-state, a new game state, or Options states.
  Requirements: Achievements opens from the title menu, Controls from Options, Online
  later from the title; leaving returns to where the player came from with the usual
  fade, and the previous selection is kept.
- Exact positions, spacing and visible-row counts, matched to the mockups.
- Scroll arrow pulse speed and distance.
- How badges get onto native sprites. The hires-override pool slots
  (`HiresOverride_PoolSlotRegisterRGBA`, used by the minimap and decals) can map an
  RGBA texture onto ordinary textured primitives.
- How the icon PNG is swapped at runtime (OPTION2 is re-read on every entry to the
  controller screen; `fsqueue_3.c` already special-cases FONT16 in a similar way, and
  `HiresOverride_RegisterRGBA` can register a decoded PNG over the TIM's VRAM rect).
- The slot layout for the new d-pad and stick icons. Document it so the user can paint
  into it.
- Which 448-line / high-res glyph mode each screen runs in to get the tall and small
  font sizes.

## Open questions

- None. A reviewer read the draft; its questions are answered above (Quick Options,
  localization, fixed cells).
