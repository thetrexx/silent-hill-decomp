# Native Menus — TODO

Branch `feat/native-menus` (local only, off `upstream/pc-port` 34166e074). Spec and plan live in
`docs/superpowers/specs/2026-10-18-native-menus-design.md` and
`docs/superpowers/plans/2026-10-18-native-menus.md`. All 8 plan tasks are done and reviewed, and the
game and unit tests pass. The latest fixes (Escape on Achievements, texture-pack icon override,
Gold cursor on duplicates) have not been playtested yet.

## Next session (user requests, 2026-10-19)

1. **Controls screen: add a Back button and move Reset to the right.** Back closes the screen,
   the same as Escape / pad B. Both sit on the bottom row.
2. **Achievements screen: anchor the counter to the right.** Right-align `42/66 (426/666 points)`
   so it never runs into the Back button on the left (it is centred today and pushed right only
   when it overlaps).
3. **User style feedback** from playtesting checkpoints A/B (layout, spacing, colours).

## Done

- Kenney input glyphs (was item 3): the Controls screen draws Kenney "Input Prompts Pixel" glyphs
  for keys and pad buttons, with a Gamepad Icons selector (Auto, PS Classic, PS Modern, Xbox,
  Nintendo, Steam). Spec `docs/superpowers/specs/2026-10-19-kenney-input-glyphs-design.md`, how
  it works and how to repaint it: `pc_port/docs/InputGlyphs.md`. It replaced the OPTION2-based
  `pad_*.png` sets and `PadIcons.md`.

## Follow-ups deferred by the final review

- Controls screen cuts to black when you leave it. The spec asks for the usual fade.
- `NmPage_Update`: when the wheel lands on an INFO row, the selection moves without the scroll
  window following it. Fix this before the Online menu adopts `NmPage`.
- `NmInput_FromGame`: gate the wheel on `mouseOk`. Today the wheel can move twice when the cursor
  is outside the picture and the wheel is also bound to a direction.
- Achievement titles and descriptions pass through the exact-match menu localization table. A
  title that equals a menu key (e.g. "Back") would be translated.
- Escape during the fade into Achievements still quits the game, as it already does on the main
  menu.
- Translator notes for `MENU.Map` / `MENU.Reload` only describe one of their two uses
  (`extract_text.py` keeps one note per key).
- Check in game: the translated "Gamepad Icons" row in pt/it/de and the Polish "Klawiatura"
  header width.
- Before the PR: playtest gates A/B/C (title, Achievements, Controls, then German/Polish/Russian).

## Decisions to keep

- The ACHIEVEMENTS row is hidden when no RetroAchievements set is loaded, and the reason is
  printed once as a console message after login finishes.
- The rebind toast stays English (the console font is ASCII-only).
- Quick Options (F10) keeps the GL bind panel.
- Chinese and the ja.lang pack show these screens in English. Their font path shows no PC menu
  rows, so this matches the PC Options rows.
- The Controls screen closes only on Escape / pad B / pad Start, not the rebindable Cancel key.
