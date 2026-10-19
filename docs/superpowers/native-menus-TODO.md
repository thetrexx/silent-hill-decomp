# Native Menus — TODO

Branch `feat/native-menus` (local only, off `upstream/pc-port` 34166e074). Spec and plan live in
`docs/superpowers/specs/2026-10-18-native-menus-design.md` and
`docs/superpowers/plans/2026-10-18-native-menus.md`. All 8 plan tasks are done and reviewed, and the
game and unit tests pass. The latest fixes (Escape on Achievements, texture-pack icon override,
Gold cursor on duplicates) have not been playtested yet.

## Next session (user requests, 2026-10-19)

1. **User style feedback** from playtesting checkpoints A/B (layout, spacing, colours).

## Done

- Controls screen: Back button on the bottom row (closes like Escape / pad B), Reset moved to
  the right edge.
- Achievements screen: the counter is right-aligned, so it no longer runs into Back.
- Controls screen fades out on exit like the retail Options sub-screens (stays drawable until
  the fade ends).
- `NmPage_Update`: a wheel that lands on an INFO row scrolls the window to the row it skips to.
- `NmInput_FromGame`: the wheel only counts while the cursor is in the picture.
- Achievement titles and descriptions are drawn without the menu localization lookup.
- Escape is ignored from the Achievements pick on the title until the fade back to the menu.
- Translator notes for `MENU.Map` / `MENU.Reload` describe every use of the key.

- Kenney input glyphs (was item 3): the Controls screen draws Kenney "Input Prompts Pixel" glyphs
  for keys and pad buttons, with a Gamepad Icons selector (Auto, PS Classic, PS Modern, Xbox,
  Nintendo, Steam). Spec `docs/superpowers/specs/2026-10-19-kenney-input-glyphs-design.md`, how
  it works and how to repaint it: `pc_port/docs/InputGlyphs.md`. It replaced the OPTION2-based
  `pad_*.png` sets and `PadIcons.md`.

## Follow-ups deferred by the final review

- Before the PR: playtest gates A/B/C (title, Achievements, Controls, then German/Polish/Russian).

## Decisions to keep

- The ACHIEVEMENTS row is hidden when no RetroAchievements set is loaded, and the reason is
  printed once as a console message after login finishes.
- The rebind toast stays English (the console font is ASCII-only).
- Quick Options (F10) keeps the GL bind panel.
- Chinese and the ja.lang pack show these screens in English. Their font path shows no PC menu
  rows, so this matches the PC Options rows.
- The Controls screen closes only on Escape / pad B / pad Start, not the rebindable Cancel key.
