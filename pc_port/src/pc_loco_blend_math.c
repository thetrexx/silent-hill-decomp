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
