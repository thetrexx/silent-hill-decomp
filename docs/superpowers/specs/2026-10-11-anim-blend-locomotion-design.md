# Blended 8-Way Locomotion for Harry: Design

Date: 2026-10-11
Status: approved in design review, awaiting spec review
Sub-project: 1 of 2 (sub-project 2, generic action crossfade, gets its own spec later)

## Goal

Optional PC-only mode where Harry's locomotion blends smoothly between animations, the way modern 8-way movement does. Holding forward and strafe-right moves him forward-right with blended legs. Changing direction, gait or speed eases from one pose to the next instead of snapping or stopping first.

Works in both control families:

- Classic tank controls with the fixed cameras. Left/right still turn; the step buttons now combine with forward/back.
- The TPS, OTS and FPS cameras (strafe-style, body tracks the camera).

Target outcome: something playable to show SlickAmogus. His alternate cameras currently look wrong because the original one-direction-at-a-time animations do not fit free movement.

## Constraints

- Default off. With the toggle off, behavior is identical to today, and the original PSX lower-body machine stays the classic default.
- Attract demos always run the native PSX machine, whatever the setting.
- The PSX lower-body state machine (`Player_LowerBodyUpdate`) is not modified.
- Speeds stay authentic per direction (see Motion).
- The core is kept in its own module so it can later move behind a hook for a mod build, if SlickAmogus declines it as a core option.

## Non-goals (this sub-project)

- Generic crossfade into and out of every action animation (attacks, damage, pickups, scripted states). That is sub-project 2, built on this core.
- New animation data. No new clips, no run-backward, no authored diagonals.
- NPC or monster blending.

## Background

- All of Harry's clips live in one keyframe buffer. `Anim_BoneUpdate` (`src/bodyprog/gfx/bodyprog_anim_800445A4.c`) decodes per-bone 3-byte translations and 9-byte rotation matrices and lerps between two keyframes.
- Lower and upper body are masked separately through `g_SysWork.playerWork.extra.disabledAnimBones`. `player->model` drives the legs; `extra->model` drives the upper body.
- `s_AnimInfo` gives each clip's `startKeyframeIdx`, `endKeyframeIdx` and `duration` (constant, or `variableFunc` for speed-matched clips). `anim.time` is an absolute Q12 keyframe position.
- Locomotion clips available: Still, WalkForward, WalkBackward, SidestepLeft, SidestepRight, RunForward, RunLeft, RunRight (+ idle-exhausted, wall-stop and stumble variants).
- `player_control.c` has two movement paths. The native PSX machine runs for classic when `movement_original=1`. The PC shim (~1728 onward) runs for TPS/OTS/FPS and 2D control. Today in the shim, forward wins over strafe (~2389), and only pure strafe moves sideways (through `Collision_WallDetect`). The shim already has speed zones and the exhaustion idle; it lacks the running wall-stop, the stumble and half-tilt analog walking.

## Architecture

```
input (tank / TPS-analog) --> LocoIntent {local dir x/z, gait 0..1, turn}
                                   |
            +----------------------+-------------------------+
            v                                                v
   motion: per-direction PSX speeds              anim: weights over clips
   blended by intent -> velocity ->              {Still, WalkF, WalkB, StepL, StepR,
   Collision_WallDetect (existing)                RunF, RunL, RunR} + shared phase
            |                                                |
            v                                                v
   wall-stop / stumble triggers (ported)         sampler: N weighted keyframe pairs
   -> hand off to native anims                   -> lerp bone matrices -> orthonormalize
                                                 -> writes lower bones (+ upper if free)
```

New files: `pc_port/src/pc_loco_blend.c` and `pc_port/src/pc_loco_blend.h`. `player_control.c` only gains thin gated calls into them.

### Units

1. **Intent builder.** Input in, `LocoIntent` out: local move direction (unit vector or zero), gait 0 (walk) .. 1 (run), turn input.
   - Tank: forward/back from stick up/down or d-pad; strafe from the step buttons; left/right = turn. Half-tilt stick forward = walk (ported PSX analog walk).
   - TPS/OTS/FPS: WASD or d-pad give 8 directions; the left stick gives a continuous 360-degree direction, and its magnitude feeds gait. The sprint control forces gait 1.
   - 2D control on: direction is always forward (Harry faces travel); only gait and start/stop blend.
