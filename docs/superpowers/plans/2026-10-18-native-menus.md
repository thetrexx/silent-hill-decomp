# Native Menus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the port's GL overlay menus (achievements browser, Options > Controller Config bind panel) with screens drawn the way the game draws its own menus, on a small reusable menu framework, plus a scrolling main menu with an ACHIEVEMENTS row and a Gamepad Icons setting.

**Architecture:** A renderer-free framework core (`pc_native_menu.c`: scrolling, text fitting, generic row page, Yes/No model) is unit-tested; a thin draw/input layer (`pc_native_menu_draw.c`) turns it into PSX primitives and FONT16 text. Each screen keeps its logic in a testable model file and its drawing in a screen file. Screens are hosted by the existing state machines: Achievements as a title sub-state, Controls as the existing `OptionsMenuState_Controller` state.

**Tech Stack:** C (C99, MinGW gcc 16 via MSYS2), PsyCross PSX HAL, CMake + Ninja, CTest (`-DBUILD_TESTING=ON`).

**Spec:** `docs/superpowers/specs/2026-10-18-native-menus-design.md` (read it first; this plan argues from it).

## Global Constraints

- Branch `feat/native-menus` (off `upstream/pc-port` 34166e074). One commit per task, message style `Native menus: <what>`, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push to `upstream`.
- Build: `MSYSTEM=MINGW64 "C:/msys64/usr/bin/bash.exe" -lc 'cd /d/GitHub/silent-hill-decomp/pc_port/build && cmake --build . 2>&1'`. Tests: same shell, `cmake .. -DBUILD_TESTING=ON && cmake --build . --target <test> && ctest -R <test> --output-on-failure`.
- Log: newest `pc_port/build/SilentHill_<timestamp>.log` (grep with `-a`). The user runs the game; agents build only.
- New screens draw with PSX primitives into the game OT and FONT16 via `Gfx_StringDraw`. No GL calls, no TrueType, in any new screen file.
- Logging only through `SH_DBG(...)`. Comments only for a non-obvious why. No features beyond the spec.
- FONT16 glyph set: ASCII `'` (0x27) to `z`, plus `!` and `&` (remapped in the atlas). Space is written `_`. No `"`, `#`, `$`, `%`. Text colours only from `e_StringColorId` (Gold, DarkGrey, Red, LightGrey, White, ...).
- Every drawn UI literal goes through `Gfx_StringDraw`, so it is localized by exact match in `Pc_LangMenuText` (`pc_port/src/lang_menu.c`). Literals are written in that dialect (`"Gamepad_Icons"`), and a label and its trailing colon are drawn separately so the label stays a clean key.
- Title screen (448-line mode): retail column x 158, y 184, pitch 20. Nothing may overlap the US copyright baked at about y 286 (authoring space).
- `config.cfg` stays launcher-compatible: new keys written only through `PcConfig_SaveKeyValue`.
- `pc_bind_panel.c` (GL) stays for Quick Options (F10) only.

## Review Focus

1. **Achievement list changes while open** (an unlock lands, or the set reloads with a different count): the selection must stay on the same achievement (or clamp), the counter must update, no out-of-range row. Test: `test_ach_refresh_keeps_selection` in Task 3.
2. **RetroAchievements text with characters outside the glyph set** (UTF-8 accents, curly quotes, emoji, `"`, `#`, `%`, over-long titles): every multi-byte sequence is consumed whole, nothing overflows, output is always terminated. Tests: `test_sanitize_utf8`, `test_sanitize_small_buffer` in Task 1.
3. **Main menu with different row sets** (no save, saves present, RetroAchievements compiled out): rows must sit exactly where retail puts them when 5 or fewer are visible, the selection never lands on a hidden row, and a mouse hover maps to the row actually drawn after scrolling. Tests: `test_title_layout_*` in Task 4.
4. **Rebinding a controller cell with no controller connected, or a bind cell that does not exist** (e.g. KB2 on Cycle Weapons): error sound, no capture started, nothing written. Test: `test_bind_cell_usable` in Task 5.
5. **Missing or broken icon PNG** (file absent, wrong size, decode failure): falls back to the disc icons, and every slot without art draws as text; switching sets while on the screen re-applies without a stale image. Test: `test_pad_icons_has_art` in Task 6.

---

### Task 1: Framework core (renderer-free)

**Files:**
- Create: `pc_port/include/pc_native_menu.h`, `pc_port/src/pc_native_menu.c`
- Create: `pc_port/tests/pc_native_menu_test.c`
- Modify: `pc_port/CMakeLists.txt` (inside `if(BUILD_TESTING)`, next to `pc_big_tmd_test`)

**Interfaces:**
- Consumes: nothing (no game headers; only `<stddef.h>`).
- Produces (all in `pc_native_menu.h`):

```c
typedef struct { int up, down, left, right;      /* pulsed (repeat) */
                 int confirm, cancel, clear;       /* press edges */
                 int wheel;                        /* notches, + = up */
                 int mouseOk, mouseX, mouseY;      /* 320x240 text space */
                 int mouseMoved, mouseLeft, mouseRight; } NmInput;

typedef struct { int count, visible, top, sel; } NmScroll;
void NmScroll_Init(NmScroll* s, int count, int visible);
void NmScroll_SetCount(NmScroll* s, int count);          /* clamps sel and top */
int  NmScroll_Move(NmScroll* s, int delta, int wrap);    /* 1 if sel changed */
void NmScroll_Select(NmScroll* s, int index);            /* keeps sel visible */
void NmScroll_Wheel(NmScroll* s, int notches);           /* moves top, pulls sel into window */
int  NmScroll_CanUp(const NmScroll* s);                  /* top > 0 */
int  NmScroll_CanDown(const NmScroll* s);                /* top + visible < count */

typedef int (*NmMeasureFn)(const char* s);               /* px width of a string */
#define NM_LINE_MAX 128
size_t Nm_SanitizeGlyphs(const char* utf8, char* out, size_t outSize, int upper);
int    Nm_FitEllipsis(const char* in, char* out, size_t outSize, int maxWidth, NmMeasureFn m); /* 1 if cut */
int    Nm_WrapLines(const char* in, char lines[][NM_LINE_MAX], int maxLines, int maxWidth, NmMeasureFn m);
void   Nm_FormatDate(int day, int month, int year, int hour, int minute, char* out, size_t outSize);

typedef enum { NM_ROW_ACTION, NM_ROW_VALUE, NM_ROW_INFO } NmRowKind;
typedef struct NmRow {
    const char* label;                                   /* FONT16 dialect */
    NmRowKind   kind;                                    /* INFO is never selectable */
    const char* (*value)(void* ctx);                     /* VALUE/INFO, may be NULL */
    void        (*step)(void* ctx, int dir);             /* VALUE */
    void        (*activate)(void* ctx);                  /* ACTION */
} NmRow;
typedef struct { const char* heading; const NmRow* rows; int rowCount; void* ctx; NmScroll scroll; } NmPage;
typedef enum { NM_EV_NONE, NM_EV_MOVED, NM_EV_STEPPED, NM_EV_ACTIVATED, NM_EV_BACK, NM_EV_ERROR } NmEvent;
void    NmPage_Init(NmPage* p, int visibleRows);         /* selects the first selectable row */
NmEvent NmPage_Update(NmPage* p, const NmInput* in, int hoverRow); /* hoverRow -1 = none */

typedef enum { NM_CONFIRM_PENDING, NM_CONFIRM_YES, NM_CONFIRM_NO } NmConfirmResult;
typedef struct { int open, yes; } NmConfirm;
void            NmConfirm_Open(NmConfirm* c);            /* opens on No */
NmConfirmResult NmConfirm_Update(NmConfirm* c, const NmInput* in); /* left/right toggles, confirm picks, cancel = No */
```

