# Blended 8-Way Locomotion for Harry: Design

Date: 2026-10-11 (revised the same day: classic keeps the native machine, see "Revision")
Status: approved
Sub-project: 1 of 2 (sub-project 2, generic action crossfade, gets its own spec later)

## Goal

Optional PC-only mode where Harry's locomotion blends smoothly between animations, the way modern 8-way movement does. Holding forward and strafe-right moves him forward-right with blended legs. Changing direction, gait or speed eases from one pose to the next instead of snapping or stopping first.

Works in both control families:

- Classic tank controls with the fixed cameras. Left/right still turn; the step buttons now combine with forward/back.
- The TPS, OTS and FPS cameras (strafe-style, body tracks the camera), including analog 360-degree stick input.

Target outcome: something playable to show SlickAmogus. His alternate cameras currently look wrong because the original one-direction-at-a-time animations do not fit free movement.

## Constraints

- Default off. With the toggle off, every new code path is skipped and behavior is identical to today.
- Attract demos always behave as if the toggle were off.
- The PSX lower-body state machine (`Player_LowerBodyUpdate`) keeps all of its cases unmodified. Blending is added around it through hooks.
- Speeds stay authentic per direction (see Motion).
- The blend core lives in its own files (`pc_loco_blend_math.c`, `pc_loco_blend.c`) so it can later move behind a hook for a mod build, if SlickAmogus declines it as a core option.

## Non-goals (this sub-project)

- Generic crossfade into and out of every action animation (attacks, damage, pickups, scripted states). That is sub-project 2, built on this core.
- New animation data. No new clips, no run-backward, no authored diagonals.
- NPC or monster blending.

## Background

- All of Harry's clips live in one keyframe buffer. `Anim_BoneUpdate` (`src/bodyprog/gfx/bodyprog_anim_800445A4.c`) decodes per-bone 3-byte translations and 9-byte rotation matrices and lerps between two keyframes.
- Lower and upper body are masked separately (`HARRY_LOWER_BODY_BONE_MASK`, `HARRY_UPPER_BODY_BONE_MASK`). `player->model` drives the legs; `extra->model` drives the upper body. `Player_AnimUpdate` plays both every frame.
- Locomotion clips (`HARRY_BASE_ANIM_INFOS`, keyframe ranges): WalkForward 1-23, RunForward 26-45, WalkBackward 46-69, SidestepLeft 70-94, SidestepRight 95-119, RunLeft 121-133, RunRight 136-148. Loop playback wraps end to start. Walk/run/back use `Player_VariableAnimDurationGet`; sidesteps run at a constant 30, run-strafes at 20 keyframes per second.
- Footstep keyframes (first/second plant) from `func_8007B924`: WalkForward 6/18, RunForward 31/41, WalkBackward 52/63, SidestepLeft 83/93, SidestepRight 108/118, RunLeft 125/131, RunRight 139/145.
- The native machine writes `lowerBodyState`, `playerProps.moveSpeed` and `playerProps.headingAngle`/`g_Player_HeadingAngle` (offset from facing: 0 forward, 180 back, +90 right, -90 left). `func_8007B924` copies the speed into `D_800C4550`. At the end of `Player_LogicUpdate` (`player_control.c` ~4142), `player->headingAngle = rotation.vy + g_Player_HeadingAngle` and `player->moveSpeed = D_800C4550`. `func_8007C0D8` then moves Harry with collision. The PC shim (TPS/OTS/FPS, 2D control) feeds the same two globals.
- Native movement transitions are keyframe-gated: walk to run only at kf 10-11/21-22, a sidestep only releases at kf >= 117 (right) / 92 (left), and releasing forward decelerates to a stop before any other direction can start. That gating is what makes direction changes feel like "stop, then switch".
- Native sidestep speed peaks at 1.25 and pulses (accelerates only inside kf 100-111); walk forward 1.4, walk back 1.15, run strafe 3.1739, run forward 3.5/4.0/5.0 by speed zone.
- The running wall-stop anims double as the skid-stop when the player releases run after at least 5 running steps at speed >= 3.125. Stumble plays when running into certain surfaces after 10 m of running.

