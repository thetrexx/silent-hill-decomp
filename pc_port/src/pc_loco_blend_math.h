/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_LOCO_BLEND_MATH_H
#define PC_LOCO_BLEND_MATH_H

#include "common.h"
#include <libgte.h>
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
    PcLocoClip_SidestepLeftMirror  = 7, /* SidestepRight played mirrored */
    PcLocoClip_SidestepRightMirror = 8, /* SidestepLeft played mirrored */
    PcLocoClip_Count         = 9
} e_PcLocoClip;

typedef struct _PcLocoSample
{
    s32   keyframe0;
    s32   keyframe1;
    s32   alpha; /* Q12 */
    float weight;
    const s8* mirrorMap; /* NULL, or the left/right partner of each bone: sample the partner reflected across X */
} s_PcLocoSample;

void  PcLocoMath_TargetWeights(float angleDeg, float gait, float out[PcLocoClip_Count]);
float PcLocoMath_Approach(float cur, float target, float maxStep);
void  PcLocoMath_EaseWeights(float cur[PcLocoClip_Count], const float target[PcLocoClip_Count], float maxStep);
float PcLocoMath_Fract(float x);
float PcLocoMath_ClipPhase(s32 startKf, s32 endKf, s32 timeQ12);
void  PcLocoMath_ClipSample(s32 startKf, s32 endKf, float phase, s_PcLocoSample* out);
void PcLocoMath_SamplePose(const s_AnmHeader* hdr, GsCOORDINATE2* coords, u32 boneMask,
                           const s_PcLocoSample* samples, s32 count, float nativeWeight);

#endif
