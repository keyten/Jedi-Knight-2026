# Foliage, wind and interaction

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

SP means single-player; MP means multiplayer.

## Recognizing stock plants

Lets later wind and interaction features find likely leaves, ferns and plant surfaces without editing stock models. Classification by itself does not animate anything.

**Main controls**

- `r_autoFoliage 0 | 1 | 2` — off / conservative stock-safe detection / broader experimental detection.

**Debug**

- `r_autoFoliageDebug 1 | 2` — classified surfaces / classified plus uncertain candidates.
- `r_autoFoliageList [filter]` — print detected foliage.

Names and supported model structure select conservative foliage candidates at load time. The broader mode accepts more uncertain candidates; explicit shader choices remain available. [Details](../rend2-auto-grass.md).

## Grass that stays in place

*Requires existing surface-sprite grass, because this changes its billboard geometry.*

Stops grass turning to face the camera and disappearing edge-on. Crossed cards make stock Yavin grass look more like a patch of blades. Medium performance cost in dense grass because there is more geometry and overdraw.

**Main controls**

- `r_grassCardMode 0 | 1 | 2 | 3` — legacy / two-card cross / three-card tuft / adaptive three-to-two cards.
- `r_grassCardLodDist <units>` — third-card fade distance in adaptive mode.
- `r_grassCardWidth <value>` — card width multiplier.

**Debug**

- `r_grassCardDebug 1..6` — card ID/direction, forced card counts and LOD visualization.

Instead of one camera-facing card, the renderer draws two or three fixed crossing cards. The adaptive mode removes the extra card with distance.

## Shared wind through grass

*Requires surface-sprite vegetation that supports wind, because it changes the existing wind deformation.*

Makes neighboring plants share broad gusts instead of waving in synchronized circles. Stock grass can use it without new assets. Light performance cost.

**Main controls**

- `r_foliageWind 0 | 1` — legacy sway / coherent breeze.
- `r_foliageWindStrength <value>` — global strength.
- `r_foliageWindSpeed <value>` — time scale.
- `r_foliageWindDirection <degrees>` — world-space yaw.

**Debug**

- `r_foliageWindDebug 1..4` — exaggerated motion, bend color, frozen time and large-gust visualization.

A world-space wind pattern combines a broad breeze with smaller motion. The vertex shader samples that common pattern at each plant. [Details](../rend2-foliage-wind.md).

## Leaf flutter

*Requires `r_autoFoliage 1` or `2`, because only classified leaves should flutter.*

Adds small quick leaf motion while trunks and branches stay still. Look at vines and leaf cards on stock Yavin models. Light performance cost.

**Main controls**

- `r_leafFlutter 0 | 1` — disable / enable leaf flutter.
- `r_leafFlutterStrength <value>` — position amplitude.
- `r_leafFlutterSpeed <value>` — animation speed.
- `r_leafFlutterNormal <value>` — lighting-normal wobble.

**Debug**

- `r_leafFlutterDebug <bits>` — exaggerate, freeze, highlight fluttering surfaces and visualize displacement.

Small vertex and normal offsets imitate high-frequency leaf motion. Classification keeps the effect away from rigid non-leaf parts. [Details](../rend2-leaf-flutter.md).

## Bending ferns and fronds

*Requires automatic foliage classification, because the renderer must identify supported plant fronds; contact also needs `r_foliageInteraction 1`.*

Makes ferns bend frond by frond, swing back after contact and respond to wind without stretching around a character. Idle contact is steadier after the recent fern fix. Light to medium CPU cost near many plants.

**Main controls**

- `r_plantWind 0 | 1` — disable / enable the breeze on the fronds.
- `r_plantWindStrength 0..4` — breeze strength.
- `r_plantStiffness <value>` — frond stiffness (1 = default).
- `r_plantSimDistance <units>` / `r_plantWindDistance <units>` — full simulation / coarse simulation / at rest.
- `r_plantSimMaxPlants <count>` — plants simulated per frame.

**Debug**

- `r_plantDebug <bits>` — frond centerlines, segment or bone colors, contact sides, CPU cost.
- `r_plantSimInfo` — print the simulated plants; `r_autoFoliageList` also prints the frond skeletons.

The renderer builds short segment chains from supported MD3 plants and simulates nearby chains on the CPU. Shared poses are used for the visible plant and its shadows. [Details](../rend2-foliage-interaction.md).

