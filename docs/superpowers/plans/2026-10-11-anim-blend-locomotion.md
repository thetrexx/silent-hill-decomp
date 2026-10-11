# Blended 8-Way Locomotion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Opt-in PC mode (`anim_blend`) where Harry's locomotion clips blend by direction and gait, forward/back combine with strafe into diagonals, and direction changes no longer stop first, for classic tank and the TPS/OTS/FPS cameras.

**Architecture:** A pure math module (weights, phase, weighted multi-clip pose sampler) plus a glue module (intent, diagonal motion, ownership fade, pose application). Classic keeps the native `Player_LowerBodyUpdate` machine untouched and gains a pre-switch shortcut hook. The PC shim gains phase-continuous clip starts and ported wall-stop/skid/stumble. One motion hook at the end of `Player_LogicUpdate` and one pose hook in `Player_AnimUpdate` serve both paths.

**Tech Stack:** C (gcc 16, MSYS2 MINGW64, CMake + Ninja), decomp headers, PsyCross types, ctest for unit tests; C# WinForms (.NET Framework 4.7.2, MSBuild) for the launcher.

**Spec:** `docs/superpowers/specs/2026-10-11-anim-blend-locomotion-design.md`

## Global Constraints

- Default off: `anim_blend=0` must run no new code path (every call site is gated; the pose hook returns at ownership 0).
- Attract demos (`SysFlag_DemoActive`) always behave as if `anim_blend=0`.
- No edits inside the cases of `Player_LowerBodyUpdate`'s state switch.
- Logging: `SH_DBG(...)` only, never `fprintf(stderr, ...)`.
- Comments only for non-obvious WHY; no comments that restate the code.
- All game-code edits under `#ifdef SH_PC_PORT`.
- Build (from any shell): `MSYSTEM=MINGW64 "C:/msys64/usr/bin/bash.exe" -lc 'cd /d/GitHub/silent-hill-decomp/pc_port/build && cmake --build . 2>&1 | tail -25'`
- Unit-test build dir: `pc_port/build-tests` (git-ignored by `build-*/`).
- Log file: newest `pc_port/build/SilentHill_<timestamp>.log`; read with `grep -a`.
- The user runs the game (`pc_port/build/SilentHillPC.exe`); in-game checks are handed to them with exact steps.
- Branch: `feat/anim-blend`. Commit after every task. Never push to `upstream`.

## Review Focus

1. Toggling `Anim_Blending` off mid-walk: Harry's pose fades back to native within ~0.12 s, with no frozen legs and no pose pop. Pinned by `test_approach` (Task 1) and the toggle-off in-game check (Task 4 Step 8).
2. Cutscene or door while moving: a scripted walk (control disabled) must show the native anim, not a stale blended pose. Pinned by the `g_Player_DisableControl` gate in Task 4 and in-game check in Task 4.
3. Rapid direction flipping (left, right, forward each frame): no state ping-pong lock or softlock (the walk-in-place class of bug). Pinned by the shortcut guard on `g_Player_IsInWalkToRunTransition` in Task 6 and in-game check in Task 6.
4. Analog stick at an exact axis angle (0/90/180/-90): pure directions keep native/shim motion, no diagonal override. Pinned by the axis tests in Task 1 and the `fabsf(sin)` gate in Task 7.
5. 30 fps vs 240 fps: weight easing and phase advance are dt-based, so blends take the same wall time. Pinned by the split-step easing test in Task 1.

---

### Task 1: Math module — weights, easing, phase

**Files:**
- Create: `pc_port/src/pc_loco_blend_math.h`
- Create: `pc_port/src/pc_loco_blend_math.c`
- Create: `pc_port/tests/pc_loco_blend_test.c`
- Modify: `pc_port/CMakeLists.txt` (inside `if(BUILD_TESTING)`, after the `add_test(NAME pc_big_tmd_test ...)` line)

**Interfaces:**
- Produces:
  - `typedef enum _PcLocoClip { PcLocoClip_WalkForward = 0, PcLocoClip_WalkBackward = 1, PcLocoClip_SidestepLeft = 2, PcLocoClip_SidestepRight = 3, PcLocoClip_RunForward = 4, PcLocoClip_RunLeft = 5, PcLocoClip_RunRight = 6, PcLocoClip_Count = 7 } e_PcLocoClip;`
  - `typedef struct _PcLocoSample { s32 keyframe0; s32 keyframe1; s32 alpha; float weight; } s_PcLocoSample;` (`alpha` is Q12)
  - `void PcLocoMath_TargetWeights(float angleDeg, float gait, float out[PcLocoClip_Count]);` angle: 0 forward, +90 right, 180 back, -90 left
  - `float PcLocoMath_Approach(float cur, float target, float maxStep);`
  - `void PcLocoMath_EaseWeights(float cur[PcLocoClip_Count], const float target[PcLocoClip_Count], float maxStep);`
  - `float PcLocoMath_Fract(float x);` result in [0, 1)
  - `float PcLocoMath_ClipPhase(s32 startKf, s32 endKf, s32 timeQ12);`
  - `void PcLocoMath_ClipSample(s32 startKf, s32 endKf, float phase, s_PcLocoSample* out);` (sets keyframe0/keyframe1/alpha, leaves weight)

- [ ] **Step 1: Write the header**

`pc_port/src/pc_loco_blend_math.h`:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_LOCO_BLEND_MATH_H
#define PC_LOCO_BLEND_MATH_H

#include "common.h"
#include <psyq/libgs.h>
#include "bodyprog/formats/anm.h"

typedef enum _PcLocoClip
{
    PcLocoClip_WalkForward   = 0,
    PcLocoClip_WalkBackward  = 1,
    PcLocoClip_SidestepLeft  = 2,
    PcLocoClip_SidestepRight = 3,
    PcLocoClip_RunForward    = 4,
    PcLocoClip_RunLeft       = 5,
    PcLocoClip_RunRight      = 6,
    PcLocoClip_Count         = 7
} e_PcLocoClip;

typedef struct _PcLocoSample
{
    s32   keyframe0;
    s32   keyframe1;
    s32   alpha; /* Q12 */
    float weight;
} s_PcLocoSample;

void  PcLocoMath_TargetWeights(float angleDeg, float gait, float out[PcLocoClip_Count]);
float PcLocoMath_Approach(float cur, float target, float maxStep);
void  PcLocoMath_EaseWeights(float cur[PcLocoClip_Count], const float target[PcLocoClip_Count], float maxStep);
float PcLocoMath_Fract(float x);
float PcLocoMath_ClipPhase(s32 startKf, s32 endKf, s32 timeQ12);
void  PcLocoMath_ClipSample(s32 startKf, s32 endKf, float phase, s_PcLocoSample* out);

#endif
```

- [ ] **Step 2: Write the failing test**

`pc_port/tests/pc_loco_blend_test.c`:

```c
/* pc_loco_blend_test.c — pure math of the blended locomotion layer
 * (pc_loco_blend_math.c): direction/gait weights, easing, clip phase. */
/* NDEBUG is set by the RelWithDebInfo build; this test IS its assertions. */
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "pc_loco_blend_math.h"

static int near(float a, float b, float eps)
{
    return fabsf(a - b) <= eps;
}

static float weight_sum(const float* w)
{
    float s = 0.0f;
    int   i;
    for (i = 0; i < PcLocoClip_Count; i++) s += w[i];
    return s;
}

static void test_weights_axes(void)
{
    float w[PcLocoClip_Count];

    PcLocoMath_TargetWeights(0.0f, 0.0f, w);
    assert(near(w[PcLocoClip_WalkForward], 1.0f, 1e-5f));
    PcLocoMath_TargetWeights(90.0f, 0.0f, w);
    assert(near(w[PcLocoClip_SidestepRight], 1.0f, 1e-5f));
    PcLocoMath_TargetWeights(-90.0f, 0.0f, w);
    assert(near(w[PcLocoClip_SidestepLeft], 1.0f, 1e-5f));
    PcLocoMath_TargetWeights(180.0f, 0.0f, w);
    assert(near(w[PcLocoClip_WalkBackward], 1.0f, 1e-5f));
    PcLocoMath_TargetWeights(0.0f, 1.0f, w);
    assert(near(w[PcLocoClip_RunForward], 1.0f, 1e-5f));
    PcLocoMath_TargetWeights(90.0f, 1.0f, w);
    assert(near(w[PcLocoClip_RunRight], 1.0f, 1e-5f));
    PcLocoMath_TargetWeights(-90.0f, 1.0f, w);
    assert(near(w[PcLocoClip_RunLeft], 1.0f, 1e-5f));
    /* No run-backward clip: back + run stays on WalkBackward. */
    PcLocoMath_TargetWeights(180.0f, 1.0f, w);
    assert(near(w[PcLocoClip_WalkBackward], 1.0f, 1e-5f));
}

static void test_weights_diagonals(void)
{
    float w[PcLocoClip_Count];

    PcLocoMath_TargetWeights(45.0f, 0.0f, w);
    assert(near(w[PcLocoClip_WalkForward], 0.5f, 1e-5f));
    assert(near(w[PcLocoClip_SidestepRight], 0.5f, 1e-5f));

    PcLocoMath_TargetWeights(135.0f, 0.0f, w);
    assert(near(w[PcLocoClip_SidestepRight], 0.5f, 1e-5f));
    assert(near(w[PcLocoClip_WalkBackward], 0.5f, 1e-5f));

    PcLocoMath_TargetWeights(-45.0f, 0.5f, w);
    assert(near(w[PcLocoClip_WalkForward], 0.25f, 1e-5f));
    assert(near(w[PcLocoClip_SidestepLeft], 0.25f, 1e-5f));
    assert(near(w[PcLocoClip_RunForward], 0.25f, 1e-5f));
    assert(near(w[PcLocoClip_RunLeft], 0.25f, 1e-5f));

    PcLocoMath_TargetWeights(405.0f, 0.0f, w);
    assert(near(w[PcLocoClip_WalkForward], 0.5f, 1e-5f));
    assert(near(w[PcLocoClip_SidestepRight], 0.5f, 1e-5f));
}

static void test_weights_sum_to_one(void)
{
    static const float gaits[] = { 0.0f, 0.3f, 1.0f, -2.0f, 3.0f };
    float              w[PcLocoClip_Count];
    float              a;
    int                g, i;

    for (g = 0; g < (int)(sizeof(gaits) / sizeof(gaits[0])); g++)
    {
        for (a = -360.0f; a <= 720.0f; a += 7.5f)
        {
            PcLocoMath_TargetWeights(a, gaits[g], w);
            assert(near(weight_sum(w), 1.0f, 1e-4f));
            for (i = 0; i < PcLocoClip_Count; i++) assert(w[i] >= 0.0f);
        }
    }
}

static void test_approach(void)
{
    assert(near(PcLocoMath_Approach(0.0f, 1.0f, 0.3f), 0.3f, 1e-6f));
    assert(near(PcLocoMath_Approach(0.9f, 1.0f, 0.3f), 1.0f, 1e-6f));
    assert(near(PcLocoMath_Approach(1.0f, 0.0f, 0.25f), 0.75f, 1e-6f));
    assert(near(PcLocoMath_Approach(0.5f, 0.5f, 0.25f), 0.5f, 1e-6f));
}