2. **Motion.** Velocity = intent-weighted mix of each direction's PSX speed (see Motion). Applied through the existing `Collision_WallDetect` path the shim already uses for strafing, so there is no tunnelling at any framerate.
3. **Weights.** Target weights from intent; current weights ease toward target. Output: up to 4 direction/gait clips + Still, summing to 1.
4. **Phase.** One shared normalized gait phase for all locomotion clips.
5. **Sampler.** Weighted multi-clip version of `Anim_BoneUpdate`.
6. **Handoff.** Yields the legs to native anims (wall-stop, stumble, jump-back, quick-turn, attack lunge, any non-locomotion state) and crossfades back in from a pose snapshot.
7. **Footstep bridge.** Feeds the existing footstep dispatcher.

## Motion

Per-direction speeds stay as the PSX/shim values:

- Walk forward and walk backward: `Q12(1.5f)`.
- Run forward: the existing shim value (speed-zone aware, `PC_SHIM_RUN_SPEED` / `PC_OTS_RUN_SPEED`).
- Walk sidestep: the keyframe-driven shuffle rate (`Q12(0.024f)` per keyframe time).
- Run strafe: in classic, the native machine's RunLeft/RunRight speed (read from `Player_LowerBodyUpdate` during planning). In TPS/OTS/FPS, the existing shim run-strafe speed (`PC_OTS_RUN_SPEED`).
- No run-backward: back + run = walk backward speed and clip.

Diagonal velocity = weighted sum of the two neighboring direction velocities, using the same weights as the animation. A diagonal is therefore slower than pure forward. This is intentional: authentic per-direction speeds.

## Weights

- Directions are the four axis clips per gait: forward, right, back, left. A local direction at angle θ picks its two neighbors with linear angle weights (forward-right at 45 degrees = 0.5 WalkF + 0.5 StepR).
- Gait blends the walk set with the run set: `w_walk = 1 - gait`, `w_run = gait`. The run set has no back clip, so back-facing run weight goes to WalkBackward.
- No input = Still weight 1. The exhaustion idle replaces Still when the existing exhaustion condition holds.
- Current weights move toward target at a fixed rate: a full swap in ~150 ms by default. The `anim_blend_rate_ms` config key (debug, no menu row) tunes it.
- At most 4 locomotion clips + Still are active.

## Phase

