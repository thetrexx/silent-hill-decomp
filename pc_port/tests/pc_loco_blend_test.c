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