static void test_ease_reaches_target_framerate_independent(void)
{
    float cur[PcLocoClip_Count];
    float tgt[PcLocoClip_Count];
    float cur8[PcLocoClip_Count];
    int   i;

    memset(cur, 0, sizeof(cur));
    memset(tgt, 0, sizeof(tgt));
    cur[PcLocoClip_WalkForward]  = 1.0f;
    tgt[PcLocoClip_SidestepRight] = 1.0f;
    memcpy(cur8, cur, sizeof(cur));

    PcLocoMath_EaseWeights(cur, tgt, 0.25f);
    assert(near(cur[PcLocoClip_WalkForward], 0.75f, 1e-5f));
    assert(near(cur[PcLocoClip_SidestepRight], 0.25f, 1e-5f));
    for (i = 0; i < 3; i++) PcLocoMath_EaseWeights(cur, tgt, 0.25f);
    for (i = 0; i < 8; i++) PcLocoMath_EaseWeights(cur8, tgt, 0.125f);
    for (i = 0; i < PcLocoClip_Count; i++)
    {
        assert(near(cur[i], tgt[i], 1e-5f));
        assert(near(cur8[i], tgt[i], 1e-5f));
    }
}

static void test_fract(void)
{
    assert(near(PcLocoMath_Fract(1.25f), 0.25f, 1e-6f));
    assert(near(PcLocoMath_Fract(-0.25f), 0.75f, 1e-6f));
    assert(near(PcLocoMath_Fract(3.0f), 0.0f, 1e-6f));
}

static void test_clip_sample(void)
{
    s_PcLocoSample s;
    s32            t;

    PcLocoMath_ClipSample(1, 23, 0.0f, &s);
    assert(s.keyframe0 == 1 && s.keyframe1 == 2 && s.alpha == 0);

    /* Last keyframe wraps to the first, like Anim_PlaybackLoop. */
    PcLocoMath_ClipSample(1, 23, 22.5f / 23.0f, &s);
    assert(s.keyframe0 == 23 && s.keyframe1 == 1);
    assert(s.alpha >= 2046 && s.alpha <= 2050);

    PcLocoMath_ClipSample(1, 23, 1.0f, &s);
    assert(s.keyframe0 == 1);

    PcLocoMath_ClipSample(1, 23, -0.25f, &s);
    assert(s.keyframe0 == 18 && s.keyframe1 == 19);
    assert(s.alpha >= 1022 && s.alpha <= 1026);

    assert(near(PcLocoMath_ClipPhase(1, 23, (12 << 12) + 2048), 0.5f, 1e-5f));

    for (t = (1 << 12); t < (24 << 12); t += 777)
    {
        PcLocoMath_ClipSample(1, 23, PcLocoMath_ClipPhase(1, 23, t), &s);
        assert(s.keyframe0 == (t >> 12));
        assert(s.keyframe0 >= 1 && s.keyframe0 <= 23);
        assert(s.keyframe1 >= 1 && s.keyframe1 <= 23);
        assert(abs(s.alpha - (t & 4095)) <= 2);
    }
}

int main(void)
{
    test_weights_axes();
    test_weights_diagonals();
    test_weights_sum_to_one();
    test_approach();
    test_ease_reaches_target_framerate_independent();
    test_fract();
    test_clip_sample();
    printf("pc_loco_blend_test: all passed\n");
    return 0;
}
```

- [ ] **Step 3: Register the test target**

In `pc_port/CMakeLists.txt`, directly after `    add_test(NAME pc_big_tmd_test COMMAND pc_big_tmd_test)`:

```cmake

    # Blended locomotion math (pc_loco_blend_math.c): weights, easing, clip
    # phase and the weighted pose sampler, against a synthetic ANM.
    add_executable(pc_loco_blend_test
        tests/pc_loco_blend_test.c
        src/pc_loco_blend_math.c)
    target_compile_definitions(pc_loco_blend_test PRIVATE
        SH_PC_PORT VER_USA SKIP_ASM static_assert=_Static_assert)
    target_include_directories(pc_loco_blend_test PRIVATE
        "${CMAKE_BINARY_DIR}"
        include include/psyq_compat src
        "${DECOMP_DIR}/include" "${DECOMP_DIR}/include/decomp"
        "${PSYCROSS_DIR}/include/psx" "${PSYCROSS_DIR}/include")
    target_link_libraries(pc_loco_blend_test PRIVATE m)
    add_test(NAME pc_loco_blend_test COMMAND pc_loco_blend_test)
```

- [ ] **Step 4: Create an empty implementation and run the test to see it fail**

`pc_port/src/pc_loco_blend_math.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pc_loco_blend_math.h"
```

Run:
```
MSYSTEM=MINGW64 "C:/msys64/usr/bin/bash.exe" -lc 'cd /d/GitHub/silent-hill-decomp/pc_port && cmake -S . -B build-tests -G Ninja -DBUILD_TESTING=ON -DSH_BUILD_MAP_DLLS=OFF > /dev/null && cmake --build build-tests --target pc_loco_blend_test 2>&1 | tail -15'
```
Expected: link FAIL with `undefined reference to 'PcLocoMath_TargetWeights'` (and the other functions). If instead a header error appears (`common.h`, `libgs.h`, `anm.h`), fix the includes in `pc_loco_blend_math.h` until only undefined-reference errors remain.

- [ ] **Step 5: Implement**

Replace `pc_port/src/pc_loco_blend_math.c` with:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <string.h>

#include "pc_loco_blend_math.h"

/* Direction order: forward, right, back, left. The run set has no back clip
 * (the original game has no run-backward), so back stays on WalkBackward. */
static const e_PcLocoClip WALK_DIRS[4] = {
    PcLocoClip_WalkForward, PcLocoClip_SidestepRight, PcLocoClip_WalkBackward, PcLocoClip_SidestepLeft
};
static const e_PcLocoClip RUN_DIRS[4] = {
    PcLocoClip_RunForward, PcLocoClip_RunRight, PcLocoClip_WalkBackward, PcLocoClip_RunLeft
};

void PcLocoMath_TargetWeights(float angleDeg, float gait, float out[PcLocoClip_Count])
{
    float a;
    float t;
    s32   sector;
    s32   next;
    s32   i;

    for (i = 0; i < PcLocoClip_Count; i++) out[i] = 0.0f;

    if (gait < 0.0f) gait = 0.0f;
    if (gait > 1.0f) gait = 1.0f;

    a = fmodf(angleDeg, 360.0f);
    if (a < 0.0f) a += 360.0f;

    sector = (s32)(a / 90.0f);
    if (sector > 3) sector = 3;
    t    = (a - (float)sector * 90.0f) / 90.0f;
    next = (sector + 1) & 3;

    out[WALK_DIRS[sector]] += (1.0f - gait) * (1.0f - t);
    out[WALK_DIRS[next]]   += (1.0f - gait) * t;
    out[RUN_DIRS[sector]]  += gait * (1.0f - t);
    out[RUN_DIRS[next]]    += gait * t;
}

float PcLocoMath_Approach(float cur, float target, float maxStep)
{
    if (cur < target)
    {
        cur += maxStep;
        return (cur > target) ? target : cur;
    }
    cur -= maxStep;
    return (cur < target) ? target : cur;
}

void PcLocoMath_EaseWeights(float cur[PcLocoClip_Count], const float target[PcLocoClip_Count], float maxStep)
{
    float sum = 0.0f;
    s32   i;

    for (i = 0; i < PcLocoClip_Count; i++)
    {
        cur[i] = PcLocoMath_Approach(cur[i], target[i], maxStep);
        sum   += cur[i];
    }
    if (sum > 0.0f)
    {
        for (i = 0; i < PcLocoClip_Count; i++) cur[i] /= sum;
    }
}

float PcLocoMath_Fract(float x)
{
    float f = x - floorf(x);
    return (f >= 1.0f) ? 0.0f : f;
}

float PcLocoMath_ClipPhase(s32 startKf, s32 endKf, s32 timeQ12)
{
    s32 span = endKf - startKf + 1;

    return PcLocoMath_Fract((float)(timeQ12 - (startKf << 12)) / ((float)span * 4096.0f));
}

void PcLocoMath_ClipSample(s32 startKf, s32 endKf, float phase, s_PcLocoSample* out)
{
    s32   span = endKf - startKf + 1;
    float t    = PcLocoMath_Fract(phase) * (float)span;
    s32   whole = (s32)t;

    if (whole >= span) whole = span - 1;

    out->keyframe0 = startKf + whole;
    out->keyframe1 = (out->keyframe0 + 1 > endKf) ? startKf : out->keyframe0 + 1;
    out->alpha     = (s32)((t - (float)whole) * 4096.0f);
    if (out->alpha < 0) out->alpha = 0;
    if (out->alpha > 4095) out->alpha = 4095;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run:
```
MSYSTEM=MINGW64 "C:/msys64/usr/bin/bash.exe" -lc 'cd /d/GitHub/silent-hill-decomp/pc_port && cmake --build build-tests --target pc_loco_blend_test 2>&1 | tail -5 && cd build-tests && ctest -R pc_loco_blend_test --output-on-failure'
```
Expected: `pc_loco_blend_test: all passed` and `100% tests passed`.

- [ ] **Step 7: Build the game to confirm the new file compiles in the exe**

Run the Global Constraints build command. Expected: no errors (the new `.c` is picked up by the `PC_PORT_SOURCES` glob; if Ninja does not see it, run `cmake ..` in `pc_port/build` first).

- [ ] **Step 8: Commit**

```bash
git add pc_port/src/pc_loco_blend_math.h pc_port/src/pc_loco_blend_math.c pc_port/tests/pc_loco_blend_test.c pc_port/CMakeLists.txt
git commit -m "Anim blend: direction weights, easing and clip phase math

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Math module — weighted pose sampler

**Files:**
- Modify: `pc_port/src/pc_loco_blend_math.h`
- Modify: `pc_port/src/pc_loco_blend_math.c`
- Modify: `pc_port/tests/pc_loco_blend_test.c`

**Interfaces:**
- Consumes: `s_PcLocoSample` (Task 1).
- Produces: `void PcLocoMath_SamplePose(const s_AnmHeader* hdr, GsCOORDINATE2* coords, u32 boneMask, const s_PcLocoSample* samples, s32 count, float nativeWeight);` Bone i is written only if `boneMask & (1u << i)` and `i >= 1`. `coords` is indexed by bone (coords[0] = root). Final value = `nativeWeight * coords(before) + sum(sample.weight * decoded)`; callers make weights + nativeWeight sum to 1.

- [ ] **Step 1: Add the declaration**

In `pc_loco_blend_math.h`, before `#endif`:

```c
void PcLocoMath_SamplePose(const s_AnmHeader* hdr, GsCOORDINATE2* coords, u32 boneMask,
                           const s_PcLocoSample* samples, s32 count, float nativeWeight);
```

- [ ] **Step 2: Write the failing tests**

In `pc_loco_blend_test.c`, add above `main`:

