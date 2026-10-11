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

static void set_rot(MATRIX* m, const s8 r[9])
{
    s32 i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            m->m[i][j] = (short)(r[i * 3 + j] << 5);
}

static void test_lerp_pose(void)
{
    GsCOORDINATE2 cur[T_BONES];
    GsCOORDINATE2 keep[T_BONES];
    MATRIX        from[T_BONES];
    float         l0, l2, d;
    float         r0[3], r2[3];
    s32           i;

    memset(cur, 0, sizeof(cur));
    memset(from, 0, sizeof(from));
    for (i = 1; i < T_BONES; i++)
    {
        set_rot(&from[i], ROT_ID);
        set_rot(&cur[i].coord, ROT_Y90);
        from[i].t[0]        = 100;
        cur[i].coord.t[0]   = 300;
    }

    /* t = 0 is the snapshot exactly, t = 1 leaves the current pose untouched. */
    memcpy(keep, cur, sizeof(cur));
    PcLocoMath_LerpPose(keep, from, T_BONES, 0xFFFFFFFFu, 0.0f);
    assert(memcmp(&keep[1].coord, &from[1], sizeof(MATRIX)) == 0);
    memcpy(keep, cur, sizeof(cur));
    PcLocoMath_LerpPose(keep, from, T_BONES, 0xFFFFFFFFu, 1.0f);
    assert(memcmp(keep, cur, sizeof(cur)) == 0);

    /* Halfway: translation averages, rotation rows stay rigid at source length. */
    memcpy(keep, cur, sizeof(cur));
    PcLocoMath_LerpPose(keep, from, T_BONES, 1u << 1, 0.5f);
    assert(keep[1].coord.t[0] == 200);
    for (i = 0; i < 3; i++)
    {
        r0[i] = (float)keep[1].coord.m[0][i];
        r2[i] = (float)keep[1].coord.m[2][i];
    }
    l0 = sqrtf(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]);
    l2 = sqrtf(r2[0] * r2[0] + r2[1] * r2[1] + r2[2] * r2[2]);
    d  = r0[0] * r2[0] + r0[1] * r2[1] + r0[2] * r2[2];
    assert(near(l0, 4064.0f, 3.0f));
    assert(near(l2, 4064.0f, 3.0f));
    assert(fabsf(d) < 4064.0f * 4064.0f * 0.002f);

    /* Bone 2 was outside the mask. */
    assert(memcmp(&keep[2], &cur[2], sizeof(GsCOORDINATE2)) == 0);
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
    test_sampler_single_matches_native();
    test_sampler_two_way_average();
    test_sampler_native_mix_and_mask();
    test_lerp_pose();
    printf("pc_loco_blend_test: all passed\n");
    return 0;
}