## Revision (classic path)

The first draft rerouted classic into the PC shim and re-ported wall-stop/stumble/half-tilt by hand. Reading the native machine showed those mechanics are tangled with keyframe gates and combat exits. Instead, classic stays on the native machine and blending is layered around it. Every native mechanic (wall-stop, skid-stop, stumble, quick-turn, jump-back, exhaustion, speed zones, half-tilt walk, combat exits) keeps working unchanged. Only the TPS/OTS/FPS path needs ported mechanics.

## Architecture

```
            classic                                   TPS / OTS / FPS
  Player_LowerBodyUpdate                          PC shim (input, body yaw)
    pre-hook: direction shortcuts                   anim selection -> EnterClip
    native cases (unchanged)                         (no snap to clip start)
            \                                            /
             +---------- end of Player_LogicUpdate ------+
                  motion hook: Pc_LocoBlend_MotionApply
                  (intent -> diagonal heading + speed)
                               |
                         func_8007C0D8 (collision move)
                               |
                         Player_AnimUpdate
                  native playback, then pose hook:
                  Pc_LocoBlend_PoseApply (weights, phase, sampler)
```

Units:

1. **Math (`pc_loco_blend_math.c/.h`)**, pure and unit-tested: direction/gait weights, weight easing, clip phase and sample keyframes, the weighted multi-clip pose sampler with orthonormalization.
2. **Glue (`pc_loco_blend.c/.h`)**: enable check, intent per frame, motion override, ownership/phase/weight state, pose application, `EnterClip`, console command.
3. **Classic shortcuts** (`player_control.c`, `#ifdef SH_PC_PORT` static function called before the native switch): needs native internals (`func_8007D6F0`, `D_800C45C8`), so it stays next to them.
4. **Shim edits** (`player_control.c`): `EnterClip` instead of snapping to a clip's blend phase; TPS ported mechanics.

## Intent

Computed once per logic frame in the motion hook.

- **Classic:** from the native state after its case ran. Forward/RunForward = 0 degrees, WalkBackward = 180, sidestep/run-strafe = +-90. While in WalkForward, RunForward or WalkBackward with exactly one step button held: forward = +-45, back = 180 -+ 45 (back-right = 135). Gait = 1 for RunForward/RunLeft/RunRight, else 0. Aim variants (state - 20) map the same way.
- **TPS/OTS/FPS (and classic with `movement_original=0`):** left stick past a 24/128 deadzone gives a continuous angle `atan2(x, -y)`; otherwise WASD/d-pad flags give the 8 directions (sidestep hold or tap). Gait = `g_Player_IsRunning`, except 0 when moving back (there is no run-back clip; the shim moves at walk-back speed).
- **2D control on:** angle 0 (Harry faces travel); gait from `g_Player_IsRunning`.

## Motion

