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
    s32   holdMotion;
    s32   phaseHeld;
} s_PcLocoBlend;

static s_PcLocoBlend s_lb = { { 0 }, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1, 0, 0, 0, 0 };

/* Shifts each clip so its first footstep plant (func_8007B924 keyframes)
 * lines up with WalkForward's. Tunable live with LOCOBLEND OFFSET. */
static float s_ClipPhaseOffset[PcLocoClip_Count] = {
    0.0f,    /* WalkForward   plants 6/18   in 1-23   */
    0.5326f, /* WalkBackward  plants 52/63  in 46-69 (+0.5: back diagonals led with the wrong foot) */
    0.8026f, /* SidestepLeft  plants 83/93  in 70-94 (+0.5: forward-left led with the wrong foot) */
    0.3026f, /* SidestepRight plants 108/118 in 95-119 */
    0.0326f, /* RunForward    plants 31/41  in 26-45  */
    0.0903f, /* RunLeft       plants 125/131 in 121-133 */
    0.0134f, /* RunRight      plants 139/145 in 136-148 */
    /* A mirrored clip swaps which foot plants, so it starts half a cycle from its source. */
    0.8026f, /* SidestepLeftMirror  = SidestepRight + 0.5 */
    0.3026f, /* SidestepRightMirror = SidestepLeft + 0.5 */
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
            snprintf(msg, sizeof(msg), "usage: locoblend offset <clip 0-8> <0..1>");
        }
        return msg;
    }

    if (strcmp(word, "MIRROR") == 0)
    {
        s32 on = 0;

        if (sscanf(arg + i, "%d", &on) == 1)
        {
            g_PcConfig.animBlendMirror = (on != 0);
        }
        else
        {
            g_PcConfig.animBlendMirror = !g_PcConfig.animBlendMirror;
        }
        snprintf(msg, sizeof(msg), "Anim blend mirrored sidesteps on diagonals: %s", g_PcConfig.animBlendMirror ? "on" : "off");
        return msg;
    }

    s_lb.trace = !s_lb.trace;
    snprintf(msg, sizeof(msg), "Anim blend trace %s (anim_blend=%d)", s_lb.trace ? "on" : "off", g_PcConfig.animBlend);
    return msg;
}

extern int g_DebugThirdPersonCam;
extern int g_PcFpsCam;

#define LOCO_OWN_FADE_SEC 0.12f

static const s32 CLIP_ANIM[PcLocoClip_Count] = {
    HarryAnim_WalkForward, HarryAnim_WalkBackward, HarryAnim_SidestepLeft, HarryAnim_SidestepRight,
    HarryAnim_RunForward,  HarryAnim_RunLeft,      HarryAnim_RunRight,
    HarryAnim_SidestepRight, HarryAnim_SidestepLeft
};

static const s8 HARRY_MIRROR_BONES[HarryBone_Count] = {
    HarryBone_Root,          HarryBone_Torso,         HarryBone_Head,
    HarryBone_RightShoulder, HarryBone_RightUpperArm, HarryBone_RightForearm, HarryBone_RightHand,
    HarryBone_LeftShoulder,  HarryBone_LeftUpperArm,  HarryBone_LeftForearm,  HarryBone_LeftHand,
    HarryBone_Hips,
    HarryBone_RightThigh,    HarryBone_RightShin,     HarryBone_RightFoot,
    HarryBone_LeftThigh,     HarryBone_LeftShin,      HarryBone_LeftFoot
};

static s32 Pc_LocoBlend_ClipFromAnim(s32 harryAnim)
{
    s32 i;

    /* Mirror slots reuse a real clip's anim; a native anim is always the real clip. */
    for (i = 0; i < PcLocoClip_SidestepLeftMirror; i++)
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

static s32 Pc_LocoBlend_Is2dControl(void)
{
    return g_PcConfig.control2d && !g_PcFpsCam && !g_SysWork.playerCombat.isAiming;
}

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
}

#define LOCO_STICK_DEADZONE 24

void Pc_LocoBlend_HoldMotion(void)
{
    s_lb.holdMotion = 1;
}