- [ ] **Step 1: Write the failing tests** in `pc_port/tests/pc_native_menu_test.c` (assert-based like `pc_big_tmd_test.c`, `#undef NDEBUG` first). Use a measure function `m8` that returns `8 * strlen(s)`.

```c
static void test_scroll_follow(void) {        /* 10 items, 4 visible */
    NmScroll s; NmScroll_Init(&s, 10, 4);
    for (int i = 0; i < 5; i++) NmScroll_Move(&s, +1, 0);
    assert(s.sel == 5 && s.top == 2);
    NmScroll_Move(&s, +10, 0); assert(s.sel == 9 && s.top == 6);   /* clamp, no wrap */
    NmScroll_Move(&s, +1, 1);  assert(s.sel == 0 && s.top == 0);   /* wrap */
    assert(!NmScroll_CanUp(&s) && NmScroll_CanDown(&s));
}
static void test_scroll_wheel(void) {
    NmScroll s; NmScroll_Init(&s, 10, 4);
    NmScroll_Wheel(&s, -3); assert(s.top == 3 && s.sel == 3);      /* wheel down pulls sel */
    NmScroll_Wheel(&s, -99); assert(s.top == 6);
    NmScroll_SetCount(&s, 5); assert(s.top == 1 && s.sel <= 4);
}
static void test_scroll_fits(void) {
    NmScroll s; NmScroll_Init(&s, 3, 5);
    assert(!NmScroll_CanUp(&s) && !NmScroll_CanDown(&s));
}
static void test_sanitize_ascii(void) {
    char o[64];
    Nm_SanitizeGlyphs("What a Nightmare", o, sizeof o, 1); assert(!strcmp(o, "WHAT_A_NIGHTMARE"));
    Nm_SanitizeGlyphs("Hammerin' Harry", o, sizeof o, 1);  assert(!strcmp(o, "HAMMERIN'_HARRY"));
    Nm_SanitizeGlyphs("Say \"hi\" 100% #1!", o, sizeof o, 0); assert(!strcmp(o, "Say_'hi'_100_1!"));
}
static void test_sanitize_utf8(void) {
    char o[64];
    Nm_SanitizeGlyphs("Caf\xC3\xA9 \xE2\x80\x93 done\xE2\x80\xA6", o, sizeof o, 0);
    assert(!strcmp(o, "Cafe_-_done..."));
    Nm_SanitizeGlyphs("It\xE2\x80\x99s \xF0\x9F\x98\x80ok", o, sizeof o, 0);  /* curly quote, emoji */
    assert(!strcmp(o, "It's_ok"));
}
static void test_sanitize_small_buffer(void) {
    char o[6]; memset(o, 'X', sizeof o);
    Nm_SanitizeGlyphs("ABCDEFGHIJ", o, sizeof o, 0); assert(!strcmp(o, "ABCDE"));
}
static void test_fit_ellipsis(void) {
    char o[64];
    assert(Nm_FitEllipsis("ABCDEFGHIJ", o, sizeof o, 48, m8) == 1 && !strcmp(o, "ABC..."));
    assert(Nm_FitEllipsis("ABC", o, sizeof o, 48, m8) == 0 && !strcmp(o, "ABC"));
    Nm_FitEllipsis("Use_the_Lion", o, sizeof o, 80, m8); assert(!strcmp(o, "Use_the..."));
    Nm_FitEllipsis("Use_the_Lion", o, sizeof o, 72, m8); assert(!strcmp(o, "Use_th..."));
    Nm_FitEllipsis("Use_the_Lion", o, sizeof o, 56, m8); assert(!strcmp(o, "Use..."));   /* trailing '_' trimmed before ... */
}
static void test_wrap(void) {
    char l[4][NM_LINE_MAX];
    assert(Nm_WrapLines("Complete_the_introduction_chapter", l, 4, 120, m8) == 3);
    assert(!strcmp(l[0], "Complete_the") && !strcmp(l[1], "introduction") && !strcmp(l[2], "chapter"));
    assert(Nm_WrapLines("aa_bb_cc_dd_ee", l, 2, 40, m8) == 2 && !strcmp(l[1], "cc..."));    /* last line ellipsized */
    assert(Nm_WrapLines("ABCDEFGHIJKLMNOP", l, 4, 40, m8) == 4 && !strcmp(l[0], "ABCDE")); /* hard cut */
}
static void test_date(void) {
    char o[32]; Nm_FormatDate(31, 12, 2026, 18, 19, o, sizeof o); assert(!strcmp(o, "31._12._2026._18:19"));
    Nm_FormatDate(1, 2, 2027, 3, 4, o, sizeof o); assert(!strcmp(o, "01._02._2027._03:04"));
}
static void test_page(void) {                 /* rows: INFO, VALUE, ACTION */
    /* build a 3-row page with counting callbacks */
    /* NmPage_Init selects row 1 (skips INFO) */
    /* right on VALUE -> step(+1), NM_EV_STEPPED; down -> row 2, NM_EV_MOVED;
       confirm on ACTION -> activate called, NM_EV_ACTIVATED; up from 1 with wrap skips row 0 -> row 2;
       cancel -> NM_EV_BACK; hoverRow 0 (INFO) leaves selection unchanged */
}
static void test_confirm(void) {
    NmConfirm c; NmInput in = {0}; NmConfirm_Open(&c); assert(c.open && !c.yes);
    in.left = 1; assert(NmConfirm_Update(&c, &in) == NM_CONFIRM_PENDING && c.yes);
    in = (NmInput){0}; in.confirm = 1; assert(NmConfirm_Update(&c, &in) == NM_CONFIRM_YES && !c.open);
    NmConfirm_Open(&c); in = (NmInput){0}; in.cancel = 1; assert(NmConfirm_Update(&c, &in) == NM_CONFIRM_NO);
}
```