- One `phase` in [0, 1) per frame: `phase += dt * sum(w_i * rate_i)`, with `rate_i = duration_i / span_i` (cycles per second at the clip's native cadence; `variableFunc` durations are evaluated each frame).
- Clip i samples at `start_i + fract(phase + offset_i) * span_i`.
- `offset_i` exists because clips may not start their cycle on the same foot. The implementation plan includes a step to measure each clip's foot-plant keyframes and set offsets so foot plants line up.
- Sidestep risk: the walking sidestep is a step-and-pause shuffle, not a steady cycle. If phase-locking it with the walk cycle looks wrong in playtest, the fallback is a separate phase for the sidestep clips with their weight eased more slowly. This is decided by playtest, not up front.

## Sampler

`Pc_LocoBlend_Sample(anmHdr, coords, const PcLocoSample* samples, int n)`, where `PcLocoSample = { keyframe0, keyframe1, alpha, weight }`.

- Same decode as `Anim_BoneUpdate`: 3-byte translation with `scaleLog2`, `rootYOffset` on translation index 0, 9-byte rotation matrices, hips translation copied to torso for the player.
- Per active bone: accumulate the weighted per-clip lerp result for translation and matrix.
- Rotation matrices are re-orthonormalized after accumulation (float Gram-Schmidt; PC only) so blended diagonals do not shrink limbs.
- Bone mask: same as `Anim_BoneUpdate` for the player (`~disabledAnimBones`). Upper-body bones are only written when the upper body is free (not aiming, attacking or reloading); otherwise the existing upper-body FSM owns them.
- A single sample with weight 1 must produce output identical to `Anim_BoneUpdate` (except the orthonormalize step, which is skipped for n == 1).

Hook point: in `Player_AnimUpdate`, after the normal playback call for `player->model` (and `extra->model` when the upper body is free), when the blend owns the legs. The normal call still runs so anim time, keyframe and link bookkeeping keep working for every other system.

## Handoff and snapshot crossfade

- The blend owns the legs only while the player is in locomotion: lower-body state is one of the walk/run/strafe/still states (or their aim variants), with no native anim playing that the blend must yield to.
- Yield cases: running wall-stop, stumble, jump-back, quick-turn, attack with root motion, and any player state outside None/Combat (scripted scenes, damage, grabs, pickups, doors).
- On yield: snapshot the current lower (and, if owned, upper) bone coords.
- On resume: crossfade from the snapshot to the blended pose over ~120 ms. Same primitive as entering locomotion from a native anim. Sub-project 2 generalizes this snapshot crossfade to every action anim.

## Ported PSX mechanics

In blended mode, under the same conditions as the native machine:

- **Running wall-stop:** running with forward weight dominant and blocked ahead plays RunForwardWallStop (L or R foot variant by current foot phase). Strafe-running blocked to the side plays RunLeftWallStop/RunRightWallStop.
- **Stumble:** RunForwardStumble / RunLeftStumble / RunRightStumble, triggered as in the native machine.
- **Half-tilt walk (tank):** a partially tilted stick walks instead of running.

The implementation plan reads the native trigger conditions in `Player_LowerBodyUpdate` and mirrors them. The blend yields while these play and crossfades back.

## Footsteps

The dominant-weight clip determines which locomotion `lowerBodyState` (or aim variant) the footstep shim (~2761) sets, and that clip's synced keyframe is what the dispatcher reads. Rule: one step sound per foot plant, no doubles while weights cross over.

## Gating and routing

- `g_PcConfig.animBlend` (`anim_blend`, default 0).
- Classic camera + blend on: routes into the shim path, like 2D control does (gate near `player_control.c` ~1681). The attract demo condition stays first.
- Shim + blend on: new `Pc_LocoBlend_Update(player, extra)` call replaces the shim's motion and anim-status selection block (~2280-2530). With blend off, that block runs unchanged.
- TPS/OTS/FPS: same shim entry, intent from the existing WASD/stick reads.

## Config, menu, launcher

- `pc_config.c`: `anim_blend` key (0/1, default 0) and `anim_blend_rate_ms` (debug, default 150).
- `src/screens/options/options.c`: `Anim_Blending` ON/OFF row on the Controls page (`PCOPT_C`, currently 9 of 11 rows). Labels added to every language pack that carries PC-option labels.
- Launcher: checkbox next to 2D controls in `ControlsForm.cs`, translated in all launcher languages. Bump `AssemblyFileVersion`, build and deploy.
- Console: `locoblend` command toggles an `SH_DBG` trace of intent, weights, phase and handoff events.

## Testing

Unit tests (ctest, `BUILD_TESTING`, same pattern as `pc_big_tmd_test`):

- Intent to weights: 8 digital directions and sampled analog angles; weights sum to 1; neighbors correct; back + run maps to WalkBackward.
- Weight easing reaches the target in the configured time, independent of framerate.
- Phase advance and wrap; per-clip sample keyframes stay inside each clip's range.
- Orthonormalize: output rows are unit length and mutually orthogonal within tolerance.
- Sampler on a synthetic ANM header: a single clip at weight 1 matches `Anim_BoneUpdate` exactly; two clips at 0.5/0.5 match a hand-computed result.

Toggle-off safety: with `anim_blend=0` no new code path runs (gated at the call sites).

Playtest matrix (the user runs it on `pc_port/build`): tank, TPS, OTS, FPS and 2D control × walk/run, 8 directions, analog sweep, start/stop, quick-turn, jump-back, aim-walk, melee lunge, wall-stop forward/side, stumble, stairs/slopes, speed zones, exhaustion, cutscene entry/exit, doors and item pickups. Anything odd is checked against the `locoblend` trace in the log.

## Risks

- Sidestep shuffle may not phase-lock nicely with walk (fallback described in Phase).
- Matrix-lerp artifacts on large pose differences (mitigated by orthonormalize).
- Footstep timing during crossovers.
- Root-motion-bearing native anims (jump-back, lunges) must keep their motion; the handoff yields them the legs and motion entirely.
