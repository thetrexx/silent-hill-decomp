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