`test_page` is described in comments above; write it with three static counters for step/activate calls and assert each listed outcome.

- [ ] **Step 2: Add the test target to `pc_port/CMakeLists.txt`**

`add_executable(pc_native_menu_test tests/pc_native_menu_test.c src/pc_native_menu.c)`, include dir `include`, `add_test(NAME pc_native_menu_test COMMAND pc_native_menu_test)`.

- [ ] **Step 3: Run tests, verify they fail to build** (no `pc_native_menu.c` yet).

Run: `cmake .. -DBUILD_TESTING=ON && cmake --build . --target pc_native_menu_test`
Expected: build error, missing `pc_native_menu.h` / undefined `NmScroll_Init`.

- [ ] **Step 4: Implement `pc_native_menu.c`** with the interface above.
  - Sanitize rules: ASCII 0x27–0x7A pass; `!` and `&` pass; space becomes `_`; `"` becomes `'`; other ASCII is dropped. UTF-8: decode each sequence whole; Latin-1 letters fold to their base letter (à–å→a, ç→c, è–ë→e, ì–ï→i, ñ→n, ò–ö/ø→o, ù–ü→u, ý/ÿ→y, ß→ss, same for capitals); U+2018/2019 → `'`, U+201C/201D → `'`, U+2013/2014 → `-`, U+2026 → `...`; anything else, and malformed bytes, are dropped. `upper` applies `toupper` after folding. Collapse runs of `_` to one.
  - `Nm_FitEllipsis`: longest prefix with `measure(prefix) + measure("...") <= maxWidth`, trailing `_` trimmed, then `...`.
  - `Nm_WrapLines`: greedy on `_`, words rejoined with `_`; a word wider than the line is hard-cut; if text remains after `maxLines`, the rest of the text from the last line's start goes through `Nm_FitEllipsis` to become the last line.
  - `NmPage_Update`: up/down move over selectable rows with wrap; left/right step VALUE rows; confirm activates ACTION rows, and on a VALUE row steps +1; wheel scrolls; a valid `hoverRow` on a selectable row selects it, and `mouseLeft` on it acts like confirm; `cancel` or `mouseRight` returns `NM_EV_BACK`; confirm on INFO cannot happen (never selected).

- [ ] **Step 5: Run tests, verify they pass**

Run: `cmake --build . --target pc_native_menu_test && ctest -R pc_native_menu_test --output-on-failure`
Expected: `100% tests passed`.

- [ ] **Step 6: Build the game** (the new file is picked up by the `PC_PORT_SOURCES` glob). Expected: no new warnings from `pc_native_menu.c`.

- [ ] **Step 7: Commit** `Native menus: framework core (scroll, text fitting, row page)`.

---

### Task 2: Framework draw and input layer

**Files:**
- Create: `pc_port/include/pc_native_menu_draw.h`, `pc_port/src/pc_native_menu_draw.c`

**Interfaces:**
- Consumes: Task 1 (`NmInput`, `NmScroll`, `NmPage`, `NmConfirm`). Game: `Gfx_StringSetPosition`, `Gfx_StringSetColor`, `Gfx_Strings2dLayerIdxSet`, `Gfx_StringDraw` (`src/bodyprog/text/text_draw.c`), `Pc_LangMenuText`, `Pc_LangMenuTextWidth` (`lang_text.h`), `Options_Selection_HighlightDraw`, `Options_Selection_BulletPointDraw` (`include/screens/options.h`), `Pc_MouseCursor_UiPos`, `Pc_MouseCursor_Moved`, `Pc_MouseCursor_LeftClicked`, `Pc_MouseCursor_RightClicked`, `Pc_MouseCursor_WheelNotches` (`pc_mouse_cursor.h`), `g_Controller0`, `g_GameWorkPtr->config.controllerConfig`.
- Produces:

```c
enum { NM_TEXT_TALL, NM_TEXT_SMALL };
void Nm_TextDraw(int x, int y, int size, int color, const char* s);   /* y in 240-line space */
int  Nm_TextWidth(const char* s);                                     /* localized width, NmMeasureFn-compatible */
void Nm_TextDrawCentered(int cx, int y, int size, int color, const char* s);
void Nm_TextDrawRight(int rx, int y, int size, int color, const char* s);
void Nm_ScrollArrowDraw(int cx, int y, int up, int enabled);           /* pulses when enabled */
void Nm_HighlightDraw(int x0, int x1, int yBottom);                    /* SH1 gold underline + shadow */
void Nm_BulletDraw(int x, int y, int selected);
void Nm_RuleDraw(int x0, int x1, int y, int gold);                     /* gold gradient (single view) or thin grey line */
void Nm_ValueArrowsDraw(int xLeft, int xRight, int y);                 /* blue ◀ ▶ around a value */
void Nm_BlackBackgroundDraw(void);
void Nm_SpriteDraw(int x, int y, int w, int h, u16 clut, u8 rgb);       /* pool-slot textured quad */
void NmInput_FromGame(NmInput* out);                                   /* game pad flags + player binds + mouse */
void NmPage_Draw(const NmPage* p, int x, int y, int pitch, int valueX);
void NmConfirm_Draw(const NmConfirm* c, const char* question, int cy);
```