```c
/* --- synthetic ANM: root + bone 1 (translation 0, rotation 0) + bone 2
 * (rotation 1 only), 3 keyframes. Layout per keyframe: 1 translation (3 bytes)
 * then 2 rotation matrices (9 bytes each), as Anim_BoneUpdate reads it. ---- */
#define T_BONES   3
#define T_KF_SIZE (1 * 3 + 2 * 9)
#define T_KF_COUNT 3

static union { u32 align; u8 bytes[512]; } g_anm;

static const s8 ROT_ID[9]  = { 127, 0, 0, 0, 127, 0, 0, 0, 127 };
static const s8 ROT_Y90[9] = { 0, 0, 127, 0, 127, 0, -127, 0, 0 };

static s_AnmHeader* build_anm(void)
{
    s_AnmHeader* h = (s_AnmHeader*)g_anm.bytes;
    u8*          data;
    s32          k;

    memset(g_anm.bytes, 0, sizeof(g_anm.bytes));
    h->boneCount            = T_BONES;
    h->translationBoneCount = 1;
    h->rotationBoneCount    = 2;
    h->keyframeDataSize     = T_KF_SIZE;
    h->keyframeCount        = T_KF_COUNT;
    h->scaleLog2            = 2;
    h->rootYOffset          = 5;
    h->dataOffset           = (u16)(sizeof(s_AnmHeader) + T_BONES * sizeof(s_AnmBindPose));

    h->bindPoses[1].parentBone         = 0;
    h->bindPoses[1].rotationDataIdx    = 0;
    h->bindPoses[1].translationDataIdx = 0;
    h->bindPoses[2].parentBone         = 1;
    h->bindPoses[2].rotationDataIdx    = 1;
    h->bindPoses[2].translationDataIdx = -1;

    data = g_anm.bytes + h->dataOffset;
    for (k = 0; k < T_KF_COUNT; k++)
    {
        s8* f = (s8*)(data + k * T_KF_SIZE);
        f[0] = (s8)(10 + k * 4);
        f[1] = (s8)(-20 + k * 2);
        f[2] = (s8)(30 - k * 6);
        memcpy(f + 3, ROT_ID, 9);
        memcpy(f + 12, (k == 1) ? ROT_Y90 : ROT_ID, 9);
    }
    return h;
}

/* Mirror of Anim_BoneUpdate's decode for a non-player model. */
static void ref_decode(const s_AnmHeader* h, GsCOORDINATE2* c, s32 k0, s32 k1, s32 alpha)
{
    const u8* data = (const u8*)h + h->dataOffset;
    const s8* f0   = (const s8*)(data + h->keyframeDataSize * k0);
    const s8* f1   = (const s8*)(data + h->keyframeDataSize * k1);
    s32       b, i, j;

    for (b = 1; b < h->boneCount; b++)
    {
        const s_AnmBindPose* bp = &h->bindPoses[b];
        if (bp->translationDataIdx >= 0)
        {
            const s8* t0 = f0 + bp->translationDataIdx * 3;
            const s8* t1 = f1 + bp->translationDataIdx * 3;
            for (i = 0; i < 3; i++)
            {
                c[b].coord.t[i] = (t0[i] << h->scaleLog2) +
                                  (((t1[i] - t0[i]) * alpha) >> (12 - h->scaleLog2));
            }
            if (bp->translationDataIdx == 0) c[b].coord.t[1] -= h->rootYOffset;
        }
        if (bp->rotationDataIdx >= 0)
        {
            const s8* r0 = f0 + h->translationBoneCount * 3 + bp->rotationDataIdx * 9;
            const s8* r1 = f1 + h->translationBoneCount * 3 + bp->rotationDataIdx * 9;
            for (i = 0; i < 3; i++)
                for (j = 0; j < 3; j++)
                    c[b].coord.m[i][j] = (r0[i * 3 + j] << 5) + (((r1[i * 3 + j] - r0[i * 3 + j]) * alpha) >> 7);
        }
    }
}

static void test_sampler_single_matches_native(void)
{
    s_AnmHeader*   h = build_anm();
    GsCOORDINATE2  ref[T_BONES];
    GsCOORDINATE2  got[T_BONES];
    s_PcLocoSample s = { 0, 1, 1365, 1.0f };
    s32            b;

    memset(ref, 0, sizeof(ref));
    memset(got, 0, sizeof(got));
    ref_decode(h, ref, 0, 1, 1365);
    PcLocoMath_SamplePose(h, got, 0xFFFFFFFFu, &s, 1, 0.0f);
    for (b = 1; b < T_BONES; b++)
    {
        assert(memcmp(ref[b].coord.t, got[b].coord.t, sizeof(ref[b].coord.t)) == 0);
        assert(memcmp(ref[b].coord.m, got[b].coord.m, sizeof(ref[b].coord.m)) == 0);
    }
}

static void test_sampler_two_way_average(void)
{
    s_AnmHeader*   h = build_anm();
    GsCOORDINATE2  got[T_BONES];
    s_PcLocoSample s[2] = { { 0, 1, 0, 0.5f }, { 1, 2, 0, 0.5f } };
    float          r0[3], r2[3];
    float          l0, l2, d;
    s32            i;

    memset(got, 0, sizeof(got));
    PcLocoMath_SamplePose(h, got, 0xFFFFFFFFu, s, 2, 0.0f);

    /* Translations average exactly: (40+56)/2, (-85+-77)/2, (120+96)/2. */
    assert(got[1].coord.t[0] == 48);
    assert(got[1].coord.t[1] == -81);
    assert(got[1].coord.t[2] == 108);

    /* identity + 90deg-Y averaged: rows stay orthogonal at the source length. */
    for (i = 0; i < 3; i++)
    {
        r0[i] = (float)got[2].coord.m[0][i];
        r2[i] = (float)got[2].coord.m[2][i];
    }
    l0 = sqrtf(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]);
    l2 = sqrtf(r2[0] * r2[0] + r2[1] * r2[1] + r2[2] * r2[2]);
    d  = r0[0] * r2[0] + r0[1] * r2[1] + r0[2] * r2[2];
    assert(near(l0, 4064.0f, 3.0f));
    assert(near(l2, 4064.0f, 3.0f));
    assert(fabsf(d) < 4064.0f * 4064.0f * 0.002f);
}

static void test_sampler_native_mix_and_mask(void)
{
    s_AnmHeader*   h = build_anm();
    GsCOORDINATE2  got[T_BONES];
    s_PcLocoSample s = { 0, 1, 0, 0.5f };

    memset(got, 0, sizeof(got));
    got[1].coord.t[0] = 1000;
    got[1].coord.t[1] = 1000;
    got[1].coord.t[2] = 1000;
    PcLocoMath_SamplePose(h, got, 0xFFFFFFFFu, &s, 1, 0.5f);
    assert(got[1].coord.t[0] == 520);
    assert(got[1].coord.t[1] == 458);
    assert(got[1].coord.t[2] == 560);

    memset(got, 0, sizeof(got));
    got[1].coord.t[0] = 777;
    PcLocoMath_SamplePose(h, got, 1u << 2, &s, 1, 0.5f);
    assert(got[1].coord.t[0] == 777);
}
```

Add the three calls to `main` before the `printf`:

```c
    test_sampler_single_matches_native();
    test_sampler_two_way_average();
    test_sampler_native_mix_and_mask();
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `MSYSTEM=MINGW64 "C:/msys64/usr/bin/bash.exe" -lc 'cd /d/GitHub/silent-hill-decomp/pc_port && cmake --build build-tests --target pc_loco_blend_test 2>&1 | tail -5'`
Expected: FAIL with `undefined reference to 'PcLocoMath_SamplePose'`.

- [ ] **Step 4: Implement the sampler**

Append to `pc_loco_blend_math.c`:

```c
static s32 PcLocoMath_DecodeTranslation(s8 f0, s8 f1, s32 alpha, s32 scaleLog2)
{
    return ((s32)f0 << scaleLog2) + ((((s32)f1 - (s32)f0) * alpha) >> (12 - scaleLog2));
}

static s32 PcLocoMath_DecodeRotation(s8 f0, s8 f1, s32 alpha)
{
    return ((s32)f0 << 5) + ((((s32)f1 - (s32)f0) * alpha) >> 7);
}

static float PcLocoMath_Len3(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void PcLocoMath_Normalize3(float v[3])
{
    float len = PcLocoMath_Len3(v);
    s32   i;

    if (len < 1e-6f) return;
    for (i = 0; i < 3; i++) v[i] /= len;
}

static float PcLocoMath_Dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* Lerping rotation matrices shrinks them; on a diagonal (two clips far apart)
 * that visibly shortens limbs. Re-orthogonalize, keeping handedness, and
 * restore the row lengths the sources had. */
static void PcLocoMath_Orthonormalize(float m[3][3], const float rowLen[3])
{
    float d;
    s32   i;

    PcLocoMath_Normalize3(m[0]);

    d = PcLocoMath_Dot3(m[1], m[0]);
    for (i = 0; i < 3; i++) m[1][i] -= d * m[0][i];
    PcLocoMath_Normalize3(m[1]);

    d = PcLocoMath_Dot3(m[2], m[0]);
    for (i = 0; i < 3; i++) m[2][i] -= d * m[0][i];
    d = PcLocoMath_Dot3(m[2], m[1]);
    for (i = 0; i < 3; i++) m[2][i] -= d * m[1][i];
    PcLocoMath_Normalize3(m[2]);

    for (i = 0; i < 3; i++)
    {
        m[i][0] *= rowLen[i];
        m[i][1] *= rowLen[i];
        m[i][2] *= rowLen[i];
    }
}

static short PcLocoMath_ToShort(float v)
{
    long r = lroundf(v);

    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    return (short)r;
}

void PcLocoMath_SamplePose(const s_AnmHeader* hdr, GsCOORDINATE2* coords, u32 boneMask,
                           const s_PcLocoSample* samples, s32 count, float nativeWeight)
{
    const u8* data  = (const u8*)hdr + hdr->dataOffset;
    s32       exact = (count == 1 && nativeWeight <= 0.0f && samples[0].weight == 1.0f);
    s32       boneIdx;
    s32       k;
    s32       i;
    s32       j;

    for (boneIdx = 1; boneIdx < hdr->boneCount; boneIdx++)
    {
        const s_AnmBindPose* bind = &hdr->bindPoses[boneIdx];
        GsCOORDINATE2*       c    = &coords[boneIdx];

        if (!(boneMask & (1u << boneIdx)))
        {
            continue;
        }
        c->flg = 0;

        if (bind->translationDataIdx >= 0)
        {
            float acc[3];

            for (i = 0; i < 3; i++) acc[i] = nativeWeight * (float)c->coord.t[i];
            for (k = 0; k < count; k++)
            {
                const s8* f0 = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe0) + bind->translationDataIdx * 3;
                const s8* f1 = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe1) + bind->translationDataIdx * 3;

                for (i = 0; i < 3; i++)
                {
                    s32 v = PcLocoMath_DecodeTranslation(f0[i], f1[i], samples[k].alpha, hdr->scaleLog2);
                    if (i == 1 && bind->translationDataIdx == 0)
                    {
                        v -= hdr->rootYOffset;
                    }
                    acc[i] += samples[k].weight * (float)v;
                }
            }
            for (i = 0; i < 3; i++) c->coord.t[i] = (s32)lroundf(acc[i]);
        }

        if (bind->rotationDataIdx >= 0)
        {
            float m[3][3];
            float rowLen[3];

            for (i = 0; i < 3; i++)
            {
                float row[3];
                for (j = 0; j < 3; j++)
                {
                    row[j]  = (float)c->coord.m[i][j];
                    m[i][j] = nativeWeight * row[j];
                }
                rowLen[i] = nativeWeight * PcLocoMath_Len3(row);
            }

            for (k = 0; k < count; k++)
            {
                const s8* r0 = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe0) +
                               hdr->translationBoneCount * 3 + bind->rotationDataIdx * 9;
                const s8* r1 = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe1) +
                               hdr->translationBoneCount * 3 + bind->rotationDataIdx * 9;

                for (i = 0; i < 3; i++)
                {
                    float row[3];
                    for (j = 0; j < 3; j++)
                    {
                        row[j]   = (float)PcLocoMath_DecodeRotation(r0[i * 3 + j], r1[i * 3 + j], samples[k].alpha);
                        m[i][j] += samples[k].weight * row[j];
                    }
                    rowLen[i] += samples[k].weight * PcLocoMath_Len3(row);
                }
            }

            if (!exact)
            {
                PcLocoMath_Orthonormalize(m, rowLen);
            }
            for (i = 0; i < 3; i++)
                for (j = 0; j < 3; j++)
                    c->coord.m[i][j] = PcLocoMath_ToShort(m[i][j]);
        }
    }
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `MSYSTEM=MINGW64 "C:/msys64/usr/bin/bash.exe" -lc 'cd /d/GitHub/silent-hill-decomp/pc_port && cmake --build build-tests --target pc_loco_blend_test 2>&1 | tail -5 && cd build-tests && ctest -R pc_loco_blend_test --output-on-failure'`
Expected: `pc_loco_blend_test: all passed`.

- [ ] **Step 6: Build the game**

Run the Global Constraints build command. Expected: no errors.

