# Grass, plants and wind

[All rendering settings](../rendering-features.md)

Grass and plants that look fuller, sway in a shared breeze, bend when you walk through them, and react to the Force and to sabers. Two kinds of vegetation exist in JKA:

- **Grass** (Yavin and similar maps) is made of "surface sprites": small flat pictures scattered over the ground by the map's shaders.
- **Plants** — ferns, bushes, vines, leaf cards — are ordinary 3D models (MD3). The renderer has to recognize them first ([below](#recognizing-stock-plants)).

## Recognizing stock plants

Stock plant models aren't marked as plants, so the renderer guesses from their names and structure which parts are leaves, fronds or stems. On its own this changes nothing visible, but [leaf flutter](#leaf-flutter), [frond motion](#ferns-and-fronds), [walking through plants](#walking-through-vegetation) and [saber cuts](#cutting-plants-with-a-saber) need it.

- `r_autoFoliage 0` — **0** off, **1** only safe matches, **2** also uncertain ones (experimental; may move things that shouldn't).

**Debug**

- `r_autoFoliageDebug 1`, `2` — color recognized surfaces; 2 also uncertain candidates.
- `r_autoFoliageList [filter]` — print recognized plants and their frond skeletons.

Design notes: [rend2-auto-grass.md](../rend2-auto-grass.md).

## Fuller grass

Original grass sprites always turn to face the camera, so they swivel as you walk and look thin from above. This draws each tuft as two or three fixed, crossing cards, so grass stays put and looks like a clump of blades from every angle.

**Cost:** medium in dense grass (more geometry and overdraw).

- `r_grassCardMode 0` — **0** original camera-facing sprites, **1** two crossed cards, **2** three cards, **3** three near, two far.
- `r_grassCardWidth 1` — card width.
- `r_grassCardLodDist 600` — mode 3: distance where the third card fades out.

**Debug**

- `r_grassCardDebug 1..6` — color by card or direction, force 1/2/3 cards, distance level.

## Wind through grass

Original grass sways each tuft in its own small circle, all in step. With this, gusts roll across a field: neighbouring grass leans together and waves of wind travel downwind. Works on stock grass.

**Cost:** light.

- `r_foliageWind 0` — 1 on.
- `r_foliageWindStrength 1` — sway strength.
- `r_foliageWindSpeed 1` — gust speed.
- `r_foliageWindDirection 30` — wind direction in degrees (0 = world +X). Plants use the same direction and speed.

**Debug**

- `r_foliageWindDebug 1..4` — exaggerated motion, color by bend, freeze time, show large gusts.

Design notes: [rend2-foliage-wind.md](../rend2-foliage-wind.md).

## Leaf flutter

Leaves and vines on plant models tremble slightly in the air while trunks and branches stay still. Needs `r_autoFoliage 1` or `2`.

**Cost:** light.

- `r_leafFlutter 0` — 1 on.
- `r_leafFlutterStrength 1` — amplitude; 1 is about 1.5 units.
- `r_leafFlutterSpeed 1` — speed.
- `r_leafFlutterNormal 0.25` — let lighting flicker with the motion; 0 motion only.

**Debug**

- `r_leafFlutterDebug <bits>` — 1 exaggerate, 2 freeze, 4 highlight fluttering surfaces, 8 color by displacement.

Design notes: [rend2-leaf-flutter.md](../rend2-leaf-flutter.md).

## Ferns and fronds

Ferns and similar plants bend frond by frond in the wind and swing back naturally after being pushed, instead of moving as one rigid model. Needs `r_autoFoliage 1` or `2`.

**Cost:** light to medium (CPU) near many plants.

- `r_plantWind 0` — 1 wind on fronds.
- `r_plantWindStrength 1` — wind strength, 0–4.
- `r_plantStiffness 1` — higher is stiffer, with a faster swing.

**How it works.** Each recognized plant gets a short chain of segments per frond, simulated on the CPU like a springy rod and used to bend the model and its shadow. Near plants get a full simulation, farther ones a coarse one, distant ones stay still.

- `r_plantSimDistance 1500` — full simulation within this distance.
- `r_plantWindDistance 3000` — beyond this, plants don't move.
- `r_plantSimMaxPlants 96` — plants simulated per frame (touched plants always are).

**Debug**

- `r_plantDebug <bits>` — 1 frond lines, 2 color by segment, 4 by bone, 8 by contact side, 16 print cost, 32 saber cut steps.
- `r_plantSimInfo` — list simulated plants, cut plants and fallen pieces.

## Walking through vegetation

Grass and ferns bend where your body or an NPC touches them and spring back after you pass. Grass you stood on stays a little flattened for a few seconds. Plants need `r_autoFoliage`; grass works as is.

**Cost:** light to medium in dense nearby vegetation.

- `r_foliageInteraction 0` — 1 on.
- `r_foliageInteractionStrength 1` — how far stems bend; 1 is about 45° at full contact.
- `r_foliageInteractionNPCs 1` — 0 only you, 1 also nearby NPCs and players.
- `r_grassTrample 0.25` — how much bend trampled grass keeps; 0 none.
- `r_grassTrampleRecovery 6` — seconds for trampled grass to stand up.

**How it works.** The game sends simple capsule shapes for nearby characters. Plants push their frond chains away from them. Grass uses a small texture around the player that stores how far each spot is bent and lets it spring back; near grass is split into curved blades so it can bend smoothly.

- `r_foliageInteractionRadius 1` — size of the character shapes.
- `r_foliageInteractionMaxInteractors 8` — characters per frame, you first.
- `r_grassBendSegments 4` — segments per nearby grass blade; 1 flat cards.
- `r_grassBendLodDist 800` — distance where curved blades become flat cards.
- `r_foliageBendFieldSize 256` *(restart)* — grass bend texture resolution (64, 128, 256).
- `r_foliageBendFieldExtent 1024` — area it covers around you.
- `r_foliageBendFieldStrength 1` — grass bend strength; 0 grass doesn't react.
- `r_foliageBendFieldRecoveryTime 0.8` — seconds for grass to come back.
- `r_foliageBendFieldDamping 0.6` — 1 no overshoot, lower more sway.
- `r_foliageBendFieldImpulse 1` — extra drag when running through.

**Debug**

- `r_foliageInteractionDebug <bits>` — 1 shapes, 2 exaggerate, 4 interaction only, 8 contact heat, 16 freeze, 32 player only.
- `r_foliageBendFieldDebug <bits>` — 1 bend overlay, 2 covered area, 4 freeze, 8 exaggerate.
- `r_foliageBendFieldClear` — reset grass.
- `r_foliageInteractionList` — characters sent, grass state, active Force waves.

Design notes: [rend2-foliage-interaction.md](../rend2-foliage-interaction.md).

## Force push and pull

A Force push or pull, by you or an NPC, sweeps a wave through plants, grass and leaves in front of the caster: away for push, toward for pull, then they settle. Plants behind the caster or out of reach don't move. On by default once `r_foliageInteraction 1`.

**Cost:** light.

- `r_foliageForceWaves 1` — 0 off.
- `r_foliageForceWaveStrength 1` — strength, 0–4.

**Debug**

- `r_foliageInteractionDebug 1` — wave front and reach: orange push, magenta pull.

## Cutting plants with a saber

Attacking with a saber cuts fern fronds and plant cards it sweeps through. A partial cut stays attached; a severed piece falls, lies on the ground and sinks away. A blade simply resting in a plant doesn't cut. Cuts last until the map changes. On by default once plants move (`r_autoFoliage` with `r_plantWind 1` or `r_foliageInteraction 1`).

**Cost:** medium while cutting, limited by the piece count.

- `r_foliageSaberCut 1` — 0 off.
- `r_foliageSaberCutPieces 48` — fallen pieces at once; 0 makes cut parts vanish.
- `r_foliageSaberCutPieceTime 20` — seconds a piece lies on the ground.

**Debug**

- `r_foliageSaberCutReset` — restore all cut plants.
- `r_foliageSaberCutTest [height] [partial]` — cut the nearest plant; height is a fraction, default 0.5.
- `r_plantDebug 32` — recent blade sweeps; red ones cut.