## Walking through vegetation

*Requires matching updated game/client modules, because they send character contact shapes; fern contact also needs classified plant models.*

Makes grass and ferns move only where bodies touch them. Grass bends at the legs, springs back and can retain a short trampled trail instead of remaining rigid. Light to medium performance cost in dense nearby foliage.

**Main controls**

- `r_foliageInteraction 0 | 1` — disable / enable character interaction.
- `r_foliageInteractionStrength <value>` — contact depth scale.
- `r_foliageInteractionRadius <value>` — character capsule radius scale.
- `r_foliageInteractionMaxInteractors <count>` — uploaded character limit.
- `r_foliageInteractionNPCs 0 | 1` — player only / player + nearby NPCs and players.
- `r_grassTrample 0..1` — how much of the bend trampled grass keeps; `r_grassTrampleRecovery <seconds>` — its recovery.
- `r_grassBendSegments 1..8` — segments of a near grass sprite (1 = flat cards); `r_grassBendLodDist <units>` — how far the curved strips are used.
- `r_foliageBendFieldSize 64 | 128 | 256` — grass state texture size. Requires `vid_restart`.
- `r_foliageBendFieldExtent <units>` — world size of the grass state around the player.
- `r_foliageBendFieldStrength <value>` — grass displacement scale.
- `r_foliageBendFieldRecoveryTime <seconds>` / `r_foliageBendFieldDamping <value>` / `r_foliageBendFieldImpulse <value>` — grass spring back.

**Debug**

- `r_foliageInteractionDebug <bits>` — capsules, exaggerated interaction, interaction-only view, grass contact heat, freeze and player-only modes.
- `r_foliageBendFieldDebug <bits>` — grass state overlay, covered square, freeze, exaggeration.
- `r_foliageBendFieldClear` — clear the grass state.
- `r_foliageInteractionList` — print submitted/uploaded interactors and the grass state.

Character capsules push plant chains and a small local grass-state texture. The texture stores bend and recovery, while nearby grass is split into curved strips. [Details](../rend2-foliage-interaction.md).

## Force push and pull through vegetation

*Requires `r_foliageInteraction 1` and updated game/client modules, because Force events must submit their direction and reach.*

Makes player and NPC Force push/pull sweep through plants, grass and leaves. Vegetation moves away from the caster for push and toward them for pull, then settles. Light additional performance cost.

**Main controls**

- `r_foliageForceWaves 0 | 1` — Enable Force waves; default 1, with foliage interaction.
- `r_foliageForceWaveStrength 0..4` — Wave strength; default 1.

**Debug**

- `r_foliageInteractionDebug 1` — Draw the wave front and reach: orange push, magenta pull.
- `r_foliageInteractionList` — Print live waves alongside contact information.

An expanding front inside the Force cone applies a bounded impulse to nearby plants and the local grass field. It does not affect plants behind the caster or beyond the event’s reach. [Details](../rend2-foliage-interaction.md).

## Cutting plants with a saber

*Requires classified MD3 plants with the plant simulation active, and matching updated game/client modules, because a real attacking blade sweep must reach the renderer.*

Lets saber attacks cut fern fronds and supported plant cards. A partial cut stays attached; a severed piece falls, lands and later fades. A blade merely resting in a plant does not cut. Medium CPU and geometry cost while cutting, bounded by the piece limit.

**Main controls**

- `r_foliageSaberCut 0 | 1` — Enable cuts; default 1. Needs `r_autoFoliage` with `r_plantWind` or `r_foliageInteraction`.
- `r_foliageSaberCutPieces <count>` — Maximum live fallen pieces; default 48, 0 makes severed parts vanish.
- `r_foliageSaberCutPieceTime <seconds>` — How long a piece lies on the ground; default 20.

**Debug**

- `r_foliageSaberCutReset` — Restore cut plants.
- `r_foliageSaberCutTest [height] [partial]` — Cut the nearest plant with a test sweep; height is a fraction, default 0.5.
- `r_plantDebug 32` — Draw recent blade steps; red means they cut a plant.
- `r_plantSimInfo` — List cut plants and fallen pieces.

The blade’s swept plane cuts the plant mesh in its current pose. Pieces still connected to the base remain simulated; detached pieces become falling geometry. Cuts stay until map change or reset. [Details](../rend2-foliage-interaction.md).