- [ ] **Step 7: Commit**

```bash
git add pc_port/src/pc_loco_blend_math.h pc_port/src/pc_loco_blend_math.c pc_port/tests/pc_loco_blend_test.c
git commit -m "Anim blend: weighted multi-clip pose sampler

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Config key, menu row, labels, glue skeleton, console command

**Files:**
- Modify: `pc_port/include/pc_config.h:278` (after the `control2dSnap` field)
- Modify: `pc_port/src/pc_config.c:110` (defaults) and `pc_port/src/pc_config.c:1199-1202` (parser)
- Modify: `src/screens/options/options.c:245` (`PCOPT_C`)
- Modify: `pc_port/src/lang_menu.c:216`, `pc_port/src/lang_jpn_pcopt.inc:32`, `pc_port/src/lang_ru_menu.inc:206`, `pc_port/localization/SilentHill_EN_raw.json:1380`
- Create: `pc_port/src/pc_loco_blend.h`, `pc_port/src/pc_loco_blend.c`
- Modify: `pc_port/src/pc_console_cmd.c` (help line near 519, command near 1841)

**Interfaces:**
- Produces:
  - `g_PcConfig.animBlend` (int, 0/1), `g_PcConfig.animBlendRateMs` (int, 10..1000, default 150)
  - `int Pc_LocoBlend_Enabled(void);`
  - `const char* Pc_LocoBlend_ConsoleCmd(const char* arg);` returns a status line for the console
  - In `pc_loco_blend.c`: `static s_PcLocoBlend s_lb;` state and `static float s_ClipPhaseOffset[PcLocoClip_Count];`, used by Tasks 4-7.

- [ ] **Step 1: Config fields**

`pc_port/include/pc_config.h`, after the `int control2dSnap; ...` line:

```c
    int animBlend;          /* 1 = blended 8-way locomotion for Harry (config key: anim_blend); default 0 */
    int animBlendRateMs;    /* time for a full direction/gait blend swap (config key: anim_blend_rate_ms); default 150 */
```

`pc_port/src/pc_config.c`, after `.control2dSnap           = 0, ...`:

```c
    .animBlend               = 0,
    .animBlendRateMs         = 150,
```

`pc_port/src/pc_config.c`, after the `control_2d_snap` parser branch (`g_PcConfig.control2dSnap = (atoi(value) != 0);` + `}`):

```c
        else if (strcmp(key, "anim_blend") == 0)
        {
            g_PcConfig.animBlend = (atoi(value) != 0);
        }
        else if (strcmp(key, "anim_blend_rate_ms") == 0)
        {
            int ms = atoi(value);
            g_PcConfig.animBlendRateMs = (ms < 10) ? 10 : ((ms > 1000) ? 1000 : ms);
        }
```

- [ ] **Step 2: Menu row and labels**

`src/screens/options/options.c`, in `PCOPT_C` directly after the `"2D_Controls"` row:

```c
    { "Anim_Blending",     &g_PcConfig.animBlend,        "anim_blend",             VAL_ONOFF, 2, LBL_ONOFF, NULL, 1, PCK_INT },
```

`pc_port/src/lang_menu.c`, after the `"2D_Controls"` entry:

```c
    { "Anim_Blending",     { "Anim.-Blending",     "Fusion_anim.",        "Mezcla_anim.",       "Fusione_anim."  } },
```

`pc_port/src/lang_jpn_pcopt.inc`, after the `"2D_Controls"` entry:

```c
{ "Anim_Blending",       "\203\101\203\152\203\201\203\165\203\214\203\223\203\150", }, /* アニメブレンド */
```

`pc_port/src/lang_ru_menu.inc`, after the `"2D_Controls"` entry:

```c
{ "Anim_Blending",     "Смеш._анимаций" },
```

`pc_port/localization/SilentHill_EN_raw.json`, after the `"MENU.2D_Controls": "2D_Controls",` line:

```json
 "MENU.Anim_Blending": "Anim_Blending",
```

- [ ] **Step 3: Glue skeleton**

`pc_port/src/pc_loco_blend.h`:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_LOCO_BLEND_H
#define PC_LOCO_BLEND_H

#include "game.h"
#include "bodyprog/bodyprog.h"

int         Pc_LocoBlend_Enabled(void);
const char* Pc_LocoBlend_ConsoleCmd(const char* arg);

#endif
```

`pc_port/src/pc_loco_blend.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_loco_blend.c — blended 8-way locomotion for Harry (anim_blend).
 *
 * The native lower-body machine (classic) and the PC shim (alternate cameras)
 * keep deciding states, speeds and footsteps. This layer adds diagonal motion,
 * phase-continuous clip changes and a weighted pose over the locomotion clips,
 * faded in and out against the native pose. Spec:
 * docs/superpowers/specs/2026-10-11-anim-blend-locomotion-design.md
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "bodyprog/bodyprog.h"
#include "bodyprog/math/math.h"
#include "bodyprog/player.h"
#include "bodyprog/sys/joy.h"
#include "pc_config.h"
#include "pc_loco_blend.h"
#include "pc_loco_blend_math.h"
#include "sh_log.h"

typedef struct _PcLocoBlend
{
    float weights[PcLocoClip_Count];
    float ownership;
    float phase;
    float angleDeg;
    float gait;
    s32   moving;
    s32   fresh;
    s32   trace;
    s32   traceTick;
} s_PcLocoBlend;

static s_PcLocoBlend s_lb = { { 0 }, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1, 0, 0 };

/* Shifts each clip so its first footstep plant (func_8007B924 keyframes)
 * lines up with WalkForward's. Tunable live with LOCOBLEND OFFSET. */
static float s_ClipPhaseOffset[PcLocoClip_Count] = {
    0.0f,    /* WalkForward   plants 6/18   in 1-23   */
    0.0326f, /* WalkBackward  plants 52/63  in 46-69  */
    0.3026f, /* SidestepLeft  plants 83/93  in 70-94  */
    0.3026f, /* SidestepRight plants 108/118 in 95-119 */
    0.0326f, /* RunForward    plants 31/41  in 26-45  */
    0.0903f, /* RunLeft       plants 125/131 in 121-133 */
    0.0134f, /* RunRight      plants 139/145 in 136-148 */
};

int Pc_LocoBlend_Enabled(void)
{
    return g_PcConfig.animBlend && !(g_SysWork.sysFlags & SysFlag_DemoActive);
}

const char* Pc_LocoBlend_ConsoleCmd(const char* arg)
{
    static char msg[96];
    char        word[16];
    s32         i;

    for (i = 0; arg[i] && arg[i] != ' ' && i < (s32)sizeof(word) - 1; i++)
    {
        word[i] = (char)toupper((unsigned char)arg[i]);
    }
    word[i] = '\0';

    if (strcmp(word, "OFFSET") == 0)
    {
        s32   clip  = -1;
        float value = 0.0f;

        if (sscanf(arg + i, "%d %f", &clip, &value) == 2 && clip >= 0 && clip < PcLocoClip_Count)
        {
            s_ClipPhaseOffset[clip] = PcLocoMath_Fract(value);
            snprintf(msg, sizeof(msg), "Anim blend: clip %d phase offset %.4f", clip, s_ClipPhaseOffset[clip]);
        }
        else
        {
            snprintf(msg, sizeof(msg), "usage: locoblend offset <clip 0-6> <0..1>");
        }
        return msg;
    }

    s_lb.trace = !s_lb.trace;
    snprintf(msg, sizeof(msg), "Anim blend trace %s (anim_blend=%d)", s_lb.trace ? "on" : "off", g_PcConfig.animBlend);
    return msg;
}
```

- [ ] **Step 4: Console command**

`pc_port/src/pc_console_cmd.c`: add `#include "pc_loco_blend.h"` with the file's other `pc_*.h` includes. In `HELP_LINES`, after the `" kf [n]         keyframe viewer..."` line:

```c
    " locoblend [offset c v]  anim blend trace / tune clip c foot phase",
```

Before `    } else if (strcmp(cmd, "WEATHERHZ") == 0) {`:

```c
    } else if (strcmp(cmd, "LOCOBLEND") == 0) {
        cprintf("%s", Pc_LocoBlend_ConsoleCmd(arg));
```

- [ ] **Step 5: Build**

Run the Global Constraints build command. Expected: no errors or new warnings in the touched files.

- [ ] **Step 6: In-game check (user)**

