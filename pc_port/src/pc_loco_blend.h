/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_LOCO_BLEND_H
#define PC_LOCO_BLEND_H

#include "game.h"
#include "bodyprog/bodyprog.h"

int         Pc_LocoBlend_Enabled(void);
const char* Pc_LocoBlend_ConsoleCmd(const char* arg);

void Pc_LocoBlend_StartClip(s_Model* model, s32 harryAnim);
void Pc_LocoBlend_MotionApply(s_SubCharacter* player);
void Pc_LocoBlend_PoseApply(s_SubCharacter* player, s_PlayerExtra* extra, s_AnmHeader* anmHdr, GsCOORDINATE2* coords);
void Pc_LocoBlend_HoldMotion(void);

#endif