- [ ] **Step 1: Implement `Nm_TextDraw` for both screen modes.** In a 448-line screen (`g_GameWork.gsScreenHeight == FRAMEBUFFER_HEIGHT_INTERLACED`) TALL sets `g_SysWork.enableHighResGlyphs = 1` and positions at `y` (the high-res path doubles `y`); SMALL leaves it 0 and positions at `2 * y - 112` so both sizes share one `y` space. Restore `enableHighResGlyphs = 0` after every call. In a 224-line screen both sizes draw TALL (normal path). Every call sets layer 8 after `Gfx_StringSetPosition` (it resets the layer to 6). `color` is an `e_StringColorId`.
- [ ] **Step 2: Implement the primitive helpers.** All take 240-line coordinates and convert to the centred primitive space of the current mode (doubling y in 448-line mode). `Nm_ScrollArrowDraw` builds its own POLY_G3: when enabled, the blue gradient colours of `Options_Selection_ArrowDraw` (read them from `options.c:4097`) and a pulse offset of 0–2 px along the arrow direction over about one second; when disabled, DarkGrey and static. `Nm_HighlightDraw` / `Nm_BulletDraw` wrap the options helpers. `Nm_SpriteDraw` emits a POLY_FT4 with `tpage = 0` and the given pool-slot clut word, as `pc_minimap.c:794-799` does.
- [ ] **Step 3: Implement `NmInput_FromGame`.** up/down/left/right from `pulsedBtnFlags & ControllerFlag_LStick*`; confirm from `clickedBtnFlags & (enter | action)`; cancel from `clickedBtnFlags & cancel`; mouse fields from the `Pc_MouseCursor_*` calls; wheel from `Pc_MouseCursor_WheelNotches()`.
- [ ] **Step 4: Implement `NmPage_Draw` and `NmConfirm_Draw`.** Page: heading TALL white centred, rows with `Nm_BulletDraw` + label, value right of `valueX` with `Nm_ValueArrowsDraw` on the selected VALUE row, INFO rows LightGrey without bullet, highlight under the selected row, scroll arrows when `NmScroll_CanUp/CanDown`. Confirm: question centred, `Yes` and `No` side by side, the selected one Gold with highlight (modelled on the inventory's "Is it OK to save?" prompt, `item_screens_2.c:509`).
- [ ] **Step 5: Build.** Expected: clean build. (Visual check happens with the first screen in Task 4.)
- [ ] **Step 6: Commit** `Native menus: draw and input layer`.

---

### Task 3: Achievements screen

**Files:**
- Create: `pc_port/include/pc_ach_model.h`, `pc_port/src/pc_ach_model.c` (pure)
- Create: `pc_port/include/pc_ach_screen.h`, `pc_port/src/pc_ach_screen.c`
- Create: `pc_port/tests/pc_ach_model_test.c`; Modify: `pc_port/CMakeLists.txt`
- Modify: `pc_port/include/pc_retroachievements.h` (gains `PcRaAch`, `Pc_Ra_SnapshotAchievements`, `Pc_Ra_RequestBadge`, `Pc_Ra_SetLoaded`, moved from `pc_ra_browser.h`)
- Modify: `pc_port/src/pc_retroachievements.c:1320-1330` (badge bytes also go to `Pc_AchScreen_ProvideBadge`)

**Interfaces:**
- Consumes: Tasks 1–2. `HiresOverride_DecodeToRGBA`, `HiresOverride_PoolSlotRegisterRGBA` (`hires_override.h`), `HIRES_POOL_CLUT_ROW_BASE`, `HIRES_POOL_MAX_ROWS`.
- Produces:

```c
/* pc_retroachievements.h */
int Pc_Ra_SetLoaded(void);                       /* 1 when a set is loaded and snapshot-able */

/* pc_ach_model.h */
#define ACH_MAX 128
typedef struct { PcRaAch items[ACH_MAX]; int count; int unlocked; unsigned pts, ptsTotal; } AchList;
void AchList_Build(AchList* l, const PcRaAch* src, int n);    /* sorts: unlocked first, then points asc, then title */
int  AchList_IndexOf(const AchList* l, const char* badge, const char* title); /* -1 if gone */
#define ACH_BADGE_SLOTS 8
#define ACH_BADGE_SLOT_FIRST 500                                 /* free pool range 448..509; minimap uses 510/511 */
typedef struct { char badge[ACH_BADGE_SLOTS][16]; unsigned age[ACH_BADGE_SLOTS]; int ready[ACH_BADGE_SLOTS]; unsigned tick; } AchBadgeCache;
int  AchBadgeCache_Find(AchBadgeCache* c, const char* badge);  /* slot index or -1; refreshes age */
int  AchBadgeCache_Claim(AchBadgeCache* c, const char* badge); /* LRU slot index, ready = 0 */

/* pc_ach_screen.h */
void Pc_AchScreen_Open(void);
int  Pc_AchScreen_IsOpen(void);
int  Pc_AchScreen_Update(const NmInput* in);      /* 1 while open; 0 once the player backs out of the list */
void Pc_AchScreen_Draw(void);
void Pc_AchScreen_ProvideBadge(const char* badge, const unsigned char* png, size_t len);
```

- [ ] **Step 1: Write failing tests** in `pc_ach_model_test.c`:
  - `test_ach_sort`: input `{B locked 10, A unlocked 5, C unlocked 1, D locked 3}` gives order `C, A, D, B`; `unlocked == 2`, `pts == 6`, `ptsTotal == 19`.
  - `test_ach_refresh_keeps_selection`: build list, select index of `D`; rebuild with `D` now unlocked; `AchList_IndexOf(l, "D-badge", "D") == 2` (it moved into the unlocked block), and for a badge no longer present it returns -1 (caller then clamps with `NmScroll_SetCount`).
  - `test_badge_cache_lru`: claim 8 names, find the first (refreshes it), claim a 9th: the evicted slot is the second name's; `Find` of the evicted name returns -1.
- [ ] **Step 2: Run, verify fail** (`pc_ach_model_test` does not build).
- [ ] **Step 3: Implement `pc_ach_model.c`**, move the struct and declarations into `pc_retroachievements.h`, add `Pc_Ra_SetLoaded` (true when the rcheevos game is loaded; stub returns 0 in the `!SH_RETROACHIEVEMENTS` branch at `pc_retroachievements.c:1451`).
- [ ] **Step 4: Run tests, verify pass.**
- [ ] **Step 5: Implement `pc_ach_screen.c`.** Runs inside the title's 448-line screen. Layout (240-line space; starting values, calibrate against the mockups):
  - Heading `"ACHIEVEMENTS"` TALL White centred at y 12.
  - List: 5 entries visible, pitch 30, first at y 46. Badge 26x26 at x 8. Title TALL at x 40 (sanitized, `upper = 1`, `Nm_FitEllipsis` to 272 px); description SMALL at title y + 16 (sanitized, fitted to 272 px). Selected: title Gold, `Nm_HighlightDraw` under the entry. Unlocked: White. Locked: DarkGrey text, badge from the greyscale row at reduced brightness.
  - `Nm_ScrollArrowDraw` above (y 38) and below (y 198) the list, enabled from `NmScroll_CanUp/CanDown`.
  - Footer y 218: `Nm_BulletDraw` + `"Back"` at x 16; counter at the centre right: `"<unlocked>/<count>_"` White, then `"(<pts>/<ptsTotal>_" + Pc_LangMenuText("points") + ")"` Gold.
  - `Back` is selectable: down from the last entry selects it, up returns. Confirm on an entry opens the single view; confirm on Back, or cancel, closes the screen.
  - Single view: badge 32x32 centred at y 40; title Gold TALL centred at y 80; `Nm_RuleDraw(gold)` under it; `"UNLOCKED:"` + `_` + `Nm_FormatDate(...)` from `localtime(unlockTime)` White TALL centred (locked: `"LOCKED"`); `<pts>_` + `"Points"` (`"Point"` when 1) Gold SMALL; second gold rule; description SMALL wrapped with `Nm_WrapLines` (8 lines, 260 px) from x 30; `Back` + bullet bottom left. Cancel or confirm returns to the list with the same selection.
  - Badges: request for visible rows only (`Pc_Ra_RequestBadge`). `Pc_AchScreen_ProvideBadge` decodes with `HiresOverride_DecodeToRGBA`, claims a cache slot, registers row 0 colour and row 1 greyscale (luma) with `HiresOverride_PoolSlotRegisterRGBA(ACH_BADGE_SLOT_FIRST + slot, row, ...)`; clut word per the canonical encoding in `hires_override.h` (see `MM_CLUT` in `pc_minimap.c:76-78`). Bytes for a badge not on screen are ignored.
  - Live unlocks: re-snapshot every 60 frames while open; rebuild, keep selection with `AchList_IndexOf`, clamp with `NmScroll_SetCount`.
- [ ] **Step 6: Wire badge delivery** in `pc_retroachievements.c` next to the existing toast call. Build. Expected: clean build (screen not reachable yet).
- [ ] **Step 7: Commit** `Native menus: achievements screen`.

---

### Task 4: Main menu row, scrolling, and hosting

**Files:**
- Modify: `include/bodyprog/bodyprog.h:85-111` (`e_MainMenuEntry`, `e_MainMenuState`)
- Modify: `src/bodyprog/events/title.c` (flags macro :58-65, `NEXT_GAME_STATES` :90-99, browser hooks :108/:140/:234-287/:313/:560-566/:686-697, confirm switch :362-407, `MainMenu_MainTextDraw` :806-873, delete `MainMenu_AchievementHintDraw` :735-803)
- Modify: `pc_port/include/pc_native_menu.h`, `pc_port/src/pc_native_menu.c`, `pc_port/tests/pc_native_menu_test.c` (title layout)
- Modify: `pc_port/src/dbg_overlay.c:2097`, `pc_port/src/pc_retroachievements.c` (drop browser calls/include)
- Delete: `pc_port/src/pc_ra_browser.c`, `pc_port/include/pc_ra_browser.h`

**Interfaces:**
- Consumes: Tasks 1–3 (`Pc_AchScreen_*`, `Pc_Ra_SetLoaded`, `Pc_Ra_UnavailableReason`, `NmInput_FromGame`, `Nm_ScrollArrowDraw`).
- Produces:

```c
/* pc_native_menu.h */
int Nm_CompactRows(unsigned visibleFlags, int entryCount, int order[]);  /* visible entry ids in order; returns n */
int Nm_SlotBase(int n, int windowSlots);                                  /* n <= window: window - n; else 0 */

/* bodyprog.h, SH_PC_PORT branch only (retail branch unchanged) */
MainMenuEntry_Achievements = 4, MainMenuEntry_Exit = 5, MainMenuEntry_Count = 6
MenuState_Achievements = 5   /* e_MainMenuState */
```

- [ ] **Step 1: Write failing tests** in `pc_native_menu_test.c`:
  - `test_title_layout_retail_full`: flags `LOAD|CONTINUE|START|OPTION|EXIT` (achievements hidden) gives n 5, base 0, so rows land on slots 0..4 exactly as retail.
  - `test_title_layout_retail_nosave`: flags `START|OPTION|EXIT` gives n 3, base 2 (START on slot 2 = y 224, as retail today).
  - `test_title_layout_nosave_ach`: `START|OPTION|ACH|EXIT` gives n 4, base 1, order `{2,3,4,5}`.
  - `test_title_layout_scroll`: all 6 visible gives n 6, base 0; with `NmScroll` (count 6, visible 5) and sel on EXIT (compact index 5), `top == 1`, so EXIT draws on slot 4 and a hover on slot 0 maps to compact index 1 (CONTINUE).
- [ ] **Step 2: Run, verify fail.** Then implement the two functions; run, verify pass.
- [ ] **Step 3: Enum and flags.** PC branch: ACHIEVEMENTS sits under OPTION and above EXIT. `MAINMENU_BASE_ENTRY_FLAGS` gains the Achievements bit `#ifdef SH_RETROACHIEVEMENTS`. `NEXT_GAME_STATES` grows to `MainMenuEntry_Count` with `GameState_MainMenu` in the Achievements slot (never used) and `GameState_MovieIntro` for Exit. The `flags << MainMenuEntry_Count` wrap trick still fits in 12 bits. `"ACHIEVEMENTS"` joins `MAIN_MENU_ENTRY_STRINGS`; it and EXIT are centred from `Pc_LangMenuTextWidth`, as EXIT is today.
- [ ] **Step 4: Layout and scroll.** Keep a static `NmScroll` over the compact list (`Nm_CompactRows`, visible = min(n, 5)), synced to `g_MainMenu_SelectedEntry` each frame (selection moves stay in the existing up/down code; then `NmScroll_Select` on the compact index). Draw compact row `i` on slot `Nm_SlotBase(n,5) + i - top`, at y `184 + slot * 20`. With n ≤ 5 nothing else changes (retail look). With n > 5 draw `Nm_ScrollArrowDraw` above and below; the builder may tighten the pitch (minimum 17) and/or raise the column in scroll mode only, so that both arrows end above y 284 (US). Replace the mouse call at `title.c:313` with a hit test over the drawn slots using `Pc_MouseCursor_UiPos` (rows 20 px tall, or the scroll pitch), mapping slot to compact index to entry id; the wheel scrolls through `NmScroll_Wheel`.
- [ ] **Step 5: Hosting.** Confirm on ACHIEVEMENTS:
  - **No set loaded** (`!Pc_Ra_SetLoaded()`): `SD_Call(Sfx_MenuError)`, `DbgOverlay_ToastLine(Pc_Ra_UnavailableReason())`, stay on the menu. This matches upstream 34166e074 and **replaces spec call #2 (hide the row) pending the user's OK** (see handoff).
  - **Set loaded:** `SD_Call(Sfx_MenuConfirm)`, fade out, `g_MainMenuState = MenuState_Achievements` (set it explicitly; the default `g_MainMenuState++` path hands off to `NEXT_GAME_STATES`). On fade end: `Pc_AchScreen_Open()`, fade in.
  - **While in `MenuState_Achievements`:** build `NmInput_FromGame`, call `Pc_AchScreen_Update`; when it returns 0, fade out, back to `MenuState_Main`, fade in, selection still on ACHIEVEMENTS.
  - **Draw:** skip `MainMenu_BackgroundDraw`, call `Pc_AchScreen_Draw()` and `Pc_MouseCursor_Draw()`.
  - **Attract mode:** keep the idle counter pinned and the attract demo suppressed while in this state, which replaces the `browserOpen` logic.
- [ ] **Step 6: Remove the old browser.** Delete the Map-button block, the `browserOpen` plumbing, the hint draw and its function, `Pc_RaBrowser_Draw` in `dbg_overlay.c`, the include and `Pc_RaBrowser_ProvideBadge` call in `pc_retroachievements.c`, and both browser files. `grep -rn "RaBrowser" src pc_port` must return nothing.
- [ ] **Step 7: Build, run the tests.** Expected: clean build, `pc_native_menu_test` and `pc_ach_model_test` pass.
- [ ] **Step 8: Commit** `Native menus: ACHIEVEMENTS row, scrolling title menu, native achievements`.
- [ ] **Step 9: USER PLAYTEST GATE A.** Ask the user to check, then wait:
  - **Title, saves present:** 6 rows, scroll arrows pulse, nothing over the copyright.
  - **Title, no save:** rows look as before, plus ACHIEVEMENTS.
  - **Mouse:** hover, click and wheel on the title rows.
  - **Achievements list and single view vs the mockups:** fonts, colours, badges (colour and grey), counter, Back.
  - **Error path:** with RetroAchievements signed out, selecting the row toasts the reason.
  - **Idle:** sitting in the list does not start the attract demo.

  Apply the user's layout corrections as follow-up commits before Task 5.

---

### Task 5: Bind core split (behaviour unchanged)

**Files:**
- Create: `pc_port/include/pc_bind_core.h`, `pc_port/src/pc_bind_core.c` (rows, values, set, reset, duplicates, toast text; no SDL)
- Create: `pc_port/include/pc_bind_input.h`, `pc_port/src/pc_bind_input.c` (raw SDL navigation and capture)
- Modify: `pc_port/src/pc_bind_panel.c` (uses both; keeps only GL drawing, phases, mouse geometry)
- Create: `pc_port/tests/pc_bind_core_test.c`; Modify: `pc_port/CMakeLists.txt`

**Interfaces:**
- Consumes: `PcConfig_BindField`, `PcConfig_BindDefault`, `PcConfig_SaveKeyValue`, `PcConfig_SaveKeyValues`, `Pc_ControlStyle_ReapplyBinds`, `Pc_QuickOptions_InvalidateRows`, `g_PcBindsGen` (as `pc_bind_panel.c` does today).
- Produces:

```c
/* pc_bind_core.h */
enum { BC_KB1 = 0, BC_KB2, BC_PAD1, BC_PAD2, BC_COLS };
#define BCF_STICK 1
#define BCF_ALTCAM 2
#define BCF_GLOBAL 4
#define BCF_MOUSE 8
#define BCF_WHEEL 16
typedef struct { const char* label; const char* key[BC_COLS]; int flags; } BcRow;   /* the s_rows[] table moves here verbatim */
int          BindCore_RowCount(void);
const BcRow* BindCore_Row(int row);
void         BindCore_SetScheme(int scheme);   /* 0 classic, 1 altcam */
int          BindCore_Scheme(void);
int          BindCore_Unbound(const char* v);
const char*  BindCore_Value(int row, int col);
int          BindCore_RowEnabled(int row);
int          BindCore_CellUsable(int row, int col);
int          BindCore_IsDuplicate(int row, int col);
void         BindCore_Set(int row, int col, const char* value);   /* writes config, logs, reapplies */
void         BindCore_ResetScheme(void);
void         BindCore_FormatRebindToast(int row, int col, char* out, size_t n); /* "Changing Primary input for Run" */

/* pc_bind_input.h */
enum { BN_UP = 0, BN_DOWN, BN_LEFT, BN_RIGHT, BN_OK, BN_BACK, BN_CLEAR, BN_COUNT };
typedef struct { int prev[BN_COUNT]; unsigned repeatAt[BN_COUNT]; } BindNav;
void        BindNav_Reset(BindNav* n);                      /* whatever is held must be released first */
void        BindNav_Poll(BindNav* n, int pressed[BN_COUNT]);/* bp_read_nav + bp_nav_edges */
void        BindCapture_Begin(int row, int col);
const char* BindCapture_Update(void);                       /* NULL waiting, "" cancelled, else bind name */
int         BindInput_PadConnected(void);
```

- [ ] **Step 1: Write failing tests** in `pc_bind_core_test.c`, stubbing the config and reapply functions with an in-memory key/value table as `pc_big_tmd_test.c` stubs its environment:
  - `test_bind_cell_usable`: `Cycle Weapons` row (index 15) has no `BC_KB2` (`CellUsable == 0`); `Move Up` `BC_PAD1` is not usable (stick); `Rear Look` is disabled on scheme 0 and enabled on scheme 1.
  - `test_bind_duplicate`: set `key_cross = Space` and `key_circle = Space` on scheme 0, then `IsDuplicate(4, BC_KB1) && IsDuplicate(5, BC_KB1)`; a pad value equal to a key value is not a duplicate.
  - `test_bind_set_altcam_key`: scheme 1 `BindCore_Set(7, BC_KB1, "E")` saves key `key_square_altcam`; global row `Quick Save` saves `key_quicksave` on either scheme.
  - `test_bind_toast`: `(7, BC_KB1)` gives `"Changing Primary input for Run"`; `(4, BC_PAD2)` gives `"Changing Secondary input for Action / Shoot"`.
- [ ] **Step 2: Run, verify fail.**
- [ ] **Step 3: Move the logic** out of `pc_bind_panel.c` into the two new files, unchanged except for names (`bp_*` to the API above). The panel calls through them. Do not change panel behaviour or look.
- [ ] **Step 4: Run tests, verify pass. Build.**
- [ ] **Step 5: Commit** `Native menus: split bind logic out of the GL bind panel`.

---

### Task 6: Gamepad icon sets

**Files:**
- Create: `pc_port/include/pc_pad_icons.h`, `pc_port/src/pc_pad_icons.c` (pure mapping + art detection), `pc_port/src/pc_pad_icons_apply.c` (PNG load and VRAM override)
- Create: `pc_port/docs/PadIcons.md` (slot layout for the user to paint)
- Move: `pc_port/Xbox_SH1.png` to `pc_port/assets/gamedata/icons/pad_xbox.png`, `pc_port/Nintendo_SH1.png` to `pc_port/assets/gamedata/icons/pad_nintendo.png` (`git mv`); `pad_original.png` is supplied later by the user
- Modify: `pc_port/include/pc_config.h`, `pc_port/src/pc_config.c` (int `gamepadIcons`, key `gamepad_icons`, default 0, parsed and saved like `bullet_decals`)
- Modify: `src/main/fsqueue_3.c` (after the OPTION2 TIM upload, call `PadIcons_Apply()`; model on the `FILE_1ST_FONT16_TIM` case at :1620-1694)
- Create: `pc_port/tests/pc_pad_icons_test.c`; Modify: `pc_port/CMakeLists.txt`

**Interfaces:**
- Consumes: `HiresOverride_DecodeToRGBA`, `HiresOverride_RegisterRGBA(label, rgba, w, h, 480, 0, 32, 256, 464, 0, 4)`, and whichever `HiresOverride_DropVramRect` signature `hires_override.h` declares, to return to the disc art.
- Produces:

```c
typedef enum { PAD_ICONS_ORIGINAL, PAD_ICONS_XBOX, PAD_ICONS_NINTENDO, PAD_ICONS_COUNT } e_PadIconSet;
typedef enum { PI_L2, PI_R2, PI_L1, PI_R1, PI_TRIANGLE, PI_CIRCLE, PI_CROSS, PI_SQUARE,
               PI_SELECT, PI_L3, PI_R3, PI_START,                    /* disc art, u 244, v = id * 16 */
               PI_DPAD_UP, PI_DPAD_RIGHT, PI_DPAD_DOWN, PI_DPAD_LEFT, /* u 244, v 192..240 (blank on disc) */
               PI_LS_UP, PI_LS_RIGHT, PI_LS_DOWN, PI_LS_LEFT,        /* u 228, v 0..48 (new column) */
               PI_COUNT } e_PadIcon;
int         PadIcons_ForBind(const char* sdlName);           /* e_PadIcon or -1 */
void        PadIcons_UV(int icon, int* u, int* v);           /* 12x12 cell in the OPTION2 tpage */
const char* PadIcons_Fallback(int icon, const char* sdlName); /* "D-PAD_U", "LS_L", else sanitized pretty name */
int         PadIcons_CellHasArt(const unsigned char* rgba, int w, int h, int u, int v); /* any alpha > 0 in the cell, PNG scaled from 128x256 */
int         PadIcons_HasArt(int icon);                       /* for the active set, after Apply */
const char* PadIcons_SetLabel(int set);                      /* "Original" / "Xbox" / "Nintendo" */
void        PadIcons_Step(int dir);                          /* cycles g_PcConfig.gamepadIcons, saves, applies */
void        PadIcons_Apply(void);
```

- [ ] **Step 1: Write failing tests** in `pc_pad_icons_test.c`:
  - `test_pad_icons_map`: `"a"` gives `PI_CROSS`, `"y"` gives `PI_TRIANGLE`, `"lefttrigger"` gives `PI_L2`, `"back"` gives `PI_SELECT`, `"dpleft"` gives `PI_DPAD_LEFT`, `"touchpad"` gives -1.
  - `test_pad_icons_uv`: `PI_START` gives (244, 176), `PI_DPAD_UP` gives (244, 192), `PI_LS_LEFT` gives (228, 48).
  - `test_pad_icons_fallback`: `PI_DPAD_UP` gives `"D-PAD_U"`, `PI_LS_RIGHT` gives `"LS_R"`, `(-1, "touchpad")` gives `"Touchpad"`.
  - `test_pad_icons_has_art`: a 128x256 RGBA buffer with one opaque pixel inside the `PI_CIRCLE` cell gives 1 for that cell and 0 for `PI_DPAD_UP`; the same at 256x512 (2x PNG) gives the same answers; a NULL buffer gives 0.
- [ ] **Step 2: Run, verify fail.** Implement `pc_pad_icons.c`; run, verify pass.
- [ ] **Step 3: Implement `PadIcons_Apply`:**
  - Load `gamedata/icons/pad_<original|xbox|nintendo>.png`, opening it the way `font_region.c` opens `FONT16.png`.
  - On success: register over the OPTION2 rect and compute `HasArt` per icon from the decoded alpha.
  - On a missing or broken file: drop the override, so the disc art shows. `HasArt` is then 1 for `PI_L2..PI_START` and 0 for the rest.
  - Log the outcome once with `SH_DBG`.
- [ ] **Step 4: Write `pc_port/docs/PadIcons.md`.** Cover: file names, 128x256 size (or any 2x/4x multiple), the 12x12 cell grid with u/v for each `e_PadIcon`, which cells are new art, and that empty cells fall back to text.
- [ ] **Step 5: Build, run tests.**
- [ ] **Step 6: Commit** `Native menus: switchable gamepad icon sets`.

---

### Task 7: Native Controls screen

**Files:**
- Create: `pc_port/include/pc_controls_screen.h`, `pc_port/src/pc_controls_screen.c`
- Modify: `src/screens/options/options.c:4282-4333` (the PC block opens the native screen instead of `Pc_BindPanel_*`)

**Interfaces:**
- Consumes: Tasks 1, 2, 5, 6. `DbgOverlay_ToastLine` (`dbg_overlay.h`). `Pc_MouseCursor_*`.
- Produces:

```c
void Pc_ControlsScreen_Open(void);    /* scheme from g_DebugThirdPersonCam, as the panel does */
int  Pc_ControlsScreen_Update(void);  /* 1 while open, 0 once the player leaves */
void Pc_ControlsScreen_Draw(void);
```

- [ ] **Step 1: Implement the screen.** It runs in Options' 224-line mode, so all text is TALL. Starting layout in 240-line space (calibrate against `Mockup Controls.png`):
  - **Background:** `Nm_BlackBackgroundDraw`.
  - **Gamepad Icons row (y 16):** `Nm_BulletDraw` + `"Gamepad_Icons"` at x 28. `PadIcons_SetLabel(...)` with `Nm_ValueArrowsDraw` right-aligned near x 300. Left/right, or confirm, call `PadIcons_Step`.
  - **Headers:** `Nm_RuleDraw(grey)` at y 34. Headers at y 38: `"Function"` centred x 70, `"Keyboard"` centred x 160, `"Controller"` centred x 260. The header of the selected cell's column is Gold, the others LightGrey. Grey rule at y 56.
  - **Action rows:** 9 visible from y 62, pitch 16, an `NmScroll` over `BindCore_RowCount()` rows.
    - Label right-aligned at x 118 in the `_` dialect (spaces in `BcRow.label` become `_`), then `":"`.
    - KB1 at x 124 and KB2 at x 182, each fitted to 54 px. The builder may shorten long SDL key names (e.g. `Left Shift` to `L-Shift`) so they fit.
    - PAD1 at x 240 and PAD2 at x 270: the icon from `PadIcons_ForBind` if `PadIcons_HasArt`, else `PadIcons_Fallback` text fitted to 28 px. Icons are 12x12 sprites from the OPTION2 atlas (`g_ControllerButtonAtlasImg`, tpage/clut as `Options_ControllerMenu_ButtonIconsDraw` at `options.c:4895`).
    - Unbound cells show `"-"`. Duplicates are Red.
    - Move rows' controller cells show the LS and D-pad icons for that direction (`PI_LS_*`, `PI_DPAD_*`, or their text), DarkGrey and not selectable. Cells that do not exist draw nothing and are skipped by left/right.
  - **Selection:** `Nm_HighlightDraw` under the selected row; the selected cell's text is Gold. Scroll arrows sit above the first and below the last visible row.
  - **Reset row:** grey rule, then `Nm_BulletDraw` + `"Reset"` at y 218.
  - **Input:** `BindNav_Poll` (raw SDL, so a bad bind cannot lock the player out) plus mouse in 240-line space. Up/down move rows (Gamepad Icons, the actions, Reset); left/right move cells or step the icon set. Hover selects a cell; left click activates it; right click clears; the wheel scrolls.
  - **Activate a bind cell:**
    - Unusable cell, or a pad cell with `!BindInput_PadConnected()`: `Sfx_MenuError`.
    - Otherwise: `Sfx_MenuConfirm`, `BindCore_FormatRebindToast` sent to `DbgOverlay_ToastLine`, then `BindCapture_Begin`. While waiting the cell shows `"..."`. On a name, `BindCore_Set` + `Sfx_MenuConfirm`; on cancel, `Sfx_MenuCancel`. Afterwards `BindNav_Reset`.
  - **Clear** (`BN_CLEAR` / right click): `BindCore_Set(..., "NONE")` + `Sfx_MenuCancel`, or `Sfx_MenuError` on an unusable cell.
  - **Reset:** `NmConfirm_Open`; `NmConfirm_Draw` with `"Reset_controls?"`; Yes calls `BindCore_ResetScheme`.
  - **Leave:** `BN_BACK` or cancel leaves (`Sfx_MenuCancel`), and `Pc_ControlsScreen_Update` returns 0.
- [ ] **Step 2: Hook into Options.** In the `options.c` PC block, replace `Pc_BindPanel_Open/Update` with `Pc_ControlsScreen_Open/Update` and call `Pc_ControlsScreen_Draw()` plus `Pc_MouseCursor_Draw()` each frame while open. Keep the existing fade and leave handling. `pc_quick_options.c` keeps using the GL panel; `grep -rn "Pc_BindPanel" src` must find nothing.
- [ ] **Step 3: Build.** Expected: clean build; existing tests still pass.
- [ ] **Step 4: Commit** `Native menus: native Controls screen`.
- [ ] **Step 5: USER PLAYTEST GATE B.** Ask the user to check, then wait:
  - **Layout vs the mockup:** columns, colours, scrolling through all 24 actions.
  - **Rebinding:** rebind a primary key, a secondary key and a pad button, with the toast text. Esc cancels. The controller timeout works.
  - **Mouse binds:** a mouse-button bind on a row that allows it.
  - **Clear and duplicates:** clear works; a duplicate turns Red.
  - **Reset:** Yes/No.
  - **Gamepad Icons:** cycling the setting (only Xbox/Nintendo PNGs exist so far; Original uses the disc art + text).
  - **Alt-cam scheme:** entering from an alt-cam camera edits the alt-cam scheme.
  - **F10 > Key Binds:** still opens the GL panel and works.

---

### Task 8: Localization

**Files:**
- Modify: `pc_port/src/lang_menu.c` (`s_MenuTr`, de/fr/es/it), `pc_port/src/lang_ru_menu.inc`, `pc_port/src/lang_jpn_pcopt.inc` (or the JP menu include that holds PC strings), `pc_port/localization/extract_text.py` and the supplements/packs it feeds (`pc_port/assets/gamedata/lang/*.lang`), following `pc_port/localization/README.md`

**Interfaces:**
- Consumes: every literal drawn by Tasks 3, 4, 6, 7.
- Not translated: the rebind toast. `DbgOverlay_ToastLine` draws in the console's 8x8 ASCII font, which cannot show accented or Cyrillic letters. This departs from the spec's list and is pending the user's OK (see handoff).
- Produces: translations for these literals (exact keys):
  - Title and achievements: `"ACHIEVEMENTS"`, `"Back"`, `"UNLOCKED:"`, `"LOCKED"`, `"Points"`, `"Point"`, `"points"`.
  - Controls headers, prompt and icon sets: `"Gamepad_Icons"`, `"Original"`, `"Xbox"`, `"Nintendo"`, `"Function"`, `"Keyboard"`, `"Controller"`, `"Reset"`, `"Reset_controls?"`, `"Yes"`, `"No"`.
  - The 24 action labels: `"Move_Up"`, `"Move_Down"`, `"Turn_Left"`, `"Turn_Right"`, `"Action_/_Shoot"`, `"Flashlight"`, `"Map"`, `"Run"`, `"Sidestep_Left"`, `"Sidestep_Right"`, `"View"`, `"Aim"`, `"Pause"`, `"Inventory"`, `"Reload"`, `"Cycle_Weapons"`, `"Quick_Heal"`, `"Quick_Turn"`, `"Rear_Look"`, `"Change_Camera"`, `"Swap_Shoulder"`, `"Quick_Options"`, `"Quick_Save"`, `"Quick_Load"`.

- [ ] **Step 1: Check key collisions.** `s_MenuTr` is a global exact-match table: grep every literal above in `lang_menu.c`, `lang_ru_menu.inc` and the packs. Where a key already exists with a different meaning (likely `"Map"`, the New-Game map row; also check `"View"`, `"Run"`, `"Reset"`), keep the existing translation if it reads correctly for both uses; otherwise give the new use a distinct literal and change the draw site to match.
- [ ] **Step 2: Add the translations.** Reuse the existing `QUICK.*` translations of the bind-panel labels (`lang_quick_pal.inc`, `lang_quick_cjk.inc`, the packs' `QUICK.` keys) as the source text, converted to the menu dialect (`_` for space, Latin-1 bytes per the rules at the top of `lang_menu.c`). Polish and Cyrillic go into the packs and RU include per the README.
- [ ] **Step 3: Regenerate** the EN template / packs with `extract_text.py` per the README; check the diff only adds the new keys.
- [ ] **Step 4: Build.**
- [ ] **Step 5: Commit** `Native menus: localize the new screens`.
- [ ] **Step 6: USER CHECK C.** Ask the user to switch to German, Polish and Russian and check, then wait:
  - **Title:** the title menu.
  - **Achievements:** the list and the single view.
  - **Controls:** labels fit before the colon column, and nothing overlaps.