Ask the user to: launch `pc_port/build/SilentHillPC.exe`, open Options → PC page Controls, confirm an `Anim_Blending` ON/OFF row under `2D_Controls`, toggle it ON, exit the menu, then open the console and type `locoblend`. Expected console line: `Anim blend trace on (anim_blend=1)`. Then verify `pc_port/build/config.cfg` contains `anim_blend 1` (or the file's key/value syntax).

- [ ] **Step 7: Commit**

```bash
git add pc_port/include/pc_config.h pc_port/src/pc_config.c src/screens/options/options.c pc_port/src/lang_menu.c pc_port/src/lang_jpn_pcopt.inc pc_port/src/lang_ru_menu.inc pc_port/localization/SilentHill_EN_raw.json pc_port/src/pc_loco_blend.h pc_port/src/pc_loco_blend.c pc_port/src/pc_console_cmd.c
git commit -m "Anim blend: anim_blend config, Controls menu row, locoblend console command

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Pose layer, intent and phase-continuous clip start (classic visuals)

**Files:**
- Modify: `pc_port/src/pc_loco_blend.h`, `pc_port/src/pc_loco_blend.c`
- Modify: `src/bodyprog/player_control.c` — `Player_AnimUpdate` (~1366-1465) and the end of `Player_LogicUpdate` (~4132-4143); add `#include "pc_loco_blend.h"` after `#include "pc_config.h"` (line 81)

**Interfaces:**
- Consumes: Task 1/2 math, Task 3 `s_lb`, `s_ClipPhaseOffset`, `Pc_LocoBlend_Enabled`.
- Produces:
  - `void Pc_LocoBlend_StartClip(s_Model* model, s32 harryAnim);` With blend off, or when `model` is not currently on a locomotion clip, it does exactly `model->anim.status = ANIM_STATUS(harryAnim, false)` (today's behavior). Otherwise it jumps to the clip's active status at the current gait phase.
  - `void Pc_LocoBlend_MotionApply(s_SubCharacter* player);` (this task: intent only; Task 5/7 add motion)
  - `void Pc_LocoBlend_PoseApply(s_SubCharacter* player, s_PlayerExtra* extra, s_AnmHeader* anmHdr, GsCOORDINATE2* coords);`
  - Static helpers in `pc_loco_blend.c`: `Pc_LocoBlend_ClipFromAnim(s32 harryAnim)` returning `e_PcLocoClip` or -1, `Pc_LocoBlend_Info(s32 clip)`, `Pc_LocoBlend_Rate(s32 clip)`, `Pc_LocoBlend_IntentClassic(void)`, `Pc_LocoBlend_Is2dControl(void)`.

- [ ] **Step 1: Declarations**

Add to `pc_loco_blend.h` before `#endif`:

```c
void Pc_LocoBlend_StartClip(s_Model* model, s32 harryAnim);
void Pc_LocoBlend_MotionApply(s_SubCharacter* player);
void Pc_LocoBlend_PoseApply(s_SubCharacter* player, s_PlayerExtra* extra, s_AnmHeader* anmHdr, GsCOORDINATE2* coords);
```

- [ ] **Step 2: Clip helpers and StartClip**

Append to `pc_loco_blend.c`:

```c
extern int g_DebugThirdPersonCam;
extern int g_PcFpsCam;

#define LOCO_OWN_FADE_SEC 0.12f

static const s32 CLIP_ANIM[PcLocoClip_Count] = {
    HarryAnim_WalkForward, HarryAnim_WalkBackward, HarryAnim_SidestepLeft, HarryAnim_SidestepRight,
    HarryAnim_RunForward,  HarryAnim_RunLeft,      HarryAnim_RunRight
};

static s32 Pc_LocoBlend_ClipFromAnim(s32 harryAnim)
{
    s32 i;

    for (i = 0; i < PcLocoClip_Count; i++)
    {
        if (CLIP_ANIM[i] == harryAnim)
        {
            return i;
        }
    }
    return -1;
}

static s_AnimInfo* Pc_LocoBlend_Info(s32 clip)
{
    return &HARRY_BASE_ANIM_INFOS[ANIM_STATUS(CLIP_ANIM[clip], true)];
}

static float Pc_LocoBlend_Rate(s32 clip)
{
    s_AnimInfo* info = Pc_LocoBlend_Info(clip);
    s32         span = info->endKeyframeIdx - info->startKeyframeIdx + 1;
    q19_12      duration;

    if (info->hasVariableDuration)
    {
        s_Model probe;

        memset(&probe, 0, sizeof(probe));
        probe.anim.status      = ANIM_STATUS(CLIP_ANIM[clip], true);
        probe.anim.keyframeIdx = info->startKeyframeIdx;
        duration               = Player_VariableAnimDurationGet(&probe);
    }
    else
    {
        duration = info->duration.constant;
    }
    return ((float)duration / 4096.0f) / (float)span;
}

void Pc_LocoBlend_StartClip(s_Model* model, s32 harryAnim)
{
    s32            clip    = Pc_LocoBlend_ClipFromAnim(harryAnim);
    s32            curClip = Pc_LocoBlend_ClipFromAnim(ANIM_STATUS_IDX_GET(model->anim.status));
    s_AnimInfo*    info;
    s_PcLocoSample smp;

    /* From idle or an action, let the native blend-in run from the clip's
     * first keyframe; only a change between locomotion clips keeps the gait. */
    if (!Pc_LocoBlend_Enabled() || clip < 0 || curClip < 0)
    {
        model->anim.status = ANIM_STATUS(harryAnim, false);
        return;
    }

    info = Pc_LocoBlend_Info(clip);
    PcLocoMath_ClipSample(info->startKeyframeIdx, info->endKeyframeIdx, s_lb.phase + s_ClipPhaseOffset[clip], &smp);
    model->anim.status      = ANIM_STATUS(harryAnim, true);
    model->anim.time        = (smp.keyframe0 << Q12_SHIFT) + smp.alpha;
    model->anim.keyframeIdx = smp.keyframe0;
    model->anim.alpha       = Q12(0.0f);
}
```

- [ ] **Step 3: Intent (classic + 2D) and MotionApply**

Append to `pc_loco_blend.c`:

```c
static s32 Pc_LocoBlend_Is2dControl(void)
{
    return g_PcConfig.control2d && !g_PcFpsCam && !g_SysWork.playerCombat.isAiming;
}

static void Pc_LocoBlend_IntentClassic(void)
{
    s32   state = g_SysWork.playerWork.extra.lowerBodyState;
    s32   base  = (state >= PlayerLowerBodyState_Aim && state <= PlayerLowerBodyState_AimRunLeft)
                      ? state - PlayerLowerBodyState_Aim
                      : state;
    float lat   = (float)((g_Player_IsSteppingRightHold ? 1 : 0) - (g_Player_IsSteppingLeftHold ? 1 : 0));

    s_lb.moving = 1;
    s_lb.gait   = (base == PlayerLowerBodyState_RunForward || base == PlayerLowerBodyState_RunLeft ||
                   base == PlayerLowerBodyState_RunRight) ? 1.0f : 0.0f;
    switch (base)
    {
        case PlayerLowerBodyState_WalkForward:
        case PlayerLowerBodyState_RunForward:
            s_lb.angleDeg = 45.0f * lat;
            break;

        case PlayerLowerBodyState_WalkBackward:
            s_lb.angleDeg = 180.0f - 45.0f * lat;
            break;

        case PlayerLowerBodyState_SidestepLeft:
        case PlayerLowerBodyState_RunLeft:
            s_lb.angleDeg = -90.0f;
            break;

        case PlayerLowerBodyState_SidestepRight:
        case PlayerLowerBodyState_RunRight:
            s_lb.angleDeg = 90.0f;
            break;

        default:
            s_lb.moving = 0;
            break;
    }
}

void Pc_LocoBlend_MotionApply(s_SubCharacter* player)
{
    (void)player;

    if (!Pc_LocoBlend_Enabled())
    {
        s_lb.moving = 0;
        return;
    }

    if (Pc_LocoBlend_Is2dControl())
    {
        s_lb.moving   = 1;
        s_lb.angleDeg = 0.0f;
        s_lb.gait     = g_Player_IsRunning ? 1.0f : 0.0f;
    }
    else if (!g_DebugThirdPersonCam)
    {
        Pc_LocoBlend_IntentClassic();
    }
    else
    {
        /* Alternate cameras: filled in by Task 7. Until then pose from state. */
        Pc_LocoBlend_IntentClassic();
    }
}
```

- [ ] **Step 4: PoseApply**

Append to `pc_loco_blend.c`:

```c
void Pc_LocoBlend_PoseApply(s_SubCharacter* player, s_PlayerExtra* extra, s_AnmHeader* anmHdr, GsCOORDINATE2* coords)
{
    float          dt         = (float)g_DeltaTime / 4096.0f;
    s32            masterClip = Pc_LocoBlend_ClipFromAnim(ANIM_STATUS_IDX_GET(player->model.anim.status));
    s32            upperClip  = Pc_LocoBlend_ClipFromAnim(ANIM_STATUS_IDX_GET(extra->model.anim.status));
    s32            playerState = g_SysWork.playerWork.extra.state;
    s32            own;
    float          target[PcLocoClip_Count];
    s_PcLocoSample samples[PcLocoClip_Count];
    float          sampleSum = 0.0f;
    float          blendSec;
    s32            count = 0;
    s32            i;

    own = Pc_LocoBlend_Enabled() && !g_Player_DisableControl &&
          (playerState == PlayerState_None || playerState == PlayerState_Combat) &&
          (masterClip >= 0 || g_Player_IsInWalkToRunTransition);

    s_lb.ownership = PcLocoMath_Approach(s_lb.ownership, own ? 1.0f : 0.0f, dt / LOCO_OWN_FADE_SEC);
    if (s_lb.ownership <= 0.0f)
    {
        s_lb.ownership = 0.0f;
        s_lb.fresh     = 1;
        return;
    }

    if (s_lb.moving)
    {
        PcLocoMath_TargetWeights(s_lb.angleDeg, s_lb.gait, target);
    }
    else
    {
        memcpy(target, s_lb.weights, sizeof(target));
    }

    if (s_lb.fresh)
    {
        memcpy(s_lb.weights, target, sizeof(target));
        s_lb.fresh = 0;
    }
    else
    {
        blendSec = (float)g_PcConfig.animBlendRateMs / 1000.0f;
        PcLocoMath_EaseWeights(s_lb.weights, target, dt / blendSec);
    }

    if (masterClip >= 0 && ANIM_STATUS_IS_ACTIVE(player->model.anim.status))
    {
        s_AnimInfo* info = Pc_LocoBlend_Info(masterClip);
        s_lb.phase = PcLocoMath_Fract(PcLocoMath_ClipPhase(info->startKeyframeIdx, info->endKeyframeIdx,
                                                           player->model.anim.time) -
                                      s_ClipPhaseOffset[masterClip]);
    }
    else
    {
        float rate = 0.0f;
        for (i = 0; i < PcLocoClip_Count; i++)
        {
            if (s_lb.weights[i] > 0.0f) rate += s_lb.weights[i] * Pc_LocoBlend_Rate(i);
        }
        s_lb.phase = PcLocoMath_Fract(s_lb.phase + dt * rate);
    }

    for (i = 0; i < PcLocoClip_Count; i++)
    {
        s_AnimInfo* info;

        if (s_lb.weights[i] < 0.001f)
        {
            continue;
        }
        info = Pc_LocoBlend_Info(i);
        PcLocoMath_ClipSample(info->startKeyframeIdx, info->endKeyframeIdx, s_lb.phase + s_ClipPhaseOffset[i],
                              &samples[count]);
        samples[count].weight = s_lb.weights[i];
        sampleSum            += s_lb.weights[i];
        count++;
    }
    if (count == 0 || sampleSum <= 0.0f)
    {
        return;
    }
    for (i = 0; i < count; i++)
    {
        samples[i].weight = samples[i].weight / sampleSum * s_lb.ownership;
    }

    PcLocoMath_SamplePose(anmHdr, coords, HARRY_LOWER_BODY_BONE_MASK, samples, count, 1.0f - s_lb.ownership);
    if (upperClip >= 0 && !g_SysWork.playerCombat.isAiming)
    {
        PcLocoMath_SamplePose(anmHdr, coords, HARRY_UPPER_BODY_BONE_MASK, samples, count, 1.0f - s_lb.ownership);
    }
    for (i = 0; i < 3; i++)
    {
        coords[HarryBone_Torso].coord.t[i] = coords[HarryBone_Hips].coord.t[i];
    }

    if (s_lb.trace && (++s_lb.traceTick % 15) == 0)
    {
        SH_DBG("[LOCOBLEND] ang=%.0f gait=%.2f mov=%d own=%.2f ph=%.3f master=%d upper=%d state=%d "
               "w=%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f",
               s_lb.angleDeg, s_lb.gait, (int)s_lb.moving, s_lb.ownership, s_lb.phase, (int)masterClip,
               (int)upperClip, (int)g_SysWork.playerWork.extra.lowerBodyState,
               s_lb.weights[0], s_lb.weights[1], s_lb.weights[2], s_lb.weights[3],
               s_lb.weights[4], s_lb.weights[5], s_lb.weights[6]);
    }
}
```

- [ ] **Step 5: Hooks in player_control.c**

Add after `#include "pc_config.h"` (line 81):

```c
#include "pc_loco_blend.h"
```

In `Player_AnimUpdate`, in the `if (!g_Player_IsInWalkToRunTransition)` branch, replace:

```c
        animInfo = &HARRY_BASE_ANIM_INFOS[extra->model.anim.status];
        animInfo->playbackFunc(&extra->model, anmHdr, coords, animInfo);
        return;
    }
```

with:

```c
        animInfo = &HARRY_BASE_ANIM_INFOS[extra->model.anim.status];
        animInfo->playbackFunc(&extra->model, anmHdr, coords, animInfo);
#ifdef SH_PC_PORT
        Pc_LocoBlend_PoseApply(player, extra, anmHdr, coords);
#endif
        return;
    }
```

At the end of `Player_AnimUpdate`, replace:

```c
        g_Player_IsInWalkToRunTransition = false;
        s_walkRunBlendTime               = 0;
    }
#endif
}
```

with:

```c
        g_Player_IsInWalkToRunTransition = false;
        s_walkRunBlendTime               = 0;
    }
    Pc_LocoBlend_PoseApply(player, extra, anmHdr, coords);
#endif
}
```

At the end of `Player_LogicUpdate`, replace:

```c
    if (g_DebugThirdPersonCam) {
        D_800C454C = 0;
    }
#endif
    player->rotation.vy      = Q12_ANGLE_NORM_U(player->rotation.vy + (D_800C454C >> 4) + Q12_ANGLE(360.0f));
```

with:

```c
    if (g_DebugThirdPersonCam) {
        D_800C454C = 0;
    }
    Pc_LocoBlend_MotionApply(player);
#endif
    player->rotation.vy      = Q12_ANGLE_NORM_U(player->rotation.vy + (D_800C454C >> 4) + Q12_ANGLE(360.0f));
```

- [ ] **Step 6: Build**

Run the Global Constraints build command. Expected: no errors. If `HarryAnim_*`, `HarryBone_*`, `Player_VariableAnimDurationGet` or `SysFlag_DemoActive` are undeclared in `pc_loco_blend.c`, add the header that declares them (`grep -rn "HarryBone_Torso *=" include`, `grep -rn "SysFlag_DemoActive *=" include`) to its includes.

- [ ] **Step 7: Unit tests still pass**

Run the Task 2 Step 5 command. Expected: `all passed`.

- [ ] **Step 8: In-game check (user)**

Ask the user to: set `anim_blend` ON, classic camera, console `locoblend` (trace on), then walk, run, walk backward, sidestep each way, stop, start, quick-turn, enter a door while walking, then toggle `Anim_Blending` OFF while walking. Expected:
- Start/stop and walk↔run transitions look softer than with OFF; no limb shrinking or popping.
- Trace lines `[LOCOBLEND] ... own=1.00 ...` while moving, `own` falling to 0 within a few lines after stopping or toggling OFF.
- Door/cutscene: native anims, `own` at 0 (no `[LOCOBLEND]` weights frozen mid-walk).
- With OFF from the start: no `[LOCOBLEND]` lines at all, game identical to before.

Read the newest log: `grep -a "LOCOBLEND" "$(ls -t /d/GitHub/silent-hill-decomp/pc_port/build/SilentHill_*.log | head -1)" | tail -40`.

- [ ] **Step 9: Commit**

```bash
git add pc_port/src/pc_loco_blend.h pc_port/src/pc_loco_blend.c src/bodyprog/player_control.c
git commit -m "Anim blend: pose layer with ownership fade and gait phase

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Classic diagonal motion

**Files:**
- Modify: `pc_port/src/pc_loco_blend.c` (`Pc_LocoBlend_IntentClassic`, `Pc_LocoBlend_MotionApply`)

**Interfaces:**
- Consumes: `PcLocoMath_TargetWeights`, `s_lb`.
- Produces: `static void Pc_LocoBlend_ApplyDiagonal(float angleDeg, float ratio);` overrides `D_800C4550`, `g_Player_HeadingAngle` and `g_SysWork.playerWork.player.properties.player.headingAngle`. Reused by Task 7.

- [ ] **Step 1: Diagonal helper**

Insert in `pc_loco_blend.c` above `Pc_LocoBlend_IntentClassic`:

```c
/* Lateral top speed / axis top speed, so the diagonal keeps the native
 * acceleration of the axis move and lands between the two native speeds. */
#define LOCO_RATIO_CLASSIC_WALK_FWD  (1.25f / 1.4f)
#define LOCO_RATIO_CLASSIC_WALK_BACK (1.25f / 1.15f)
#define LOCO_RATIO_CLASSIC_RUN_FWD   (3.1739f / 4.0f)
/* The shim's walk sidestep averages 0.024 m per keyframe at 30 keyframes/s. */
#define LOCO_RATIO_SHIM_WALK         (0.72f / 1.5f)
#define LOCO_RATIO_SHIM_RUN          1.0f

static void Pc_LocoBlend_ApplyDiagonal(float angleDeg, float ratio)
{
    float w[PcLocoClip_Count];
    float axisW;
    float latW;
    float speed;
    s32   heading;

    PcLocoMath_TargetWeights(angleDeg, 0.0f, w);
    axisW = w[PcLocoClip_WalkForward] + w[PcLocoClip_WalkBackward];
    latW  = w[PcLocoClip_SidestepLeft] + w[PcLocoClip_SidestepRight];
    speed = fabsf((float)D_800C4550) * (axisW + latW * ratio);

    heading               = (s32)lroundf(angleDeg * 4096.0f / 360.0f);
    D_800C4550            = (q19_12)lroundf(speed);
    g_Player_HeadingAngle = heading;
    g_SysWork.playerWork.player.properties.player.headingAngle = heading;
}
```

- [ ] **Step 2: Apply in classic**

In `Pc_LocoBlend_IntentClassic`, after the `switch` block, add:

```c
    if (s_lb.moving && lat != 0.0f && !g_Player_IsInWalkToRunTransition &&
        g_SysWork.playerWork.extra.state == PlayerState_None)
    {
        if (state == PlayerLowerBodyState_WalkForward)
        {
            Pc_LocoBlend_ApplyDiagonal(s_lb.angleDeg, LOCO_RATIO_CLASSIC_WALK_FWD);
        }
        else if (state == PlayerLowerBodyState_RunForward)
        {
            Pc_LocoBlend_ApplyDiagonal(s_lb.angleDeg, LOCO_RATIO_CLASSIC_RUN_FWD);
        }
        else if (state == PlayerLowerBodyState_WalkBackward)
        {
            Pc_LocoBlend_ApplyDiagonal(s_lb.angleDeg, LOCO_RATIO_CLASSIC_WALK_BACK);
        }
    }
```

(Uses the raw `state`, not `base`, so aim variants keep native motion, per spec.)

In `Pc_LocoBlend_MotionApply`, the alternate-camera branch must not get classic diagonals before Task 7. Change its body to:

```c
    else
    {
        float lat = (float)((g_Player_IsSteppingRightHold ? 1 : 0) - (g_Player_IsSteppingLeftHold ? 1 : 0));
        s_lb.moving   = 1;
        s_lb.gait     = g_Player_IsRunning ? 1.0f : 0.0f;
        s_lb.angleDeg = g_Player_IsMovingBackward ? 180.0f - 45.0f * lat :
                        g_Player_IsMovingForward  ? 45.0f * lat : 90.0f * lat;
    }
```

- [ ] **Step 3: Build**

Run the Global Constraints build command. Expected: no errors.

- [ ] **Step 4: In-game check (user)**

Ask the user to: `anim_blend` ON, classic camera, hold forward then also hold a strafe (step) button; then back + strafe; then run + forward + strafe. Expected: Harry travels diagonally (≈45° off his facing), legs show a forward/side mix, footsteps still play, speed a bit below straight forward. Releasing strafe returns to straight forward without a stop. Walls hit diagonally still stop him (collision). Trace shows `ang=45` / `ang=-45` / `ang=135`.

- [ ] **Step 5: Commit**

```bash
git add pc_port/src/pc_loco_blend.c
git commit -m "Anim blend: classic forward/back + strafe diagonals

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Classic direction shortcuts

**Files:**
- Modify: `src/bodyprog/player_control.c` — new static functions right before `void Player_LowerBodyUpdate(` (~7376), call inside its existing `#ifdef SH_PC_PORT` block after `g_PcNativeDistAccrued = 1;` (~7419)

**Interfaces:**
- Consumes: `Pc_LocoBlend_Enabled`, `Pc_LocoBlend_StartClip`, native `func_8007D6F0`, `D_800C45C8`.
- Produces: `static void Pc_LocoBlend_NativeShortcuts(s_SubCharacter* player, e_PlayerLowerBodyState ahead);`

- [ ] **Step 1: Shortcut functions**

Insert directly above `void Player_LowerBodyUpdate(s_SubCharacter* player, s_PlayerExtra* extra) // 0x80077D00`:

```c
#ifdef SH_PC_PORT
/* anim_blend: a held direction takes over at once. Natively each direction
 * has to decelerate to a stop (or finish its sidestep) before the None case
 * may start another, which is the "stop, then strafe" feel. The native case
 * for the new state runs this same frame and owns speed and exits. */
static void Pc_LocoBlend_SwitchState(s_SubCharacter* player, e_PlayerLowerBodyState state, s32 harryAnim)
{
    g_SysWork.playerWork.extra.lowerBodyState = state;
    /* 1, not 0: the case would otherwise restart the clip from its first keyframe. */
    player->model.stateStep                   = 1;
    player->model.controlState                = 0;
    player->properties.player.runStepSfxCount = Q12(0.0f);
    player->properties.player.afkTimer        = Q12(0.0f);
    g_SysWork.playerStopFlags                 = PlayerStopFlag_None;
    Pc_LocoBlend_StartClip(&player->model, harryAnim);
}

static bool Pc_LocoBlend_SideClear(s_SubCharacter* player, bool right)
{
    s32 savedHeading = player->headingAngle;
    s32 probe;

    player->headingAngle += right ? Q12_ANGLE(90.0f) : -Q12_ANGLE(90.0f);
    probe                 = func_8007D6F0(player, &D_800C45C8);
    player->headingAngle  = savedHeading;
    return probe == PlayerLowerBodyState_None;
}

static void Pc_LocoBlend_ToSide(s_SubCharacter* player, bool right)
{
    if (g_Player_IsRunning && Pc_LocoBlend_SideClear(player, right))
    {
        Pc_LocoBlend_SwitchState(player, right ? PlayerLowerBodyState_RunRight : PlayerLowerBodyState_RunLeft,
                                 right ? HarryAnim_RunRight : HarryAnim_RunLeft);
    }
    else
    {
        Pc_LocoBlend_SwitchState(player, right ? PlayerLowerBodyState_SidestepRight : PlayerLowerBodyState_SidestepLeft,
                                 right ? HarryAnim_SidestepRight : HarryAnim_SidestepLeft);
    }
}

static void Pc_LocoBlend_ToForward(s_SubCharacter* player, e_PlayerLowerBodyState ahead)
{
    if (g_Player_IsRunning && ahead == PlayerLowerBodyState_None)
    {
        Pc_LocoBlend_SwitchState(player, PlayerLowerBodyState_RunForward, HarryAnim_RunForward);
    }
    else
    {
        Pc_LocoBlend_SwitchState(player, PlayerLowerBodyState_WalkForward, HarryAnim_WalkForward);
    }
}

static void Pc_LocoBlend_NativeShortcuts(s_SubCharacter* player, e_PlayerLowerBodyState ahead)
{
    e_PlayerLowerBodyState cur = g_SysWork.playerWork.extra.lowerBodyState;
    bool                   f   = g_Player_IsMovingForward != 0;
    bool                   b   = g_Player_IsMovingBackward != 0;
    bool                   l   = g_Player_IsSteppingLeftHold != 0;
    bool                   r   = g_Player_IsSteppingRightHold != 0;
    bool                   run = g_Player_IsRunning != 0;

    if (!Pc_LocoBlend_Enabled() || cur >= PlayerLowerBodyState_Aim ||
        g_SysWork.playerWork.extra.state != PlayerState_None ||
        g_Player_IsInWalkToRunTransition ||
        g_SysWork.playerWork.extra.upperBodyState == PlayerUpperBodyState_AimStartTargetLock)
    {
        return;
    }

    switch (cur)
    {
        case PlayerLowerBodyState_WalkForward:
        case PlayerLowerBodyState_RunForward:
            if (!f && !b && (l || r))
            {
                Pc_LocoBlend_ToSide(player, r);
            }
            else if (cur == PlayerLowerBodyState_WalkForward && b && !f && !run)
            {
                Pc_LocoBlend_SwitchState(player, PlayerLowerBodyState_WalkBackward, HarryAnim_WalkBackward);
            }
            break;

        case PlayerLowerBodyState_WalkBackward:
            if (!f && !b && (l || r))
            {
                Pc_LocoBlend_ToSide(player, r);
            }
            else if (f && !b)
            {
                Pc_LocoBlend_ToForward(player, ahead);
            }
            break;

        case PlayerLowerBodyState_SidestepLeft:
        case PlayerLowerBodyState_SidestepRight:
        case PlayerLowerBodyState_RunLeft:
        case PlayerLowerBodyState_RunRight:
            if (f)
            {
                Pc_LocoBlend_ToForward(player, ahead);
            }
            /* B + run is left to the native path so the jump-back still triggers. */
            else if (b && !run)
            {
                Pc_LocoBlend_SwitchState(player, PlayerLowerBodyState_WalkBackward, HarryAnim_WalkBackward);
            }
            else if (r && (cur == PlayerLowerBodyState_SidestepLeft || cur == PlayerLowerBodyState_RunLeft))
            {
                Pc_LocoBlend_ToSide(player, true);
            }
            else if (l && (cur == PlayerLowerBodyState_SidestepRight || cur == PlayerLowerBodyState_RunRight))
            {
                Pc_LocoBlend_ToSide(player, false);
            }
            break;

        default:
            break;
    }
}
#endif
```

- [ ] **Step 2: Call it**

In `Player_LowerBodyUpdate`, replace:

```c
    g_PcNativeDistAccrued = 1;
#endif
```

with:

```c
    g_PcNativeDistAccrued = 1;
    Pc_LocoBlend_NativeShortcuts(player, temp_s3);
#endif
```

- [ ] **Step 3: Build**

Run the Global Constraints build command. Expected: no errors. If `func_8007D6F0` is reported as implicitly declared at the new call site, add above the new block: `s32 func_8007D6F0(s_SubCharacter* player, s_800C45C8* arg1);`

- [ ] **Step 4: In-game check (user)**

Ask the user to (`anim_blend` ON, classic):
1. Walk forward, release forward while holding strafe: sidestep starts immediately, no stop.
2. Sidestep, press forward: walks forward immediately.
3. Sidestep left, switch to right: flips without finishing the step.
4. Walk forward, press back (no run): walks backward immediately.
5. Run forward, release forward + hold strafe + run: run-strafe immediately.
6. Back + run from a sidestep still does the jump-back.
7. Mash left/right/forward quickly for ~10 s: no walk-in-place lock, input always responds.
8. Run into a wall straight and then diagonally: wall-stop anims still play; release run after a sprint: skid-stop still plays.
9. With `Anim_Blending` OFF: every one of the above behaves like the original game.

- [ ] **Step 5: Commit**

```bash
git add src/bodyprog/player_control.c
git commit -m "Anim blend: classic direction changes without stopping first

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Alternate cameras — analog intent, diagonals, phase-continuous clip starts

**Files:**
- Modify: `pc_port/src/pc_loco_blend.c` (`Pc_LocoBlend_MotionApply`)
- Modify: `src/bodyprog/player_control.c` — shim anim selection (~2390-2460)

**Interfaces:**
- Consumes: `Pc_LocoBlend_ApplyDiagonal` (Task 5), `Pc_LocoBlend_StartClip` (Task 4).
- Produces: `static void Pc_LocoBlend_IntentShim(void);`

- [ ] **Step 1: Shim intent**

Insert in `pc_loco_blend.c` above `Pc_LocoBlend_MotionApply`:

```c
#define LOCO_STICK_DEADZONE 24

static void Pc_LocoBlend_IntentShim(void)
{
    s32   ax   = (s32)g_Controller0->analogController.leftX - 128;
    s32   ay   = (s32)g_Controller0->analogController.leftY - 128;
    s32   fwd  = g_Player_IsMovingForward != 0;
    s32   back = g_Player_IsMovingBackward != 0;
    float x;
    float z;

    if (ax * ax + ay * ay >= LOCO_STICK_DEADZONE * LOCO_STICK_DEADZONE)
    {
        x = (float)ax;
        z = (float)-ay;
    }
    else
    {
        x = (float)((g_Player_IsSteppingRightHold ? 1 : 0) - (g_Player_IsSteppingLeftHold ? 1 : 0));
        z = (float)(fwd - back);
    }

    s_lb.moving = (x != 0.0f || z != 0.0f);
    s_lb.gait   = g_Player_IsRunning ? 1.0f : 0.0f;
    if (!s_lb.moving)
    {
        return;
    }
    s_lb.angleDeg = atan2f(x, z) * (180.0f / 3.14159265f);

    /* Only when the shim's forward/back branch ran: its pure-strafe branch
     * already moves Harry sideways itself. Exact axis angles keep shim motion. */
    if ((fwd || back) && fabsf(sinf(s_lb.angleDeg * (3.14159265f / 180.0f))) > 0.01f)
    {
        Pc_LocoBlend_ApplyDiagonal(s_lb.angleDeg, (g_Player_IsRunning && fwd) ? LOCO_RATIO_SHIM_RUN : LOCO_RATIO_SHIM_WALK);
    }
}
```

Replace the alternate-camera `else` branch body in `Pc_LocoBlend_MotionApply` (the Task 5 version) with:

```c
    else
    {
        Pc_LocoBlend_IntentShim();
    }
```

- [ ] **Step 2: Shim clip starts**

In `src/bodyprog/player_control.c` shim forward branch, replace:

```c
                        player->model.anim.status = ANIM_STATUS(targetWalk, false);
                        player->model.stateStep = 0;
                        if (!aimingNow && !inGunAttack) {
                            extra->model.anim.status = ANIM_STATUS(targetWalk, false);
```

with:

```c
                        Pc_LocoBlend_StartClip(&player->model, targetWalk);
                        player->model.stateStep = 0;
                        if (!aimingNow && !inGunAttack) {
                            Pc_LocoBlend_StartClip(&extra->model, targetWalk);
```

Backward branch, replace:

```c
                            player->model.anim.status = ANIM_STATUS(HarryAnim_WalkBackward, false);
                            player->model.stateStep = 0;
                            if (!aimingNow && !inGunAttack) {
                                extra->model.anim.status = ANIM_STATUS(HarryAnim_WalkBackward, false);
```

with:

```c
                            Pc_LocoBlend_StartClip(&player->model, HarryAnim_WalkBackward);
                            player->model.stateStep = 0;
                            if (!aimingNow && !inGunAttack) {
                                Pc_LocoBlend_StartClip(&extra->model, HarryAnim_WalkBackward);
```

Sidestep branch, replace:

```c
                        player->model.anim.status = wantInactive;
                        player->model.stateStep = 0;
                        if (!aimingNow && !inGunAttack) {
                            extra->model.anim.status = wantInactive;
```

with:

```c
                        Pc_LocoBlend_StartClip(&player->model, isLeft ? leftStrafeAnim : rightStrafeAnim);
                        player->model.stateStep = 0;
                        if (!aimingNow && !inGunAttack) {
                            Pc_LocoBlend_StartClip(&extra->model, isLeft ? leftStrafeAnim : rightStrafeAnim);
```

(These four statements are byte-identical to the old ones when blend is off: `StartClip` falls back to `ANIM_STATUS(anim, false)`, and `wantInactive` is `ANIM_STATUS(isLeft ? leftStrafeAnim : rightStrafeAnim, false)`.)

- [ ] **Step 3: Build**

Run the Global Constraints build command. Expected: no errors.

- [ ] **Step 4: In-game check (user)**

Ask the user to (`anim_blend` ON), in TPS, OTS and FPS:
1. W+D, W+A, S+D, S+A: diagonal travel with mixed legs; sprint + W+D too.
2. Controller: sweep the left stick slowly around a full circle walking and pushed fully: direction and legs follow the stick smoothly; no jitter at exact up/down/left/right.
3. Switch W → D → S → A quickly: no snapping to the start of a step, no pauses.
4. Aim while moving, shoot, melee lunge: unchanged behavior.
5. 2D control ON (classic camera): walk/run/stop blend smoothly, Harry still turns into the direction.
6. `Anim_Blending` OFF: alternate cameras behave exactly as before.

- [ ] **Step 5: Commit**

```bash
git add pc_port/src/pc_loco_blend.c src/bodyprog/player_control.c
git commit -m "Anim blend: analog 360 and diagonals for the alternate cameras

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Launcher checkbox

**Files:**
- Modify: `pc_port/launcher/SilentHillPC_Launcher/ControlsForm.cs` (field ~184, construction after `chkDisableDpad` ~800, load ~1194, reset ~1242, save ~1379)
- Modify: `pc_port/launcher/SilentHillPC_Launcher/Loc.Strings.cs` (after the `"2D Controls (screen-relative)"` entry, line 215)
- Modify: `pc_port/launcher/SilentHillPC_Launcher/Properties/AssemblyInfo.cs:43`

**Interfaces:**
- Consumes: config key `anim_blend` (Task 3).
- Produces: nothing used by later tasks.

- [ ] **Step 1: Field and control**

After `    private CheckBox chk2dControls;`:

```csharp
    private CheckBox chkAnimBlend;
```

After `tips.SetToolTip(chkDisableDpad, ...);`:

```csharp
        chkAnimBlend = new CheckBox
        {
            Text = "Animation blending (8-way)",
            Left = colPadX,
            Top = chkY + 264,
            Width = 230,
            ForeColor = TextColor,
        };
        Controls.Add(chkAnimBlend);
        tips.SetToolTip(chkAnimBlend, Loc.T("Harry's walking, running and strafing blend smoothly into each other, and forward or back combine with strafing for diagonal movement. Off = original animations."));
```

Load (after `chk2dControls.Checked = config.Get("control_2d", "0") == "1";`):

```csharp
        chkAnimBlend.Checked = config.Get("anim_blend", "0") == "1";
```

Reset (after `chk2dControls.Checked = false;`):

```csharp
        chkAnimBlend.Checked = false;
```

Save (after `config.Set("control_2d", chk2dControls.Checked ? "1" : "0");`):

```csharp
        config.Set("anim_blend", chkAnimBlend.Checked ? "1" : "0");
```

- [ ] **Step 2: Translations**

In `Loc.Strings.cs`, after the `"2D Controls (screen-relative)"` entry (order: ES, PT, FR, DE, IT, JA, ZH, RU, PL):

```csharp
            { "Animation blending (8-way)",           new[]{ "Mezcla de animaciones (8 dir.)", "Mistura de animações (8 dir.)", "Fusion d'animations (8 dir.)", "Animations-Blending (8 Richt.)", "Fusione animazioni (8 dir.)", "アニメーションブレンド(8方向)", "动画混合(8方向)", "Смешивание анимаций (8 напр.)", "Mieszanie animacji (8 kier.)" } },
            { "Harry's walking, running and strafing blend smoothly into each other, and forward or back combine with strafing for diagonal movement. Off = original animations.",  new[]{ "Caminar, correr y desplazarse de lado se mezclan suavemente, y avanzar o retroceder se combina con el desplazamiento lateral para moverse en diagonal. Desactivado = animaciones originales.", "Andar, correr e passo lateral se misturam suavemente, e avançar ou recuar se combina com o passo lateral para movimento diagonal. Desligado = animações originais.", "La marche, la course et les pas de côté s'enchaînent en douceur, et avancer ou reculer se combine avec le pas de côté pour se déplacer en diagonale. Désactivé = animations d'origine.", "Gehen, Laufen und Seitwärtsschritte gehen fließend ineinander über, und Vor- oder Rückwärts lässt sich mit Seitwärts zu Diagonalbewegung kombinieren. Aus = Originalanimationen.", "Camminata, corsa e passi laterali si fondono in modo fluido, e avanti o indietro si combinano con il passo laterale per muoversi in diagonale. Disattivato = animazioni originali.", "歩き・走り・横歩きが滑らかにつながり、前後移動と横歩きを組み合わせて斜めに移動できます。オフ = オリジナルのアニメーション。", "行走、奔跑和侧步平滑过渡，前进或后退可与侧步组合进行斜向移动。关闭 = 原版动画。", "Ходьба, бег и шаги в сторону плавно переходят друг в друга, а движение вперёд или назад сочетается с шагом в сторону для диагонального движения. Выкл. = оригинальные анимации.", "Chód, bieg i krok w bok płynnie przechodzą w siebie, a ruch do przodu lub do tyłu łączy się z krokiem w bok w ruch po przekątnej. Wył. = oryginalne animacje." } },
```

- [ ] **Step 3: Version bump**

`Properties/AssemblyInfo.cs:43`: `[assembly: AssemblyFileVersion("2026.10.10.3")]` → `[assembly: AssemblyFileVersion("2026.10.11.1")]`.

- [ ] **Step 4: Build and deploy**

Run:
```
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" "D:/GitHub/silent-hill-decomp/pc_port/launcher/SilentHillPC_Launcher/SilentHillPC_Launcher.csproj" -p:Configuration=Release -v:minimal && cp "/d/GitHub/silent-hill-decomp/pc_port/launcher/SilentHillPC_Launcher/bin/Release/SilentHillPC_Launcher.exe" /d/GitHub/silent-hill-decomp/pc_port/build/
```
Expected: `0 Error(s)` and the exe copied.

- [ ] **Step 5: Check (user)**

Ask the user to open `pc_port/build/SilentHillPC_Launcher.exe` → Controls: "Animation blending (8-way)" sits under "Disable D-pad for movement" without overlapping anything; hover shows the tooltip; toggle + Save writes `anim_blend` to `config.cfg`; switching launcher language translates both. If the checkbox overlaps another control, move it to the first free `chkY + 26*n` slot in that column.

- [ ] **Step 6: Commit**

```bash
git add pc_port/launcher/SilentHillPC_Launcher/ControlsForm.cs pc_port/launcher/SilentHillPC_Launcher/Loc.Strings.cs pc_port/launcher/SilentHillPC_Launcher/Properties/AssemblyInfo.cs
git commit -m "Launcher: Animation blending (8-way) checkbox

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Alternate cameras — wall-stop, skid-stop, stumble

**Files:**
- Modify: `src/bodyprog/player_control.c` — prototype + state before `void Player_LogicUpdate(` (~1524); definition next to the Task 6 block; shim anim-selection gate (`if (!jumpBackActive) if (g_Player_IsMovingForward) {`, ~2390); footstep shim gate (`if (!jumpBackActive) {` followed by `/* Strafe footsteps (PC):`, ~2761)

- Modify: `pc_port/src/pc_loco_blend.h`, `pc_port/src/pc_loco_blend.c` (`Pc_LocoBlend_HoldMotion`)

**Interfaces:**
- Consumes: `Pc_LocoBlend_Enabled`, `func_8007D6F0`, `D_800C45C8`, `g_Player_PrevPosition`, `g_Player_LastMoveStep`.
- Produces: `static bool Pc_TpsLocoAction_Update(s_SubCharacter* player, s_PlayerExtra* extra);`, `static s32 s_PcTpsLocoAction;` (`HarryAnim` being played, or `NO_VALUE`), and `void Pc_LocoBlend_HoldMotion(void);` (skips the diagonal override for the current frame).

- [ ] **Step 0: Motion hold in the glue**

So a held W+D cannot steer Harry diagonally while a wall-stop plays. Add to `pc_loco_blend.h` before `#endif`:

```c
void Pc_LocoBlend_HoldMotion(void);
```

In `pc_loco_blend.c`, add a field `s32 holdMotion;` at the end of `s_PcLocoBlend` (and a trailing `, 0` to the `s_lb` initializer), then add above `Pc_LocoBlend_IntentShim`:

```c
void Pc_LocoBlend_HoldMotion(void)
{
    s_lb.holdMotion = 1;
}
```

and make the first statements of `Pc_LocoBlend_IntentShim`:

```c
    if (s_lb.holdMotion)
    {
        s_lb.holdMotion = 0;
        s_lb.moving     = 0;
        return;
    }
```

(placed before the `if (ax * ax + ...)` block; move the local declarations above it as needed).

- [ ] **Step 1: Prototype and state**

Directly above `void Player_LogicUpdate(s_SubCharacter* player, s_PlayerExtra* extra, GsCOORDINATE2* coords) // 0x80071CE8`:

```c
#ifdef SH_PC_PORT
static s32  s_PcTpsLocoAction = NO_VALUE;
static bool Pc_TpsLocoAction_Update(s_SubCharacter* player, s_PlayerExtra* extra);
#endif
```

- [ ] **Step 2: Definition**

Append inside the Task 6 `#ifdef SH_PC_PORT` block (before its `#endif`):

```c
/* anim_blend + alternate cameras: the native running wall-stop, skid-stop and
 * stumble live in Player_LowerBodyUpdate, which these cameras never run.
 * Same triggers as its RunForward case. */
static bool Pc_TpsLocoAction_Update(s_SubCharacter* player, s_PlayerExtra* extra)
{
    s32 anim = NO_VALUE;

    (void)extra;
    if (!Pc_LocoBlend_Enabled())
    {
        s_PcTpsLocoAction = NO_VALUE;
        return false;
    }

    if (s_PcTpsLocoAction == NO_VALUE)
    {
        s32 kf     = player->model.anim.keyframeIdx;
        s32 travel = SquareRoot0(SQUARE(player->position.vx - g_Player_PrevPosition.vx) +
                                 SQUARE(player->position.vz - g_Player_PrevPosition.vz));

        if (player->model.anim.status != ANIM_STATUS(HarryAnim_RunForward, true))
        {
            return false;
        }

        if (player->properties.player.runDistance >= (u32)Q12(10.0f))
        {
            s32 probe = func_8007D6F0(player, &D_800C45C8);

            if (probe == PlayerLowerBodyState_WalkForward)
            {
                anim = HarryAnim_RunForwardStumble;
            }
            else if (probe == PlayerLowerBodyState_RunForward && travel < (g_Player_LastMoveStep >> 1))
            {
                anim = (kf >= 36) ? HarryAnim_RunForwardWallStopRight : HarryAnim_RunForwardWallStopLeft;
            }
        }
        else if ((!g_Player_IsRunning || !g_Player_IsMovingForward) &&
                 player->properties.player.runStepSfxCount >= 5 && player->moveSpeed >= Q12(3.125f))
        {
            if (kf >= 33 && kf <= 34)
            {
                anim = HarryAnim_RunForwardWallStopLeft;
            }
            else if (kf >= 43 && kf <= 44)
            {
                anim = HarryAnim_RunForwardWallStopRight;
            }
        }

        if (anim == NO_VALUE)
        {
            return false;
        }
        s_PcTpsLocoAction         = anim;
        player->model.anim.status = ANIM_STATUS(anim, false);
        player->model.stateStep   = 0;
        SH_DBG("[LOCOBLEND] alt-cam action anim=%d kf=%d runDist=%d", (int)anim, (int)kf,
               (int)player->properties.player.runDistance);
    }

    {
        s32 endKf = HARRY_BASE_ANIM_INFOS[ANIM_STATUS(s_PcTpsLocoAction, true)].endKeyframeIdx;

        D_800C4550 = player->moveSpeed - (TIMESTEP_SCALE_30_FPS(g_DeltaTime, Q12(0.4f)) >> 1);
        if (D_800C4550 < Q12(0.0f))
        {
            D_800C4550 = Q12(0.0f);
        }
        g_Player_HeadingAngle = Q12_ANGLE(0.0f);

        if (player->model.anim.status == ANIM_STATUS(s_PcTpsLocoAction, true) &&
            player->model.anim.keyframeIdx >= endKf - 1)
        {
            s_PcTpsLocoAction                         = NO_VALUE;
            g_SysWork.playerWork.extra.lowerBodyState = PlayerLowerBodyState_None;
            return false;
        }
    }
    Pc_LocoBlend_HoldMotion();
    return true;
}
```

- [ ] **Step 3: Gate the shim**

Replace:

```c
                if (!jumpBackActive) if (g_Player_IsMovingForward) {
```

with:

```c
                bool pcLocoAction = Pc_TpsLocoAction_Update(player, extra);
                if (!jumpBackActive && !pcLocoAction) if (g_Player_IsMovingForward) {
```

Replace (footstep shim):

```c
                if (!jumpBackActive) {
                    /* Strafe footsteps (PC): the sidestep / strafe-run anim is driven
```

with:

```c
                if (!jumpBackActive && s_PcTpsLocoAction == NO_VALUE) {
                    /* Strafe footsteps (PC): the sidestep / strafe-run anim is driven
```

and replace the end of that same block:

```c
                    /* Aiming outside the alternate cameras keeps whatever the
                     * original path set — classic is not this shim's business. */
                }
```

with:

```c
                    /* Aiming outside the alternate cameras keeps whatever the
                     * original path set — classic is not this shim's business. */
                }
                if (s_PcTpsLocoAction != NO_VALUE) {
                    extra->lowerBodyState = (s_PcTpsLocoAction == HarryAnim_RunForwardStumble)
                                                ? PlayerLowerBodyState_Stumble
                                                : PlayerLowerBodyState_RunForwardWallStop;
                }
```

- [ ] **Step 4: Build**

Run the Global Constraints build command. Expected: no errors. If `bool pcLocoAction` is rejected as a declaration after a label/statement, wrap the two lines in their own scope by moving the declaration to the top of the enclosing `{` block that begins with `bool aimingNow = ...`.

- [ ] **Step 5: In-game check (user)**

Ask the user to (`anim_blend` ON, TPS and OTS):
1. Sprint >10 m straight into a wall: wall-stop anim plays, Harry stops, control returns after it.
2. Sprint, then release sprint/forward: skid-stop plays (needs ≥5 running steps).
3. Sprint into a stumble surface (where classic stumbles): stumble plays.
4. Short sprints and walking: none of these trigger.
5. Log shows `[LOCOBLEND] alt-cam action ...` once per event: `grep -a "alt-cam action" "$(ls -t /d/GitHub/silent-hill-decomp/pc_port/build/SilentHill_*.log | head -1)"`.
6. `Anim_Blending` OFF: none of these appear in TPS/OTS (as before).

- [ ] **Step 6: Commit**

```bash
git add src/bodyprog/player_control.c
git commit -m "Anim blend: wall-stop, skid-stop and stumble on the alternate cameras

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Foot-phase tuning and playtest pass

**Files:**
- Modify: `pc_port/src/pc_loco_blend.c` (`s_ClipPhaseOffset` values)
- Modify: `docs/superpowers/specs/2026-10-11-anim-blend-locomotion-design.md` (Phase section offsets, Risks)

**Interfaces:**
- Consumes: the `LOCOBLEND OFFSET` console command (Task 3).
- Produces: final offsets.

- [ ] **Step 1: Tune with the user**

For each clip pair, ask the user to hold the diagonal or switch directions and watch the feet (classic camera from a side angle is easiest):
- Forward + right (clips 0 + 3), forward + left (0 + 2), back + right (1 + 3), run forward + run right (4 + 6), run forward + run left (4 + 5).
- If the legs scissor (left foot of one clip blended with the right foot of the other), the clip's offset is off by half a cycle: `locoblend offset <clip> <current + 0.5>`.
- Fine-tune by ±0.05 until feet plant together.
- If the walking sidestep (clips 2/3) never looks right phase-locked, record that; the spec fallback (own phase for sidesteps) becomes a follow-up task.

- [ ] **Step 2: Hardcode the tuned values**

Replace the values in `s_ClipPhaseOffset` with the ones the user settled on, keeping the per-line keyframe comments.

- [ ] **Step 3: Update the spec**

In the spec's Phase section, replace the offset list with the tuned values and note the date; under Risks, mark the footstep-pairing risk resolved (or record what remains).

- [ ] **Step 4: Full playtest matrix (user)**

Hand the user the spec's playtest matrix (tank, TPS, OTS, FPS, 2D control × walk/run, 8 directions, analog sweep, start/stop, quick-turn, jump-back, aim-walk, melee lunge, wall-stop forward/side, skid-stop, stumble, stairs/slopes, speed zones, exhaustion, cutscene entry/exit, doors, item pickups), with `locoblend` trace on. Fix any issue found as its own commit before closing this task.

- [ ] **Step 5: Unit tests and build one last time**

Run the Task 2 Step 5 test command and the Global Constraints build command. Expected: `all passed`, no build errors.

- [ ] **Step 6: Commit**

```bash
git add pc_port/src/pc_loco_blend.c docs/superpowers/specs/2026-10-11-anim-blend-locomotion-design.md
git commit -m "Anim blend: tuned foot-phase offsets

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