- Diagonal override only when the axis move (forward or back) is active together with a lateral input. Pure directions keep their native/shim motion untouched.
- Classic: in `PlayerState_None` and `PlayerState_Combat`, including the aim walk states (gun raised, locked on; revised 2026-10-11 after the legs slid under straight aim-walk motion), not during the walk-to-run transition.
- TPS: only when the shim's forward/back branch ran (`g_Player_IsMovingForward` or `g_Player_IsMovingBackward`); the shim's own pure-strafe branch already moves sideways and is left alone. Only in `PlayerState_None` or `PlayerState_Combat` and not during JumpBackward: other states (knockback, jump-back) own speed and heading.
- The classic intent runs only on the native machine (classic camera with `movement_original=1`); the legacy shim path (`movement_original=0`) uses the TPS intent.
- Heading: `g_Player_HeadingAngle` (and the player property copy) = intent angle.
- Speed keeps the native acceleration: `speed = |D_800C4550| * (w_axis + w_lateral * ratio)`, where `w` are the walk-set direction weights for the intent angle and `ratio` = lateral top speed / axis top speed:
  - Classic: walk forward 1.25/1.4, walk back 1.25/1.15, run forward 3.1739/4.0.
  - TPS: walk 0.72/1.5 (the shim's sidestep averages 0.024 m per keyframe at 30 keyframes per second), run 1.0 (shim run-strafe equals run-forward speed).
- At 45 degrees walking in classic this gives about 1.33 m/s, between forward 1.4 and sidestep 1.25: authentic per-direction speeds, interpolated.

## Classic direction shortcuts (pre-hook)

Run before the native switch, only when blend is enabled, not in a demo, `lowerBodyState < PlayerLowerBodyState_Aim`, `extra.state == PlayerState_None`, not in the walk-to-run transition, and upper body not `AimStartTargetLock`. F/B = moving forward/back, L/R = step hold, run = `g_Player_IsRunning`, `ahead` = the native `temp_s3` wall probe, which a switch replaces (see below).

| Current state | Input | New state |
|---|---|---|
| WalkForward, RunForward | not F, not B, L or R | run and side probe (from facing) clear: RunLeft/RunRight, else SidestepLeft/SidestepRight |
| WalkBackward | not F, not B, L or R | same as above |
| SidestepX, RunX | F | run and a fresh forward probe is None: RunForward, else WalkForward |
| SidestepX, RunX | B and not the jump-back input (native predicate incl. `extraWalkRunCtrl`) | WalkBackward (the jump-back input keeps the native path so jump-back still triggers) |
| SidestepLeft/RunLeft | R (not L) | the right-side mirror state (and vice versa) |
| WalkForward | B, not F, not the jump-back input | WalkBackward |
| WalkBackward | F, not B | run and a fresh forward probe is None: RunForward, else WalkForward |

All probes are taken from Harry's facing, not the movement heading: set `player->headingAngle` to `rotation.vy` plus the direction offset (forward 0, back 180, right +90, left -90), call `func_8007D6F0(player, &D_800C45C8)`, restore the heading. After a switch, `temp_s3` is replaced by the probe for the new direction so the native case that runs this frame sees a matching probe.

A switch sets the new state, sets `stateStep = 1` and `controlState = 0`, resets `runStepSfxCount`, `afkTimer` and `playerStopFlags` (what `Player_MovementStateReset` does, minus the stateStep reset), and calls `EnterClip` so the new clip starts at the current gait phase instead of its first keyframe. `moveSpeed` is kept; the native case accelerates or decelerates from it.

## Phase

- One `phase` in [0, 1).
- When the leg clip (`player->model`) is an active loop locomotion clip, it is the master: `phase = fract(clipPhase(master) - offset[master])`.
- Starting from idle (first frame of ownership with a locomotion master), phase is set to the master clip's start phase `fract(-offset[master])` and held there while the master is still in its native blend-in; it follows the master once that goes active. Without the hold a stale phase free-runs through the blend-in and pops.
- Otherwise (blend phases, walk-to-run transition) `phase += dt * sum(w_i * rate_i)` with `rate_i = duration_i / span_i` (variable durations evaluated through `Player_VariableAnimDurationGet`).
- Clip i samples at `start_i + fract(phase + offset_i) * span_i`.
- Offsets line up the first footstep plant of every clip with WalkForward's: WalkForward 0, RunForward 0.0326, WalkBackward 0.0326, SidestepLeft 0.3026, SidestepRight 0.3026, RunLeft 0.0903, RunRight 0.0134. Which foot plants first per clip is unverified; `LOCOBLEND OFFSET <clip> <value>` tunes it live, and the tuned values are then hardcoded.
- `EnterClip(model, anim)` sets the clip's active status and `anim.time` from the current phase, so a native or shim state change never restarts a gait cycle.
- Sidestep risk: the walking sidestep is a step-and-pause shuffle (plants at 0.52/0.92). If phase-locking it with walk looks wrong, give sidesteps their own phase. Decided in playtest.

## Weights

- Directions per gait: forward, right, back, left. A local angle picks its two neighbors with linear angle weights (45 degrees = 0.5 WalkForward + 0.5 SidestepRight).
- Gait blends the walk set with the run set (`w_walk = 1 - gait`, `w_run = gait`). The run set has no back clip; back-run weight goes to WalkBackward.
- Current weights move toward target by `dt / blendTime` per frame (default 0.15 s, `anim_blend_rate_ms`), then renormalize.
- Not moving: weights hold; the ownership fade (below) hands the legs back to the native idle.

## Ownership (start, stop and handoff)

- `ownership` in [0, 1]: 0 = native pose only, 1 = blended pose only. Final bone = `(1 - ownership) * native + ownership * blended`.
- Target 1 when: enabled, not a demo, `!g_Player_DisableControl`, `extra.state` is None or Combat, and either the leg clip is a locomotion clip (active or blend phase) or the walk-to-run transition is running. Otherwise target 0.
- Ownership moves toward the target at `dt / 0.12 s`.
- This covers starting (idle into walk), stopping (walk into idle), and every non-locomotion action (wall-stop, stumble, quick-turn, jump-back, attacks, scripted states): those anims are not locomotion clips, so the blend fades out over 0.12 s and fades back in afterwards. Sub-project 2 generalizes this into a crossfade for every action anim.
- With ownership 0 and target 0 the pose hook returns without touching any bone.

## Idle and run-stop crossfade (added 2026-10-11)

Native leg changes between idle, moving and the run-stop/wall-stop/stumble clips use short `Anim_BlendLinear` blend-ins (about 0.1 s into idle and about 0.02 s into a run-stop). Those blend-ins start from a stored keyframe rather than from the pose on screen, so they pop, especially when the blended pose is on screen.

- The leg clip is classified each frame into a family key: "moving" (any locomotion clip, or the walk-to-run transition), or one key per idle-family clip (Still, TransitionToStill, TurnLeft/Right, LookAround, Idle, IdleExhausted) or stop-family clip (the run wall-stops and stumbles). Anything else gets no key.
- The final on-screen pose is stored every frame. When the key changes between two keyed families, the pose crossfades from the stored pose to the new one over `anim_blend_xfade_ms` (default 200, 0 = off; console `LOCOBLEND XFADE <ms>`) on a smoothstep curve. Rotations stay rigid (`PcLocoMath_LerpPose`).
- The legs always fade. The upper body fades only while its clip is keyed and Harry is not aiming.
- No fade when entering a non-keyed anim (attacks, reactions, quick-turn, jump-back, scripted) or when control is disabled. The stored pose is discarded after gaps longer than 200 ms (menus, loads, scenes).

## Sampler

`PcLocoMath_SamplePose(hdr, coords, boneMask, samples, count, nativeWeight)`:

- Same decode as `Anim_BoneUpdate`: 3-byte translation `(f0 << scaleLog2) + (((f1 - f0) * alpha) >> (12 - scaleLog2))`, minus `rootYOffset` on y for translation index 0; 9-byte rotation `(f0 << 5) + (((f1 - f0) * alpha) >> 7)`.
- Per bone in `boneMask` (root skipped): accumulate `nativeWeight * current + sum(w_i * decoded_i)`.
- Rotation rows are Gram-Schmidt orthonormalized, then rescaled to the weighted mean of the source row lengths, so blended diagonals do not shrink limbs. Skipped when there is exactly one sample of weight 1 and `nativeWeight` is 0, which then reproduces `Anim_BoneUpdate` bit for bit.
- The pose hook calls it for the lower body (`HARRY_LOWER_BODY_BONE_MASK`) and copies the hips translation to the torso afterwards (as `Anim_BoneUpdate` does). It blends the upper body too (`HARRY_UPPER_BODY_BONE_MASK`) when the upper clip (`extra->model`) is a locomotion clip, so arms swing with the blend; aiming, attacking and reloading keep their native upper pose.

## Footsteps

The master leg clip keeps playing natively, so the existing footstep dispatcher keeps firing from its keyframes. One step per plant, no doubles. In a classic diagonal the master is the axis clip (forward or back), so its footsteps play.

## TPS/OTS/FPS ported mechanics

Run in the shim when blend is enabled and an alternate camera is active (never on the classic camera's legacy shim path), only in `PlayerState_None` (not while aiming), mirroring the native conditions:

- **Running wall-stop:** state RunForward, `runDistance >= 10 m`, and actually blocked this frame (moved less than half of `g_Player_LastMoveStep`). Plays RunForwardWallStopLeft or Right by the run-cycle half the master clip is in.
- **Stumble:** the wall probe `func_8007D6F0` returns WalkForward while running after `runDistance >= 10 m`.
- While one of these plays: no shim movement input, speed decays like the native case, and it ends at the clip's last keyframe.
- `PlayerFlag_WallStopRight` is set or cleared with the wall-stop variant, as natively. An action is cancelled if its anim is replaced by anything else (no state is written on cancel). A 2 s game-time timeout ends any action as a softlock backstop (logged, lower-body state reset to None).
- The skid-stop is not ported to the alternate cameras yet: natively it needs the run clip held until its kf 33-34 / 43-44 window after forward is released, so a port needs a latched release (follow-up). A one-frame window test fired at random.

## Gating, config, menu, launcher

- `g_PcConfig.animBlend` (`anim_blend`, default 0) and `g_PcConfig.animBlendRateMs` (`anim_blend_rate_ms`, default 150, config only).
- `Pc_LocoBlend_Enabled()` = `animBlend && !(sysFlags & SysFlag_DemoActive)`. Every hook checks it first; the pose hook additionally finishes a fade-out after the toggle goes off.
- Menu: `Anim_Blending` ON/OFF row on the Controls page (`PCOPT_C`), labels in `lang_menu.c` (DE/FR/ES/IT), `lang_jpn_pcopt.inc`, `lang_ru_menu.inc` and the English localization JSON.
- Launcher: "Animation blending (8-way)" checkbox in `ControlsForm.cs`, translated in `Loc.Strings.cs`. Bump `AssemblyFileVersion`, build and deploy to `pc_port/build`.
- Console: `LOCOBLEND` (toggle `SH_DBG` trace of intent, weights, phase, ownership), `LOCOBLEND OFFSET <clip 0-6> <value>`.

## Testing

Unit tests (`pc_loco_blend_test`, ctest with `-DBUILD_TESTING=ON`, same pattern as `pc_big_tmd_test`):

- Weights: 8 directions and an angle sweep at several gaits; weights sum to 1; correct neighbors; back + run maps to WalkBackward; angles outside [0, 360) wrap.
- Easing: reaches the target after `blendTime` regardless of how the time is split into frames.
- Phase: clip phase and sample keyframes round-trip; wrap from the last keyframe to the first; keyframes stay within the clip.
- Sampler on a synthetic ANM: one sample at weight 1 matches the `Anim_BoneUpdate` formula exactly; two samples at 0.5 average translations and give orthonormal rotation rows with preserved length; `nativeWeight` mixes with the existing pose; bones outside the mask stay untouched.

Toggle-off safety: every call site is gated, so `anim_blend=0` runs no new code (pose hook returns at ownership 0).

Playtest matrix (the user runs it on `pc_port/build`): tank, TPS, OTS, FPS and 2D control × walk/run, 8 directions, analog sweep, start/stop, quick-turn, jump-back, aim-walk, melee lunge, wall-stop forward/side, skid-stop, stumble, stairs/slopes, speed zones, exhaustion, cutscene entry/exit, doors and item pickups. Anything odd is checked against the `LOCOBLEND` trace in the log.

## Risks

- The sidestep shuffle may not phase-lock nicely with walk (fallback in Phase).
- Footstep plant offsets may pair the wrong feet until tuned.
- The classic diagonal changes the direction of the native forward wall probe (it follows `player->headingAngle`), so wall-stops trigger against walls along the diagonal. This is intended.
