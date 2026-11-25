# Render-only culling of distant offscreen characters

Implemented 2026-11-23. This is an explicit gameplay-rendering approximation,
not occlusion culling or a claim that a hidden body can never cast a visible shadow.

## Policy and controls

- `r_characterCull 1` (archived, default): skip eligible distant offscreen body
  submissions before renderer bones, entity lighting/constants, raster shadows
  and RT caster selection. `0` restores the old renderer path and render history.
- `r_characterCullDistance 256` (archived): base keep distance in world units;
  `0` disables the approximation. Increasing it keeps more offscreen bodies.
- `r_characterCullStats 1` (cheat; `2` also lists per-body decisions): once per second prints skipped/total eligible
  body submissions and whether the scene bypasses the approximation. Powerup
  body copies count as separate submissions; these are not unique NPC counts.

Only `RT_MODEL` submissions with a Ghoul2 instance and `RF_CHARACTER` qualify.
Independent projectiles, dropped/thrown weapons, beams, sprites, Force FX,
dynamic lights and gameplay entities are not filtered. CG_Player and server
simulation always continue; required gameplay/bolt animation work remains.

The character sphere must lie outside an expanded main-camera frustum AND
outside the keep zone of BOTH the local player and camera. The sphere uses
`max(64, entity.radius) * max(1, maximum axis scale)`. Each side of the camera
FOV is expanded by 5 degrees (10 degrees added to the total FOV), with another
32 world units added to the silhouette sphere. Normalized plane tests retain
partially visible spheres at wide FOVs.

The keep radius is:

```
baseDistance + 4 * max(bodyRadius - 64, 0)
             + 2 * max(max(heightAbovePlayer, heightAboveCamera) - 96, 0)
```

Height differences use absolute values. This conservatively keeps large/scaled
and elevated casters farther away. The camera has its own keep zone to support
third-person and remote views. The nearest `RF_CHARACTER_PLAYER` submission
supplies player origin; when no local body is supplied, camera origin is used.

SP cgame marks the local/controlled body as `RF_CHARACTER_PLAYER` and exempts
Boba Fett, rocket troopers, AT-ST and flying/swimming movement with
`RF_CHARACTER_NO_CULL`. MP marks the local snapshot player, Boba Fett, AT-ST and
jetpack wearers/users. These are renderfx bits only: refEntity/refdef public
layouts and renderer API version do not change. Other renderers ignore the bits.
Body powerup copies retain the hints; weapon submissions do not inherit them.
Modded larger/elevated models also receive geometric keep-distance expansion.

## Conservative bypasses

Own body, first-person/depth-hacked models, nodepth/post-render and shadow-only
bodies remain exact. Cinematics, UI/no-world scenes, automap, Force Sight and
skyportal entities bypass the approximation. A loaded portal/mirror shader or
submitted portal surface disables it for the whole scene, preserving secondary
color-camera visibility. The portal inventory is cached by world, shader
inventory and caster generation; it does not scan all loaded shaders per frame.
`r_nocull 1` also bypasses it.

The permitted image difference is a distant ordinary offscreen body's shadow
or indirect visibility disappearing when its body leaves the guarded camera
frustum. The near zone and flying/giant exemptions reduce this. This is not a
strict visibility proof for unusually low sun, a distant ledge or a custom map.
For those cases use a larger distance, `RF_CHARACTER_NO_CULL`, or disable it.

## Pipeline and temporal correctness

`R_CharacterCullBeginScene` snapshots `characterRenderCulled` before
`R_RtScenePrepare`, `R_GatherFrameViews` and `RB_UpdateConstants`. All passes use
that one decision. Culled entities skip renderer bone evaluation/upload,
entity lighting/UBOs, general raster submission, projected shadows, hard sun
layers, per-character sun layers and RT selection.

The point-shadow dynamic-caster key excludes exactly the same culled bodies.
Turning the camera or toggling the policy therefore invalidates full cached
shadows even when scene time is paused. Static+dynamic shadow overlays restore
the previous damage footprint before drawing the next frame; the disappearing
body does not leave a persistent shadow behind.

Previous UBO histories naturally omit skipped models. On reentry with the
policy enabled, stale render-smoothing touches are cleared before evaluation;
first-frame velocity uses current matrices when no previous-frame record exists.
This does not reset gameplay final bones. With the policy disabled, the old
smoothing behavior is retained.

## Validation

The dependency-free `tests/rend2_character_cull_test.cpp` exercises the actual
shared policy: disabled/zero-distance modes, near boundaries, visible and
edge-of-FOV bodies, normalized wide-FOV planes, large/elevated casters,
player/camera keep zones, turn-back visibility, protected/independent objects,
invalid bounds/camera, and render-history reentry/disabled behavior.

Build with `-DBuildCharacterCullTests=ON`, then run
`ctest --test-dir build/msvc -C Release -R rend2_character_cull --output-on-failure`.
Local validation on the resumed run:

- MSVC Release SP/MP renderers, SP game and MP cgame build successfully; the
  dependency-free CPU suite passes all 25 policy/history checks.
- Intel UHD GL 3.2 at 640x360 on `mp/ffa3`: seven body submissions (local player,
  ordinary NPCs, Boba Fett and AT-ST). The far-back view skips 4/7, the turn-back
  view restores all (0/7 skipped), and the nearer back view skips only 1/7.
  Own body and both explicitly protected bodies remain in every view.
- The world image is byte-identical between on/off in all three views. The
  initial harness allowed debug notifications in the top 48 rows; excluding
  that UI area (top 64 rows) gives zero differing RGB channels. This was a
  harness issue: the final runner disables notifications and stats during
  captures rather than relaxing a renderer correctness tolerance.
- The run was non-isolated because a foreign smoke-test game was active; these
  checks establish functionality, not FPS gain. No historical or contaminated
  timing is advertised as this change's speedup.

The reusable runner is `tools/rend2/test_character_cull_runtime.py`:

```powershell
python tools/rend2/test_character_cull_runtime.py --installation C:/path/to/game --run
python tools/rend2/test_character_cull_runtime.py --installation C:/path/to/game --gl43 1 --velocity --shadows --run
```

It stages private binaries/configs/homepaths and never modifies installed game
assets. It refuses foreign games for performance runs; explicit
`--allow-foreign-correctness` permits functional checks only and records the
non-isolated inventory. `--benchmark` adds uncapped ABBA complete-frame windows
with counters/GPU query logging disabled. The final combined-source check is
recorded separately after integration.

## Combined integration check (2026-11-25)

Both worktree changes were merged into `rend2-unified-wip` at `898a2864978261846fc1468cc8fbb302e6d235f1`.
The combined MSVC x64 Release SP/MP renderers, SP game, MP cgame and character
policy test built successfully; all 25 CPU policy/history checks passed.
A private hidden runtime run exercised GL4.3, the velocity prepass, modern sun
shadows and exact sun-cache mode 1 together with character culling. All seven
off/on/turn-back/near phases completed. All four capture comparisons had zero
differing RGB channels, distant-body culling was exercised, and protected bodies
remained present. Installed game files were not modified.

The run observed foreign game processes and is correctness evidence only;
no isolated FPS improvement is claimed. See the retained
[combined receipt](../tools/rend2/optimization_defaults/integration-2026-11-25.json).
