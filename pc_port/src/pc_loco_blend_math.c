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

/* A mirrored sample reads each bone's left/right partner. Falls back to the
 * bone's own channel (unreflected) where the partner has none. */
static const s_AnmBindPose* PcLocoMath_SourceBind(const s_AnmHeader* hdr, const s_PcLocoSample* sample, s32 boneIdx)
{
    if (sample->mirrorMap == NULL)
    {
        return &hdr->bindPoses[boneIdx];
    }
    return &hdr->bindPoses[sample->mirrorMap[boneIdx]];
}

void PcLocoMath_SamplePose(const s_AnmHeader* hdr, GsCOORDINATE2* coords, u32 boneMask,
                           const s_PcLocoSample* samples, s32 count, float nativeWeight)
{
    const u8* data  = (const u8*)hdr + hdr->dataOffset;
    s32       exact = (count == 1 && nativeWeight <= 0.0f && samples[0].weight == 1.0f && samples[0].mirrorMap == NULL);
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
                const s_AnmBindPose* src     = PcLocoMath_SourceBind(hdr, &samples[k], boneIdx);
                s32                  reflect = (samples[k].mirrorMap != NULL && src->translationDataIdx >= 0);
                const s_AnmBindPose* use     = reflect ? src : bind;
                const s8*            f0      = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe0) + use->translationDataIdx * 3;
                const s8*            f1      = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe1) + use->translationDataIdx * 3;

                for (i = 0; i < 3; i++)
                {
                    s32 v = PcLocoMath_DecodeTranslation(f0[i], f1[i], samples[k].alpha, hdr->scaleLog2);
                    if (i == 1 && use->translationDataIdx == 0)
                    {
                        v -= hdr->rootYOffset;
                    }
                    if (i == 0 && reflect)
                    {
                        v = -v;
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
                const s_AnmBindPose* src     = PcLocoMath_SourceBind(hdr, &samples[k], boneIdx);
                s32                  reflect = (samples[k].mirrorMap != NULL && src->rotationDataIdx >= 0);
                const s_AnmBindPose* use     = reflect ? src : bind;
                const s8*            r0      = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe0) +
                                               hdr->translationBoneCount * 3 + use->rotationDataIdx * 9;
                const s8*            r1      = (const s8*)(data + hdr->keyframeDataSize * samples[k].keyframe1) +
                                               hdr->translationBoneCount * 3 + use->rotationDataIdx * 9;

                for (i = 0; i < 3; i++)
                {
                    float row[3];
                    for (j = 0; j < 3; j++)
                    {
                        row[j] = (float)PcLocoMath_DecodeRotation(r0[i * 3 + j], r1[i * 3 + j], samples[k].alpha);
                        /* Reflection S*M*S with S = diag(-1,1,1) flips the X row and column, not their crossing. */
                        if (reflect && ((i == 0) != (j == 0)))
                        {
                            row[j] = -row[j];
                        }
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