static void Pc_LocoBlend_IntentShim(void)
{
    s32   ax   = (s32)g_Controller0->analogController.leftX - 128;
    s32   ay   = (s32)g_Controller0->analogController.leftY - 128;
    s32   fwd  = g_Player_IsMovingForward != 0;
    s32   back = g_Player_IsMovingBackward != 0;
    float x;
    float z;

    if (s_lb.holdMotion)
    {
        s_lb.holdMotion = 0;
        s_lb.moving     = 0;
        return;
    }

    if (ax * ax + ay * ay >= LOCO_STICK_DEADZONE * LOCO_STICK_DEADZONE)
    {
        x = (float)ax;
        z = (float)-ay;
    }
    else
    {
        x = (float)(((g_Player_IsSteppingRightHold || g_Player_IsSteppingRightTap) ? 1 : 0) -
                    ((g_Player_IsSteppingLeftHold || g_Player_IsSteppingLeftTap) ? 1 : 0));
        z = (float)(fwd - back);
    }

    s_lb.moving = (x != 0.0f || z != 0.0f);
    /* No run-back clip: the shim moves at walk-back speed. */
    s_lb.gait   = (g_Player_IsRunning && !back) ? 1.0f : 0.0f;
    if (!s_lb.moving)
    {
        return;
    }
    s_lb.angleDeg = atan2f(x, z) * (180.0f / 3.14159265f);

    /* Only when the shim's forward/back branch ran: its pure-strafe branch
     * already moves Harry sideways itself. Exact axis angles keep shim motion.
     * Other player states (knockback, jump-back) own D_800C4550 and the heading. */
    if ((g_SysWork.playerWork.extra.state == PlayerState_None || g_SysWork.playerWork.extra.state == PlayerState_Combat) &&
        g_SysWork.playerWork.extra.lowerBodyState != PlayerLowerBodyState_JumpBackward &&
        (fwd || back) && fabsf(sinf(s_lb.angleDeg * (3.14159265f / 180.0f))) > 0.01f)
    {
        Pc_LocoBlend_ApplyDiagonal(s_lb.angleDeg, (g_Player_IsRunning && fwd) ? LOCO_RATIO_SHIM_RUN : LOCO_RATIO_SHIM_WALK);
    }
}

void Pc_LocoBlend_MotionApply(s_SubCharacter* player)
{
    (void)player;

    if (!Pc_LocoBlend_Enabled())
    {
        s_lb.moving     = 0;
        s_lb.holdMotion = 0;
        return;
    }

    if (Pc_LocoBlend_Is2dControl())
    {
        s_lb.moving   = 1;
        s_lb.angleDeg = 0.0f;
        s_lb.gait     = g_Player_IsRunning ? 1.0f : 0.0f;
    }
    else if (!g_DebugThirdPersonCam && g_PcConfig.movementOriginal)
    {
        Pc_LocoBlend_IntentClassic();
    }
    else
    {
        Pc_LocoBlend_IntentShim();
    }
    s_lb.holdMotion = 0;
}

/* Experiment (anim_blend_mirror): forward+left and back+right walk with the
 * opposite sidestep played mirrored. Pure strafes keep the real clip so the
 * pose matches the native clip driving footsteps. */
static void Pc_LocoBlend_MirrorDiagonals(float target[PcLocoClip_Count])
{
    if (target[PcLocoClip_WalkForward] > 0.0f && target[PcLocoClip_SidestepLeft] > 0.0f)
    {
        target[PcLocoClip_SidestepLeftMirror] += target[PcLocoClip_SidestepLeft];
        target[PcLocoClip_SidestepLeft]        = 0.0f;
    }
    if (target[PcLocoClip_WalkBackward] > 0.0f && target[PcLocoClip_SidestepRight] > 0.0f)
    {
        target[PcLocoClip_SidestepRightMirror] += target[PcLocoClip_SidestepRight];
        target[PcLocoClip_SidestepRight]        = 0.0f;
    }
}

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
        if (g_PcConfig.animBlendMirror)
        {
            Pc_LocoBlend_MirrorDiagonals(target);
        }
    }
    else
    {
        memcpy(target, s_lb.weights, sizeof(target));
    }

    if (s_lb.fresh)
    {
        memcpy(s_lb.weights, target, sizeof(target));
        s_lb.fresh = 0;
        /* From idle the master's native blend-in starts at its first keyframe;
         * a stale phase would free-run through the blend-in and pop when it ends. */
        s_lb.phaseHeld = masterClip >= 0;
        if (s_lb.phaseHeld)
        {
            s_lb.phase = PcLocoMath_Fract(-s_ClipPhaseOffset[masterClip]);
        }
    }
    else
    {
        blendSec = (float)g_PcConfig.animBlendRateMs / 1000.0f;
        PcLocoMath_EaseWeights(s_lb.weights, target, dt / blendSec);
    }

    if (masterClip >= 0 && ANIM_STATUS_IS_ACTIVE(player->model.anim.status))
    {
        s_AnimInfo* info = Pc_LocoBlend_Info(masterClip);
        s_lb.phaseHeld = 0;
        s_lb.phase = PcLocoMath_Fract(PcLocoMath_ClipPhase(info->startKeyframeIdx, info->endKeyframeIdx,
                                                           player->model.anim.time) -
                                      s_ClipPhaseOffset[masterClip]);
    }
    else if (masterClip < 0 || !s_lb.phaseHeld)
    {
        float rate = 0.0f;
        s_lb.phaseHeld = 0;
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
        samples[count].weight    = s_lb.weights[i];
        samples[count].mirrorMap = (i >= PcLocoClip_SidestepLeftMirror) ? HARRY_MIRROR_BONES : NULL;
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
               "w=%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f/%.2f",
               s_lb.angleDeg, s_lb.gait, (int)s_lb.moving, s_lb.ownership, s_lb.phase, (int)masterClip,
               (int)upperClip, (int)g_SysWork.playerWork.extra.lowerBodyState,
               s_lb.weights[0], s_lb.weights[1], s_lb.weights[2], s_lb.weights[3],
               s_lb.weights[4], s_lb.weights[5], s_lb.weights[6], s_lb.weights[7], s_lb.weights[8]);
    }
}
