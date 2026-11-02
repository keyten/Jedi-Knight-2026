window.FEATURES = {
 "post-processing/tone-mapping": {
  "group": "Image and camera",
  "title": "Tone mapping",
  "cvar": "r_toneMapMode",
  "sections": [
   {
    "title": "Tone mapping",
    "html": "<p>The renderer computes light far brighter than a monitor can show: sky, lamps, saber blades. The tone mapper squeezes that range onto your screen and decides how contrasty the image looks and what happens to very bright colors. Pick the look you like; every curve costs the same. Needs <code>r_hdr 1</code> and <code>r_toneMap 1</code>.</p>\n<ul><li><code>r_toneMapMode 0</code> — <strong>0</strong> Rend2's original filmic curve. <strong>1</strong> ACES, the film look of many modern games: stronger contrast; very bright colored lights can shift hue. <strong>2</strong> AgX-like: gentler contrast; bright colored lights fade to white without changing hue.</li></ul>\n<p><code>r_toneMapDebug 2</code> shows legacy / ACES / AgX side by side.</p>\n<h4>Debug</h4>\n<ul><li><code>r_toneMapDebug 1</code> — left legacy, right the current <code>r_toneMapMode</code>.</li><li><code>r_toneMapDebug 2</code> — legacy / ACES / AgX strips.</li><li><code>r_toneMapDebug 3..5</code> — raw HDR image, image after exposure, exposure in false color.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-tonemapping.md\" target=\"_blank\" rel=\"noopener\">rend2-tonemapping.md</a>.</p>"
   }
  ],
  "media": []
 },
 "post-processing/brightness-and-exposure": {
  "group": "Image and camera",
  "title": "Brightness and exposure",
  "cvar": "r_autoExposure",
  "sections": [
   {
    "title": "Brightness and exposure",
    "html": "<p>Like your eyes, the renderer adapts to the scene: dark rooms are brightened, sunny exteriors darkened. This is on by default. Everything in this section needs <code>r_hdr 1</code> and <code>r_toneMap 1</code>; without them brightness is fixed.</p>\n<ul><li><code>r_autoExposure 1</code> — 0 fixed brightness, 1 adapt to the scene.</li><li><code>r_exposureCompensation 0</code> — the simplest brightness control, in stops: <code>1</code> is twice as bright, <code>-1</code> half. Works in every mode below.</li></ul>\n<h3 class=\"sub\">Physical camera (optional)</h3>\n<p>Instead of the original adaptation you can use real camera settings, mostly useful for screenshots and fixed shots.</p>\n<ul><li><code>r_cameraMode 0</code> — <strong>0</strong> original exposure (above). <strong>1</strong> manual camera: brightness comes from ISO, aperture and shutter time. <strong>2</strong> automatic camera: meters the scene and adapts within the limits below.</li><li><code>r_cameraISO 100</code>, <code>r_cameraAperture 16</code>, <code>r_cameraShutterSeconds 0.008</code> — mode 1. Higher ISO, a smaller f-number or a longer shutter make the image brighter. The aperture does not blur anything: there is no depth of field yet.</li><li><code>r_cameraAutoEVMin -6</code> — mode 2: the darkest scene it fully adapts to. Raise it to keep dark areas dark.</li><li><code>r_cameraAutoEVMax 20</code> — mode 2: the brightest scene it fully adapts to. Lower it to keep bright areas bright.</li><li><code>r_motionBlurPhysicalShutter 0</code> — 1: in modes 1–2, <a href=\"#f/post-processing/motion-blur\">motion blur</a> length follows <code>r_cameraShutterSeconds</code> instead of the shutter angle.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Manual mode converts ISO, aperture and shutter into an exposure value (EV100), as a light meter would. Automatic mode measures the average scene brightness and moves toward it, clamped to the EV range. Exposure compensation is applied on top.</p></div>\n<ul><li><code>r_cameraExposure 0</code> — mode 0 only: a fixed gain in stops.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_cameraDebug 1</code> — print the camera and meter state once per second.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-physical-camera.md\" target=\"_blank\" rel=\"noopener\">rend2-physical-camera.md</a>.</p>"
   }
  ],
  "media": []
 },
 "post-processing/bloom": {
  "group": "Image and camera",
  "title": "Bloom",
  "cvar": "r_bloom",
  "sections": [
   {
    "title": "Bloom",
    "html": "<p>A soft halo around sabers, lamps and glowing panels. The original JKA \"dynamic glow\" blurs only surfaces whose shader is marked as glowing, with the same strength whatever their brightness. HDR bloom works from actual brightness: a brighter light glows more, and the halo is wider and smoother.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_bloom -1</code> — <strong>-1</strong> original dynamic glow (switched by the original <code>r_dynamicGlow</code>). <strong>0</strong> off. <strong>1</strong> HDR bloom; needs <code>r_hdr 1</code> and <code>r_toneMap 1</code>.</li></ul>\n<p>HDR bloom (<code>r_bloom 1</code>):</p>\n<ul><li><code>r_bloomIntensity 0.15</code> — strength of the halo.</li><li><code>r_bloomScatter 0.7</code> — how wide it spreads.</li><li><code>r_bloomSceneIntensity 0</code> — by default only glowing materials bloom. Raise it to let any very bright pixel bloom too: sun glints, bright sky, lit fog.</li></ul>\n<p>Original glow (<code>r_bloom -1</code>):</p>\n<ul><li><code>r_dynamicGlowBloom 0.0</code> — also let bright parts of the scene glow, not only glow-marked surfaces.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>HDR bloom takes the image before tone mapping, blurs progressively smaller copies of its bright parts and adds them back. Glowing materials always contribute; other pixels contribute only above a brightness threshold, scaled by <code>r_bloomSceneIntensity</code>.</p></div>\n<ul><li><code>r_bloomThreshold 2.0</code> — brightness where scene bloom starts.</li><li><code>r_bloomKnee 0.5</code> — how softly it fades in around the threshold.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-bloom.md\" target=\"_blank\" rel=\"noopener\">rend2-bloom.md</a>.</p>"
   }
  ],
  "media": []
 },
 "post-processing/color-grading": {
  "group": "Image and camera",
  "title": "Color grading",
  "cvar": "r_colorGrading",
  "sections": [
   {
    "title": "Color grading",
    "html": "<p>Applies a color filter to the whole image — warmer, colder, faded, more contrast — from a LUT file (<code>.cube</code>), the format photo and video editors export. Stock maps have no LUT, so nothing changes until you provide one. Needs <code>r_hdr 1</code> and <code>r_toneMap 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_colorGrading 1</code> — on; does nothing without a LUT.</li><li><code>r_colorGradingLUT \"\"</code> — a <code>.cube</code> file in the game data, for example <code>luts/warm.cube</code> in <code>base/</code> or a pk3. Empty uses the map's own LUT if it has one; <code>*identity</code> is neutral.</li><li><code>r_colorGradingIntensity 1</code> — 0 original colors … 1 the full LUT.</li><li><code>r_colorGradingCompare 0</code> — 1: original on the left, graded on the right.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> put <code>maps/&lt;map&gt;.cube</code> next to the <code>.bsp</code>; it loads with the map unless the player sets <code>r_colorGradingLUT</code>.</p>\n<div class=\"how\"><h3>How it works</h3><p>A 3D LUT is a table that gives an output color for every input color. The renderer looks up each pixel of the final image in it and blends the result with the original by <code>r_colorGradingIntensity</code>.</p></div>"
   }
  ],
  "media": []
 },
 "post-processing/motion-blur": {
  "group": "Image and camera",
  "title": "Motion blur",
  "cvar": "r_motionBlur",
  "sections": [
   {
    "title": "Motion blur",
    "html": "<p>Fast camera turns and moving characters or sabers leave short streaks, like film. The HUD and menus stay sharp. The original game has no motion blur. Needs <code>r_hdr 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; grows with resolution and sample count.</p>\n<ul><li><code>r_motionBlur 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_motionBlurShutterAngle 180</code> — streak length. 180° blurs half a frame of motion (the film standard); 360 is longer, 90 shorter.</li><li><code>r_motionBlurReferenceFps 60</code> — the frame rate the shutter angle refers to, so the blur looks the same at 60 and 144 fps. 0 uses your real frame time: blur gets shorter as fps rises.</li><li><code>r_motionBlurMaxPixels 32</code> — longest streak, in pixels at 1080p.</li><li><code>r_motionBlurQuality 1</code> — 0 low, 1 medium, 2 high. Higher removes graininess in long streaks.</li><li><code>r_motionBlurViewModelScale 0.5</code> — blur on your own hands, weapon and saber in first person.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Every pixel stores how far its surface moved since the last frame. The blur samples along that movement. A camera cut or teleport would produce a huge false streak, so a jump bigger than the limits below resets the history instead.</p></div>\n<ul><li><code>r_motionBlurSamples 0</code> — exact samples per pixel; 0 takes them from the quality.</li><li><code>r_motionBlurCutDistance 256</code>, <code>r_motionBlurCutAngle 75</code> — camera movement (units) or rotation (degrees) in one frame treated as a cut.</li><li><code>r_motionBlurShutterScale 1</code>, <code>r_motionBlurReset 0</code> — set by the game (longer exposure during Force Speed, reset after cuts). Don't change them.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_motionBlurDebug 1..5</code> — velocity, camera velocity, object velocity, sample count, blur contribution.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-motion-blur.md\" target=\"_blank\" rel=\"noopener\">rend2-motion-blur.md</a>.</p>"
   }
  ],
  "media": []
 },
 "post-processing/anti-aliasing-smaa": {
  "group": "Image and camera",
  "title": "Anti-aliasing (SMAA)",
  "cvar": "r_smaa",
  "sections": [
   {
    "title": "Anti-aliasing (SMAA)",
    "html": "<p>Smooths jagged geometry edges.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_smaa 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_smaa_quality 2</code> <em>(restart)</em> — 0 low, 1 medium, 2 high, 3 ultra.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>SMAA finds edges in the finished image and blends pixels along them. It does not fix shimmering of very thin moving details, such as distant fences or grass.</p></div>"
   }
  ],
  "media": []
 },
 "post-processing/linear-lighting-experimental": {
  "group": "Image and camera",
  "title": "Linear lighting (experimental)",
  "cvar": "r_linearLighting",
  "sections": [
   {
    "title": "Linear lighting (experimental)",
    "html": "<p>Every stock map stores its lighting in screen color values, and the game mixes textures and light in those values. Linear lighting converts everything to real light amounts first, as on HDR-lit maps. Overlapping lights, dynamic lights on lightmapped walls and soft gradients add up more naturally, but stock maps can look noticeably different.</p>\n<p>Known side effects: sabers, blaster bolts, explosions and other effects drawn by game code look brighter and paler; the original dynamic glow gets weaker; 3D models in menus look darker.</p>\n<ul><li><code>r_linearLighting 0</code> <em>(restart)</em> — 1 on. Needs <code>r_hdr 1</code>, <code>r_toneMap 1</code> and <code>r_overBrightBits 0</code>; otherwise it is ignored.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-tonemapping.md#linear-lighting-r_linearlighting-experimental\" target=\"_blank\" rel=\"noopener\">rend2-tonemapping.md</a>.</p>"
   }
  ],
  "media": []
 },
 "post-processing/distortion-color-fringes": {
  "group": "Image and camera",
  "title": "Distortion color fringes",
  "cvar": "r_refractionChromaticAberration",
  "sections": [
   {
    "title": "Distortion color fringes",
    "html": "<p>Screen-distortion effects (Force push, some weapon shots, refractive shaders) split red, green and blue slightly, like a cheap lens.</p>\n<ul><li><code>r_refractionChromaticAberration 0.05</code> — fringe width; 0 turns it off.</li></ul>"
   }
  ],
  "media": []
 },
 "materials/automatic-materials-from-stock-textures-auto-pbr": {
  "group": "Materials and surface detail",
  "title": "Automatic materials from stock textures (Auto PBR)",
  "cvar": "r_autoPBR",
  "sections": [
   {
    "title": "Automatic materials from stock textures (Auto PBR)",
    "html": "<p>Stock JKA textures are only color pictures: nothing tells the renderer whether a surface is metal, leather or cloth. Rend2 therefore lights them all as the same fully matte, non-metallic material. Auto PBR guesses the material from texture and model names — <code>metal</code>, <code>leather</code>, <code>cloth</code>, <code>helmet</code>, droids, stormtroopers, weapons and so on — and gives each kind its own glossiness and metalness. Armor then shines differently from a robe under the same lamp.</p>\n<p>Textures that already come with material maps (PBR texture packs) are never changed.</p>\n<ul><li><code>r_autoPBR 0</code> — <strong>0</strong> original Rend2: everything fully matte, non-metal. <strong>1</strong> everything slightly less matte, still non-metal. <strong>2</strong> guess the material per texture.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>When a material loads, its shader and texture names are split into words and matched against lists: material words (<code>steel</code>, <code>leather</code>, <code>robe</code>…), <code>models/weapons2</code> (metal), whole characters (droids → metal, troopers and vehicles → painted plastic, Wookiees → fur) and body parts (<code>head</code>, <code>hands</code> → skin; <code>torso</code>, <code>legs</code> → cloth). Each class has a fixed roughness and metalness. Nothing is computed per frame.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_autoPBRDebug 1</code> — color by guessed class: yellow metal, salmon skin, blue cloth, brown leather, green plastic, purple hair, grey generic, white authored.</li><li><code>r_autoPBRDebug 2</code> — where the values came from: authored map, automatic guess, converted legacy shine. Red means the surface is still drawn with the old vertex lighting (see <a href=\"#f/materials/shiny-stock-models-troopers-droids-weapons\">shiny models</a>).</li><li><code>r_pbrDumpMaterials [used|all|auto|authored|gouraud|&lt;class&gt;]</code> — list materials with their class and values.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-auto-pbr.md\" target=\"_blank\" rel=\"noopener\">rend2-auto-pbr.md</a>.</p>"
   }
  ],
  "media": []
 },
 "materials/shiny-stock-models-troopers-droids-weapons": {
  "group": "Materials and surface detail",
  "title": "Shiny stock models (troopers, droids, weapons)",
  "cvar": "r_autoPBRConvert",
  "sections": [
   {
    "title": "Shiny stock models (troopers, droids, weapons)",
    "html": "<p>In Rend2, about 130 stock shaders that use the original \"shiny\" effect are still drawn the old way, with lighting computed only at mesh vertices: every stormtrooper and droid, Boba Fett, most weapons, vehicles and some map objects. They get no normal maps, no sun shadows on themselves and no reflections, which is why they look flat next to everything else. This option converts them to the modern per-pixel path, and their old highlight mask decides which parts are glossy or metallic.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_autoPBRConvert 0</code> <em>(restart)</em> — 1 convert.</li></ul>\n<p>Combine it with <code>r_autoPBR 2</code> so the converted parts get the right material class.</p>\n<div class=\"how\"><h3>How it works</h3><p>The old shaders draw a lit color layer plus an additive \"specular\" layer masked by a <code>_spec</code> texture, and sometimes a fake chrome layer. The conversion removes those layers and turns the <code>_spec</code> mask into a per-pixel glossiness/metal map: bright areas of the mask become glossier and, on metal classes, metallic. With <code>r_cubeMapping 1</code>, the fake chrome layer is dropped because real reflections replace it.</p></div>"
   }
  ],
  "media": []
 },
 "materials/roughness-variation": {
  "group": "Materials and surface detail",
  "title": "Roughness variation",
  "cvar": "r_autoPBRRoughness",
  "sections": [
   {
    "title": "Roughness variation",
    "html": "<p>With Auto PBR, a whole texture gets one glossiness value, so a wall or armor plate shines evenly. This adds subtle dull and glossy patches derived from the texture's own detail. It does not turn bright pixels into metal.</p>\n<ul><li><code>r_autoPBRRoughness 0</code> — 1 on. Turning it on needs <code>vid_restart</code>; needs <code>r_autoPBR 1</code> or <code>2</code>.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>At load time the renderer measures local contrast in each color texture — creases, scratches, painted detail — and turns it into a glossiness map that varies about ±0.1 around the class value. Materials with authored maps or converted shine are left alone. This adds loading time, not frame time.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_autoPBRDebug 3</code> — final roughness in grey; blue means the surface doesn't use auto roughness.</li></ul>"
   }
  ],
  "media": []
 },
 "materials/glowing-surfaces": {
  "group": "Materials and surface detail",
  "title": "Glowing surfaces",
  "cvar": "r_autoEmissive",
  "sections": [
   {
    "title": "Glowing surfaces",
    "html": "<p>Stock lamps, screens, lava and control panels are drawn with \"glow\" layers that the newer features don't see as light. This option marks them as light-emitting surfaces, so HDR <a href=\"#f/post-processing/bloom\">bloom</a>, <a href=\"#f/lighting/bounced-light-ssgi\">bounced light</a> and <a href=\"#f/lighting/lamp-shaped-lights-ltc\">area lights</a> can use them.</p>\n<p class=\"cost\"><strong>Cost:</strong> light, on glowing surfaces only.</p>\n<ul><li><code>r_autoEmissive 0</code> — 1 on.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> give a stage its own glow map; black parts don't glow.</p>\n<pre><code>emissiveMap textures/example/panel_e\nemissiveColor 1 0.25 0.05\nemissiveScale 4</code></pre>\n<p><code>emissiveMap</code> is the glow image, <code>emissiveColor</code> tints it, <code>emissiveScale</code> sets brightness (or <code>emissiveNits</code> for real units with <code>r_pbrPhysical 1</code>).</p>\n<div class=\"how\"><h3>How it works</h3><p>Shader stages with the legacy <code>glow</code> keyword, and standalone unlit additive stages, are exported as emission instead of plain added color. Emission is a brightness of its own, added after lighting and shadows, and written to the bloom buffer.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_emissiveDebug 1..3</code> — where the emission comes from, its brightness in false color, and classification confidence.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-emissive-materials.md\" target=\"_blank\" rel=\"noopener\">rend2-emissive-materials.md</a>.</p>"
   }
  ],
  "media": []
 },
 "materials/stable-highlights": {
  "group": "Materials and surface detail",
  "title": "Stable highlights",
  "cvar": "r_specularAA",
  "sections": [
   {
    "title": "Stable highlights",
    "html": "<p>Fine normal-map detail on glossy surfaces makes highlights sparkle and crawl when you move. Specular anti-aliasing widens highlights that are too small for a pixel, so armor and polished floors stay calm. Energy compensation keeps rough metal from looking too dark. Both are on by default.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_specularAA 1</code> <em>(restart)</em> — 0 off.</li><li><code>r_brdfEnergyCompensation 1</code> — 0 off: rough metal gets darker.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The renderer estimates how much the surface normal varies inside one pixel (from screen derivatives and the normal map's mip levels) and raises the roughness of that pixel to match. The simple reflection model loses light on rough surfaces that would really bounce more than once inside the surface; energy compensation adds that light back.</p></div>\n<ul><li><code>r_specularAAStrength 0.15</code> — how much normal variation widens highlights.</li><li><code>r_specularAAMaxAdded 0.25</code> — limit on the added roughness.</li><li><code>r_pbrLegacyRoughness 1</code> — convert older glossiness conventions (Auto PBR, spec-gloss maps) to one roughness scale; 0 is a comparison mode.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_specularAADebug 1..6</code> — material roughness, material alpha, effective roughness, added roughness, normal variation, effective alpha.</li><li><code>r_pbrEnergyDebug 1..5</code> — diffuse, specular and total energy, lost energy, split legacy/compensated.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-specular-aa.md\" target=\"_blank\" rel=\"noopener\">rend2-specular-aa.md</a>.</p>"
   }
  ],
  "media": []
 },
 "materials/matte-surfaces": {
  "group": "Materials and surface detail",
  "title": "Matte surfaces",
  "cvar": "r_diffuseBRDF",
  "sections": [
   {
    "title": "Matte surfaces",
    "html": "<p>Changes how rough matte surfaces — cloth, plaster, stone — catch light. With the Burley model rough surfaces brighten a little toward grazing angles and smooth ones darken slightly at their edges, which looks less like plastic. The difference is subtle.</p>\n<ul><li><code>r_diffuseBRDF 0</code> — 0 Lambert (the classic model), 1 Burley/Disney.</li></ul>"
   }
  ],
  "media": []
 },
 "materials/physical-light-units": {
  "group": "Materials and surface detail",
  "title": "Physical light units",
  "cvar": "r_pbrPhysical",
  "sections": [
   {
    "title": "Physical light units",
    "html": "<p>Mostly for mod authors. Normally every light, the sun and glowing surfaces use the game's own unrelated brightness scales. This mode puts them on one real-world scale (lux, candela, nits), so authored lights and materials can be given real values. Stock content is converted to keep its look, but the image can change a little. Leave it at 0 if you only play stock content.</p>\n<ul><li><code>r_pbrPhysical 0</code> <em>(restart)</em> — 1 physical units.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Game distances are converted to metres and legacy light values to physical units around a reference brightness, then scaled to the renderer's internal range. These are calibration settings; they don't recover the real brightness of the lamps the original mappers imagined.</p></div>\n<ul><li><code>r_pbrWorldUnitsPerMeter 33.333333</code> <em>(restart)</em> — game units per metre, used for light falloff and emitter size.</li><li><code>r_pbrLegacyReference 1</code> <em>(restart)</em> — reference brightness for converting legacy lights.</li><li><code>r_pbrPhysicalToScene 0.0001</code> <em>(restart)</em> — physical units to internal brightness.</li><li><code>r_pbrSunIlluminance 0</code> — sun brightness in lux; 0 converts the map's own sun.</li><li><code>r_emissiveLegacyNits 1000</code> <em>(restart)</em> — nits labelled for a legacy <code>emissiveScale 1</code> in debug views.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_pbrInfo</code> — print the calibration.</li><li><code>r_pbrLight</code> — inspect or test calibrated lights; run without arguments for syntax.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-physical-pbr.md\" target=\"_blank\" rel=\"noopener\">rend2-physical-pbr.md</a>.</p>"
   }
  ],
  "media": []
 },
 "materials/relief-detail-pom": {
  "group": "Materials and surface detail",
  "title": "Relief detail (POM)",
  "cvar": "r_parallaxMapping",
  "sections": [
   {
    "title": "Relief detail (POM)",
    "html": "<p>Makes cracks, brick joints and panel grooves look recessed instead of painted on, and shift correctly as you move. It only works on textures that come with a height map (<code>_nh</code> images from texture packs, or the <code>heightMap</code> shader keyword). Stock JKA textures have none, so without such a pack nothing changes.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium on large surfaces close to the camera.</p>\n<ul><li><code>r_parallaxMapping 0</code> — 1 on.</li><li><code>r_pomAdaptiveSteps 0</code> — 1: spend fewer samples when looking straight at a surface and more at shallow angles, where steps would show. Usually better quality for the same cost.</li><li><code>r_pomFadeStart 0</code>, <code>r_pomFadeEnd 0</code> — distances where relief starts to flatten and where it switches off (plain normal mapping). 0 = never.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>For each pixel the shader walks a short ray into the texture's height map until it hits the surface, then draws the texture from that point. More steps remove stair-step artifacts at shallow angles.</p></div>\n<ul><li><code>r_pomMinSteps 8</code>, <code>r_pomMaxSteps 32</code> — ray steps when looking straight at a surface / along it (adaptive mode).</li><li><code>r_pomBinarySteps 8</code> — final refinement of the hit point.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_pomDebug 1..8</code> — height, displaced texture coordinates, ray depth, sun / local-light self-shadow, view steps, shadow steps, distance fade. Turning it on needs <code>vid_restart</code>.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-pom.md\" target=\"_blank\" rel=\"noopener\">rend2-pom.md</a>.</p>"
   }
  ],
  "media": []
 },
 "materials/shadows-inside-relief": {
  "group": "Materials and surface detail",
  "title": "Shadows inside relief",
  "cvar": "r_pomSelfShadow",
  "sections": [
   {
    "title": "Shadows inside relief",
    "html": "<p>Raised parts of a POM texture cast shadows into its grooves, so brick and carved stone show depth when a light moves across them. Needs <code>r_parallaxMapping 1</code> and height-mapped textures, as above.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium to heavy with many local lights.</p>\n<ul><li><code>r_pomSelfShadow 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_pomSelfShadowLightMode 1</code> — which lights cast relief shadows: <strong>0</strong> sun only, <strong>1</strong> sun + the strongest nearby light, <strong>2</strong> sun + the strongest few (<code>r_pomSelfShadowMaxLocalLights</code>), <strong>3</strong> all lights.</li><li><code>r_pomSelfShadowStrength 1</code> — 0 none … 1 full.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>After finding the visible point in the height map, the shader walks a second short ray toward each light and checks whether higher relief blocks it. Every extra light costs another ray.</p></div>\n<ul><li><code>r_pomSelfShadowMaxLocalLights 2</code> — lights for mode 2.</li><li><code>r_pomSelfShadowSteps 12</code> — samples per shadow ray.</li><li><code>r_pomSelfShadowSoftness 4</code> — edge hardness; higher is harder.</li><li><code>r_pomSelfShadowBias 0.02</code> — start offset of the shadow ray; raise it if grooves get speckled shadow.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_pomDebug 4</code>, <code>5</code> — sun / local-light self-shadow.</li><li><code>r_pomDebugFreezeLight 1</code> — freeze the sun direction for comparisons.</li></ul>"
   }
  ],
  "media": []
 },
 "materials/relief-silhouettes": {
  "group": "Materials and surface detail",
  "title": "Relief silhouettes",
  "cvar": "r_pomSilhouette",
  "sections": [
   {
    "title": "Relief silhouettes",
    "html": "<p>Ordinary POM only fakes depth inside a surface; its outline stays flat. This draws the relief as a thin shell, so rocks and broken panels look uneven at their edges too, and the shell casts sun shadows. Far away it falls back to ordinary POM. Needs <code>r_parallaxMapping 1</code> and height-mapped textures.</p>\n<p class=\"cost\"><strong>Cost:</strong> heavy on large surfaces close to the camera.</p>\n<ul><li><code>r_pomSilhouette 0</code> <em>(restart)</em> — 1 on, for materials that ask for it (<code>pomSilhouette</code> keyword).</li><li><code>r_autoPOMSilhouette 1</code> — console command: also use it on every material that has an ordinary POM height map. Run after <code>r_pomSilhouette 1</code> and a map load; saved in <code>r_autoPOMSilhouetteMode</code>.</li><li><code>r_pomSilhouetteDistance 512</code> — shells are drawn this close; farther surfaces use ordinary POM.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> add the shader keyword <code>pomSilhouette</code> and a stage with <code>normalHeightMap</code>, <code>parallaxDepth</code> and <code>parallaxBias</code>. Optional <code>pomSilhouetteDistance</code> and <code>pomSilhouetteSteps</code> override range and quality per shader. Use it for deep edge relief and keep ordinary POM for flat detail.</p>\n<div class=\"how\"><h3>How it works</h3><p>The renderer extrudes a shell above the mesh, traces the height map through it and discards empty parts. Unlike ordinary POM this can change the visible outline.</p></div>\n<ul><li><code>r_pomSilhouetteFade 96</code> — width of the crossfade to ordinary POM.</li><li><code>r_pomSilhouetteSteps 12</code>, <code>r_pomSilhouetteMaxSteps 48</code> — ray steps looking straight / along the surface.</li><li><code>r_pomSilhouetteViewDependence 1</code> — how fast steps grow toward shallow angles.</li><li><code>r_pomSilhouetteBinarySteps 6</code> — hit refinement.</li><li><code>r_pomSilhouetteShadows 1</code> — shells cast into the sun shadows.</li><li><code>r_pomSilhouetteContactShadows 0</code> — 1: apply contact shadows to shell pixels (tends to blacken grooves).</li><li><code>r_autoPOMSilhouette &lt;shader&gt; 0|1|default</code>, <code>r_autoPOMSilhouette clear</code> — per-shader override (a trailing <code>*</code> matches a prefix) and clearing them.</li><li><code>r_autoPOMSilhouetteMode 0</code> — stores the global choice of <code>r_autoPOMSilhouette</code>; set it with the command.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_pomSilhouetteDebug 1..11</code> — shell, wireframes, walls, discarded pixels, hit depth, step count, split POM / silhouette, depth, normal.</li><li><code>r_pomSilhouetteInfo</code> — shell counts, memory, fallbacks, last-frame statistics.</li><li><code>r_autoPOMSilhouette list</code> — candidates and why others were rejected; <code>r_autoPOMSilhouette &lt;shader&gt;</code> — one shader's state.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-silhouette-pom.md\" target=\"_blank\" rel=\"noopener\">rend2-silhouette-pom.md</a>.</p>"
   }
  ],
  "media": []
 },
 "lighting/ambient-occlusion-gtao": {
  "group": "Lighting",
  "title": "Ambient occlusion (GTAO)",
  "cvar": "r_aoMode",
  "sections": [
   {
    "title": "Ambient occlusion (GTAO)",
    "html": "<p>Darkens corners, wall joints, the ground under feet and crates, and small recesses, so objects look grounded instead of evenly lit. The map's baked lighting already has some of this, but it can't follow moving characters or props. GTAO is a more accurate replacement for Rend2's original SSAO, with fewer halos.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium.</p>\n<ul><li><code>r_aoMode -1</code> — <strong>-1</strong> follow the original <code>r_ssao</code> setting (off unless <code>r_ssao 1</code>). <strong>0</strong> off. <strong>1</strong> original SSAO. <strong>2</strong> GTAO. Turning it on from off needs <code>vid_restart</code>.</li><li><code>r_gtaoQuality 2</code> — 0 low, 1 medium, 2 high, 3 ultra.</li><li><code>r_gtaoRadius 32</code> — how far an object reaches to darken its surroundings, in world units (a player is about 64 tall).</li><li><code>r_gtaoPower 1.5</code> — strength; higher is darker.</li></ul>\n<p><code>r_aoCompare 1</code> shows original SSAO on the left and GTAO on the right.</p>\n<div class=\"how\"><h3>How it works</h3><p>For each pixel the shader looks around in the depth image to estimate how much of the surrounding sky of light is blocked by nearby geometry. It only knows what is on screen, so occlusion from objects just outside the view is missing. GTAO darkens only indirect light (ambient light, the indirect share of baked light and reflections), not direct sun or lamps, as in reality.</p></div>\n<ul><li><code>r_gtaoHalfRes 1</code> <em>(restart)</em> — compute at half resolution; 0 full resolution, sharper and slower.</li><li><code>r_gtaoFalloff 0.6</code> — how quickly occlusion fades toward the radius.</li><li><code>r_gtaoThickness 0.2</code> — treat objects as thin, so things in front of a wall don't darken it as much (fewer dark halos around characters).</li><li><code>r_gtaoDenoise 2</code> — smoothing passes, 0–3.</li><li><code>r_gtaoBentNormals 1</code> — also tilt ambient light toward the open direction; 0 off, up to 1 full.</li><li><code>r_aoApply -1</code> — <strong>-1</strong> automatic. <strong>0</strong> original: darken ambient light and reflections. <strong>1</strong> physically based: darken only indirect light.</li><li><code>r_aoMultiBounce 1</code> — bright surfaces keep more light in creases, as light bounces inside them.</li><li><code>r_aoLightmapFraction 0.5</code> — share of baked lighting treated as indirect and therefore darkened by AO.</li><li><code>r_aoSpecOcclusion -1</code> — how AO darkens reflections: <strong>-1</strong> automatic, <strong>0</strong> plain AO, <strong>1</strong> from AO, roughness and view angle, <strong>2</strong> from the bent-normal cone.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_aoDebug 1..12</code> — original SSAO, raw / denoised GTAO, normals, depth, contact shadows, cascade shadows, sun visibility, ambient visibility, final AO, bent normals, reflection occlusion.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-ambient-occlusion.md\" target=\"_blank\" rel=\"noopener\">rend2-ambient-occlusion.md</a>.</p>"
   }
  ],
  "media": []
 },
 "lighting/many-dynamic-lights-forward": {
  "group": "Lighting",
  "title": "Many dynamic lights (Forward+)",
  "cvar": "r_forwardPlus",
  "sections": [
   {
    "title": "Many dynamic lights (Forward+)",
    "html": "<p>Rend2 allows at most 32 dynamic lights per frame — sabers, blaster bolts, explosions, muzzle flashes. In big fights the extra lights are simply dropped. Forward+ raises the limit to 256 and makes every pixel check only the lights that actually reach it. <a href=\"#f/lighting/lamp-shaped-lights-ltc\">Lamp-shaped lights</a> and <a href=\"#f/lighting/saber-light-along-the-blade\">blade-shaped saber light</a> need it.</p>\n<p class=\"cost\"><strong>Cost:</strong> light; it can be faster than the original in busy scenes.</p>\n<ul><li><code>r_forwardPlus 0</code> — 1 on.</li><li><code>r_forwardPlusMaxShadowLights 4</code> — with dynamic-light shadows (<code>r_dlightMode 2</code>, see <a href=\"#f/shadows/dynamic-light-shadows\">Shadows</a>), how many of the strongest lights cast them.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The view is split into screen tiles and depth slices (clusters). Each frame every light is added to the clusters it touches, and each pixel loops only over its cluster's list.</p></div>\n<ul><li><code>r_forwardPlusTileSize 64</code> — tile size in pixels.</li><li><code>r_forwardPlusSlices 16</code> — depth slices.</li><li><code>r_forwardPlusNearSlice 48</code> — depth covered by the first slice.</li><li><code>r_forwardPlusMaxLightsPerCluster 64</code> — lights kept per cluster; the least important ones are dropped.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_forwardPlusDebug 1..9</code> <em>(restart)</em> — tiles, slices, clusters, lights per cluster, overflow, shadowed and unshadowed lights, light spheres, one light (<code>r_forwardPlusDebugLight &lt;index&gt;</code>).</li><li><code>r_forwardPlusStats</code> — print cluster statistics.</li><li><code>r_forwardPlusBenchmark [frames]</code> — time the lighting path.</li><li><code>r_forwardPlusSpawnTestLights &lt;count&gt; [spread]</code> — spawn test lights.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-forward-plus.md\" target=\"_blank\" rel=\"noopener\">rend2-forward-plus.md</a>.</p>"
   }
  ],
  "media": []
 },
 "lighting/bounced-light-ssgi": {
  "group": "Lighting",
  "title": "Bounced light (SSGI)",
  "cvar": "r_ssgi",
  "sections": [
   {
    "title": "Bounced light (SSGI)",
    "html": "<p>A saber or explosion near a wall lights the wall — and with this, the wall also throws a weaker, colored glow onto the floor and characters next to it. Glowing materials can light their surroundings the same way. The map's own baked lighting already contains bounced light, so by default only dynamic lights and glowing surfaces bounce. Works best with <code>r_hdr 1</code>; without it bright sources are clipped.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium to heavy.</p>\n<ul><li><code>r_ssgi 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_ssgiIntensity 1</code> — strength of the bounce.</li><li><code>r_ssgiQuality 1</code> — 0 low, 1 medium, 2 high, 3 ultra.</li><li><code>r_ssgiSource 0</code> — what bounces: <strong>0</strong> dynamic lights and glowing materials, <strong>1</strong> glowing materials only, <strong>2</strong> everything, including baked lighting (experimental: baked bounce is counted twice).</li></ul>\n<p><code>r_ssgiCompare 1</code> shows the screen without SSGI on the left and with it on the right.</p>\n<div class=\"how\"><h3>How it works</h3><p>For each pixel the shader traces a few short rays through the on-screen depth image and picks up the light of the surfaces they hit. It only sees what is on screen, and the noisy result is averaged over several frames and smoothed.</p></div>\n<ul><li><code>r_ssgiMaxDistance 256</code> — how far light bounces, in world units.</li><li><code>r_ssgiEmissiveScale 1</code> — strength of materials with their own glow maps as sources.</li><li><code>r_ssgiGlowScale 0</code> — strength of stock glow layers (and <a href=\"#f/materials/glowing-surfaces\">auto-emissive</a>) as sources; 0 ignores them.</li><li><code>r_ssgiRays 0</code>, <code>r_ssgiSteps 0</code> — rays per pixel and steps per ray; 0 takes them from the quality.</li><li><code>r_ssgiThickness 12</code> — assumed thickness of surfaces on screen.</li><li><code>r_ssgiTemporal 1</code>, <code>r_ssgiHistoryWeight 0.9</code> — averaging over frames and how much history to keep.</li><li><code>r_ssgiDenoise -1</code> — smoothing passes 0–4; -1 from the quality.</li><li><code>r_ssgiHalfRes -1</code> — trace at half resolution: -1 from the quality, 0 full, 1 half (applied at <code>vid_restart</code>).</li><li><code>r_ssgiHiZ -1</code> — faster hierarchical tracing: -1 from the quality, 0 off, 1 on.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_ssgiDebug 1..10</code> — hit mask, hit distance, raw / averaged / smoothed GI, history weight, dynamic and glow sources, final light, receiver color.</li><li><code>r_ssgiFreezeHistory 1</code> — stop updating the frame history.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-ssgi.md\" target=\"_blank\" rel=\"noopener\">rend2-ssgi.md</a>.</p>"
   }
  ],
  "media": []
 },
 "lighting/lamp-shaped-lights-ltc": {
  "group": "Lighting",
  "title": "Lamp-shaped lights (LTC)",
  "cvar": "r_ltcAreaLights",
  "sections": [
   {
    "title": "Lamp-shaped lights (LTC)",
    "html": "<p>A dynamic light in JKA is a point. Real lamps have size and shape: a long ceiling strip should leave a long highlight on a glossy floor, not a round spot. Area lights give lights a rectangle or line shape. On stock maps, glowing lamp surfaces are detected automatically and turned into rectangle lights that add only highlights, since the lightmap already has their light. Needs <code>r_forwardPlus 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium where many shaped lights overlap.</p>\n<ul><li><code>r_ltcAreaLights 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_ltcAutoAreaLights 1</code> — find lamps on maps without a light file: <strong>0</strong> off, <strong>1</strong> clear lamp shapes, <strong>2</strong> also doubtful ones (more lamps, more mistakes). Applied at map load.</li><li><code>r_ltcIntensityScale 1</code> — brightness of map area lights.</li><li><code>r_ltcStaticShadows 0</code> — shadows of map area lights: <strong>0</strong> off, <strong>1</strong> world shadows (cached), <strong>2</strong> also characters near up to four lights.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> a map's own <code>maps/&lt;map&gt;.arealights.json</code> replaces auto-detection. A warm rectangle lamp:</p>\n<pre><code>{&quot;lights&quot;:[{&quot;type&quot;:&quot;rect&quot;,&quot;center&quot;:[0,0,128],&quot;right&quot;:[1,0,0],&quot;up&quot;:[0,1,0],\n&quot;halfWidth&quot;:64,&quot;halfHeight&quot;:8,&quot;color&quot;:[1,0.85,0.7],&quot;intensity&quot;:4,&quot;mode&quot;:&quot;static_specular&quot;}]}</code></pre>\n<p>A line light uses <code>start</code>, <code>end</code> and <code>radius</code>. <code>static_specular</code> adds only highlights over the baked light; <code>static_full</code> and <code>dynamic</code> also light surfaces. <code>r_ltcExtractLights</code> writes the auto-detected lamps to <code>maps/&lt;map&gt;.arealights.generated.json</code> as a starting point.</p>\n<div class=\"how\"><h3>How it works</h3><p>LTC (linearly transformed cosines) computes the light from a whole polygon exactly in one step, instead of summing many point lights, so there is no quality setting.</p></div>\n<ul><li><code>r_ltcMaxLights 64</code> — map lights used per scene, most important first.</li><li><code>r_ltcStaticDiffuse 0</code> — 1: auto-detected and <code>static_specular</code> lamps also light surfaces, although the lightmap usually contains that light already.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_ltcDebug 1..14</code> <em>(restart)</em> — specular/diffuse, light source, cluster counts, bounds, outlines, axes, strongest light, shadow and saber views. <code>r_ltcDebugLight &lt;id&gt;</code> picks the highlighted light; -1 the nearest.</li><li><code>r_ltcList</code>, <code>r_ltcNearest</code> — list loaded lights / print the nearest.</li><li><code>r_ltcReloadLights</code> — reload the light file or rerun detection.</li><li><code>r_ltcShadowStats</code> — shadow selection and cache statistics.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-ltc-area-lights.md\" target=\"_blank\" rel=\"noopener\">rend2-ltc-area-lights.md</a>.</p>"
   }
  ],
  "media": []
 },
 "lighting/saber-light-along-the-blade": {
  "group": "Lighting",
  "title": "Saber light along the blade",
  "cvar": "r_ltcSaberAreaLights",
  "sections": [
   {
    "title": "Saber light along the blade",
    "html": "<p>The saber light normally comes from one point at the middle of the blade, so floors and walls get a round spot. With this, the whole blade lights its surroundings: a long, even glow along the floor and long reflections in armor. Works with stock sabers. Needs <code>r_forwardPlus 1</code> and <code>r_ltcAreaLights 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium near lit surfaces.</p>\n<ul><li><code>r_ltcSaberAreaLights 0</code> — <strong>0</strong> point light (original). <strong>1</strong> blade-shaped light on surfaces and in fog. <strong>2</strong> blade-shaped light on surfaces, point light with shadow in fog.</li><li><code>r_ltcSaberShadows -1</code> — saber shadows: <strong>-1</strong> from a point at the blade middle (original), <strong>0</strong> off, <strong>1</strong> stretched along the blade, <strong>2</strong> plus screen-space blade visibility, <strong>3</strong> precise three-point shadows for up to two sabers (slow).</li><li><code>r_ltcSaberIntensityScale 1</code> — brightness.</li><li><code>r_ltcSaberLuminance 40000</code> — blade brightness in nits with <code>r_pbrPhysical 1</code>.</li></ul>"
   }
  ],
  "media": []
 },
 "lighting/spot-lights-mod-authors": {
  "group": "Lighting",
  "title": "Spot lights (mod authors)",
  "cvar": "r_spotLights",
  "sections": [
   {
    "title": "Spot lights (mod authors)",
    "html": "<p>Flashlight-like cones instead of lights that shine in every direction. Stock effects have no cones; a mod adds them in EFX files. Cones can cast shadows (<code>r_dlightMode 2</code>) and show a beam in <a href=\"#topic-fog\">volumetric fog</a>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light; shadows and fog beams medium.</p>\n<ul><li><code>r_spotLights 1</code> — 0 draws authored spots as ordinary point lights.</li><li><code>r_spotLightShadows 1</code> — 0 no spot shadows.</li></ul>\n<p>Add a <code>spot</code> block inside an EFX <code>Light</code>; its size and color still set range and color:</p>\n<pre><code>spot\n{\n    innerAngle 16\n    outerAngle 26\n    direction 1 0 0\n    shadows 1\n}</code></pre>\n<h4>Debug</h4>\n<ul><li><code>r_spotLightDebug 1..4</code> — cones, shadow frustums, surfaces lit by spots only, fog lit by spots only.</li><li><code>r_spot add|attach|spin|noshadow|list|clear</code> — place test spots.</li><li><code>fxplay &lt;efx&gt; [distance|muzzle]</code> — play an EFX for testing (developer).</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-spot-lights.md\" target=\"_blank\" rel=\"noopener\">rend2-spot-lights.md</a>.</p>"
   }
  ],
  "media": []
 },
 "lighting/projected-patterns-mod-authors": {
  "group": "Lighting",
  "title": "Projected patterns (mod authors)",
  "cvar": "r_spotLightCookies",
  "sections": [
   {
    "title": "Projected patterns (mod authors)",
    "html": "<p>A spot light can project an image — a grate, a window frame, colored glass — onto walls and through fog. Needs an authored spot.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_spotLightCookies 1</code> — <strong>0</strong> off, <strong>1</strong> pattern brightness only, <strong>2</strong> in color.</li></ul>\n<p>Inside the EFX <code>spot</code> block add <code>cookie textures/imperial/grate02</code>; <code>cookieRoll &lt;degrees&gt;</code> rotates it.</p>\n<h4>Debug</h4>\n<ul><li><code>r_spotLightCookieDebug 1..3</code> — projected coordinates, pattern, pattern × shadow.</li><li><code>r_spot cookie &lt;index&gt; &lt;image|none&gt;</code> — put a pattern on a test spot.</li></ul>"
   }
  ],
  "media": []
 },
 "shadows/sun-shadows": {
  "group": "Shadows",
  "title": "Sun shadows",
  "cvar": "r_forceSun",
  "sections": [
   {
    "title": "Sun shadows",
    "html": "<p>Characters, props and the world cast realtime sun shadows that move with them. Shadows are sharp where an object touches the ground and soften with distance, like real sunlight, and they no longer crawl and flicker when the camera moves.</p>\n<p class=\"req\"><strong>Stock maps don't get realtime sun shadows by default.</strong> Rend2 only draws them on maps whose sky shader has the Rend2 keyword <code>q3gl2_sun</code>, and stock JKA maps don't. <code>r_forceSun 1</code> turns them on with the map's own sun direction. Many other features use the sun too — character sun lighting, sun light in fog, atmosphere, clouds — and need the same.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; wide, high-quality soft shadows can be heavy.</p>\n<ul><li><code>r_forceSun 0</code> — 1 realtime sun on maps without <code>q3gl2_sun</code>. (2 is a test mode: the sun circles the sky.)</li><li><code>r_forceSunAmbientScale 0.5</code> — with <code>r_forceSun</code>: brightness left in shadow; 0 black shadows, 1 no shadows.</li><li><code>r_sunShadowMode 1</code> <em>(restart)</em> — <strong>0</strong> Rend2's original shadows, <strong>1</strong> the improved ones described here.</li><li><code>r_shadowPCSS 1</code> — soft shadows that harden near contact; 0 uniformly soft edges.</li><li><code>r_shadowPCSSQuality 1</code> — 0 low, 1 high, 2 ultra.</li><li><code>r_sunShadowAlphaCasters 1</code> <em>(restart)</em> — leaves, grates, grass and other see-through textures cast shadows with holes, not solid blocks.</li><li><code>r_sunShadowCache 0</code> <em>(restart)</em> — reuse static world shadows instead of redrawing them every frame: <strong>0</strong> off, <strong>1</strong> exact reuse (safe), <strong>2</strong> scrolling cache, <strong>3</strong> scrolling cache with saved pages. 2 and 3 are experimental.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The sun's view is rendered into several shadow maps (cascades), each covering a larger area farther from the camera. Their position is snapped so the shadow texels don't swim, and neighbouring cascades are blended. PCSS (percentage-closer soft shadows) finds how far each shadowed point is from its blocker and widens the blur accordingly.</p></div>\n<ul><li><code>r_shadowSunAngularDiameter 0.53</code> — apparent size of the sun in degrees; larger gives softer shadows.</li><li><code>r_shadowPCSSMaxPenumbra 32</code> — upper limit of the softness, in world units.</li><li><code>r_shadowCascadeBlend 0.10</code> — width of the blend between cascades.</li><li><code>r_forceSunLightScale 1.0</code>, <code>r_forceSunMapLightScale 1.0</code> — with <code>r_forceSun</code>: sun and lightmap brightness.</li></ul>\n<p>Bias settings fix two opposite artifacts: dark speckles on lit surfaces (\"acne\") when too low, and shadows detached from their objects (\"peter-panning\") when too high.</p>\n<ul><li><code>r_shadowDepthBias 0.15</code> — constant offset, in world units.</li><li><code>r_shadowNormalBias 0.75</code> — offset along the surface normal, in shadow-map texels.</li><li><code>r_shadowSlopeBias 1.0</code> — extra offset on surfaces at a steep angle to the sun.</li><li><code>r_shadowReceiverBiasClamp 4.0</code> — limit on that slope correction.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_shadowDebug 1..13</code> — cascades, depth, filtering, blockers, penumbra, PCSS, contact, final, bias, point shadows, character receivers, model sun split, character layers.</li><li><code>r_sunShadowVerify 1</code> — compare cached sun shadows with a full redraw on the next frame; 2 every frame (slow).</li><li><code>r_sunShadowBenchmark &lt;frames&gt;</code> — time that many frames without debug readbacks.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-shadows2.md\" target=\"_blank\" rel=\"noopener\">rend2-shadows2.md</a>, <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-sun-shadow-cache-validation.md\" target=\"_blank\" rel=\"noopener\">cache validation</a>.</p>"
   }
  ],
  "media": []
 },
 "shadows/character-sun-shadows": {
  "group": "Shadows",
  "title": "Character sun shadows",
  "cvar": "r_sunShadowSplit",
  "sections": [
   {
    "title": "Character sun shadows",
    "html": "<p>A soft shadow that suits a building makes a character's shadow look blurry and detached, and broad scenery shadows wash out a character's own contact shadow. By default characters and other models are drawn into a separate sharp shadow layer and combined with the softer scenery shadows.</p>\n<ul><li><code>r_sunShadowSplit 1</code> <em>(restart)</em> — 1 separate sharp layer for models (characters, NPCs, MD3 props); 0 one shared shadow.</li><li><code>r_shadowPCSSCharacterLayers 1</code> <em>(restart)</em> — older variant: harder edges where character shadows meet scenery. Used only with <code>r_sunShadowSplit 0</code>.</li><li><code>r_shadowCasterLod 0</code> — 1: characters cast shadows with the same detail level you see. By default the shadow uses the detail level for its own distance, which can make it look simpler than the model.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_shadowDebug 11</code>, <code>12</code>, <code>13</code> — character receivers and casters, model sun split, character layers.</li><li><code>r_shadowCasterStats 1</code> — print character shadow casters and their detail levels once per second.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-character-shadows.md\" target=\"_blank\" rel=\"noopener\">rend2-character-shadows.md</a>.</p>"
   }
  ],
  "media": []
 },
 "shadows/contact-shadows": {
  "group": "Shadows",
  "title": "Contact shadows",
  "cvar": "r_contactShadows",
  "sections": [
   {
    "title": "Contact shadows",
    "html": "<p>Adds small sun shadows where the shadow maps are too coarse: under feet, behind small props, in carved detail. Needs realtime sun (see above).</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium.</p>\n<ul><li><code>r_contactShadows 0</code> — 1 on. Turning it on from off needs <code>vid_restart</code>.</li><li><code>r_contactShadowLength 16</code> — how far a contact shadow reaches, in world units.</li><li><code>r_contactShadowStrength 0.85</code> — darkness.</li><li><code>r_contactShadowSoft 0</code> — 1 softer edges.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>For each pixel the shader steps a short way toward the sun through the on-screen depth image and checks whether something is in front. It can't see objects outside the screen.</p></div>\n<ul><li><code>r_contactShadowSteps 12</code> — steps per ray.</li><li><code>r_contactShadowThickness 6</code> — assumed thickness of what blocks the ray.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_aoDebug 6</code> — contact shadows only.</li></ul>"
   }
  ],
  "media": []
 },
 "shadows/dynamic-light-shadows": {
  "group": "Shadows",
  "title": "Dynamic light shadows",
  "cvar": "r_dlightMode",
  "sections": [
   {
    "title": "Dynamic light shadows",
    "html": "<p>Sabers, explosions and other dynamic lights can cast realtime shadows. This is an original Rend2 setting; several features here build on it.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium to heavy, depending on how many lights cast shadows.</p>\n<ul><li><code>r_dlightMode 1</code> <em>(restart)</em> — 2 dynamic lights cast shadows. With <a href=\"#f/lighting/many-dynamic-lights-forward\">Forward+</a>, <code>r_forwardPlusMaxShadowLights</code> limits how many.</li><li><code>r_dlightShadowBias 0</code> — 1 a more consistent offset, with less shadow acne and fewer detached shadows.</li></ul>\n<p>These keep the same shadows but redraw less. All are on by default:</p>\n<ul><li><code>r_shadowCache 1</code> — reuse a light's shadow while nothing in it changes.</li><li><code>r_shadowStaticCache 1</code> — keep the static world part and redraw only moving objects over it. Needs OpenGL 4.3 or <code>ARB_copy_image</code>.</li><li><code>r_shadowSinglePass 1</code> — walk the scene once per light instead of once per cube face.</li><li><code>r_shadowLayered 1</code> — draw all six cube faces in one call where supported.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Each shadowed light renders the scene into a cube of six depth images. The caches above skip redrawing parts that haven't changed.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_shadowDebug 10</code> — point-light shadow visibility.</li><li><code>r_shadowStaticCache 2</code> — compare one frame with a full redraw (slow).</li><li><code>r_shadowVerify 1</code> — draw every cube both ways on the next frame and print differences.</li><li><code>r_shadowStats 1</code> — CPU cost of shadow and camera views, once per second.</li></ul>"
   }
  ],
  "media": []
 },
 "characters/light-across-the-body": {
  "group": "Characters and animation",
  "title": "Light across the body",
  "cvar": "r_entityLightGrid",
  "sections": [
   {
    "title": "Light across the body",
    "html": "<p>Characters aren't lit by the map's lightmaps. Instead the map stores a 3D grid of light samples (the light grid), each with an ambient color, a main light color and that light's direction. The original game picks <strong>one</strong> sample at the character's position and lights the whole body with it, so a character walking through a doorway changes brightness all at once, and an arm in a beam of light is no brighter than the rest.</p>\n<p class=\"cost\"><strong>Cost:</strong> light for mode 1, medium for mode 2.</p>\n<ul><li><code>r_entityLightGrid 0</code> <em>(restart)</em> — <strong>0</strong> one sample per character (original). <strong>1</strong> three samples at feet, waist and head height, blended up the body: a character standing in a shaft of light gets a lit head and dark legs. <strong>2</strong> every pixel takes its own smoothly interpolated grid sample: light changes across the body and along outstretched limbs.</li><li><code>r_entityLightProbes 1</code> <em>(restart)</em> — mode 2 only. The grid stores one main light direction plus flat ambient; this turns each sample into a smooth directional light, so the lit side wraps naturally around the body and the shadowed side isn't uniformly flat.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_entityLightGridDebug 1..11</code> <em>(restart)</em> — ambient, main light, direction, validity, grid cell, the three modes, the difference between them, probe brightness and direction.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-grid-reconstruction-removal.md\" target=\"_blank\" rel=\"noopener\">rend2-grid-reconstruction-removal.md</a>.</p>"
   }
  ],
  "media": []
 },
 "characters/sun-on-characters": {
  "group": "Characters and animation",
  "title": "Sun on characters",
  "cvar": "r_entitySunSplit",
  "sections": [
   {
    "title": "Sun on characters",
    "html": "<p class=\"req\">Needs realtime sun shadows (<a href=\"#f/shadows/sun-shadows\"><code>r_forceSun 1</code></a> on stock maps).</p>\n<p>The light grid merges the sun and every lamp into one main light whose direction is an average, rarely where the sun really is. When a character steps into a sun shadow, Rend2 just darkens that merged light, so the shading still comes from the wrong side and doesn't match the shadow on the ground.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_entitySunSplit 0</code> <em>(restart)</em> — 1: take the sun out of the grid light and light models from the real sun direction, with per-pixel sun shadows. Only with <code>r_entityLightGrid 0</code>.</li><li><code>r_entityShadowAmbient 0</code> — in sun shadow the world loses its ambient light too, but characters keep theirs and look too bright. 0 darkens only the main light (original), 1 also the whole ambient, like the world, values in between partly.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_shadowDebug 12</code> — model sun split.</li></ul>"
   }
  ],
  "media": []
 },
 "characters/ambient-light-from-the-surroundings": {
  "group": "Characters and animation",
  "title": "Ambient light from the surroundings",
  "cvar": "r_diffuseIBL",
  "sections": [
   {
    "title": "Ambient light from the surroundings",
    "html": "<p>A character's ambient light from the grid is one color from every side. With this, the side facing a bright wall or an open door gets more light and the side facing a dark corridor less. Uses the map's cubemaps, which are built at map load.</p>\n<p class=\"cost\"><strong>Cost:</strong> light; longer map loading.</p>\n<ul><li><code>r_diffuseIBL 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_diffuseIBLStrength 1</code> — how strongly the direction modulates the ambient.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>For each cubemap the renderer computes how much light arrives from each direction. Models use that to vary the original grid ambient around the body; it modulates, rather than replaces, the map's lighting.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_diffuseIBLDebug 1..5</code> — incoming light, modulation, old and new ambient, selected cubemap.</li></ul>"
   }
  ],
  "media": []
 },
 "characters/skin-subsurface-scattering": {
  "group": "Characters and animation",
  "title": "Skin (subsurface scattering)",
  "cvar": "r_skinSSS",
  "sections": [
   {
    "title": "Skin (subsurface scattering)",
    "html": "<p>In real life light enters skin, scatters under it and comes out a few millimetres away, often reddened by blood. That's why faces have soft light-to-shadow transitions, and why ears and fingers glow red against a bright light. Without it skin looks like painted plastic. This works on existing characters: faces and hands of organic characters are recognized automatically. Droids, armor and fur are left alone.</p>\n<p class=\"cost\"><strong>Cost:</strong> light for mode 1, medium for mode 2.</p>\n<ul><li><code>r_skinSSS 0</code> <em>(restart)</em> — <strong>0</strong> off. <strong>1</strong> cheap soft falloff: light wraps a little further around the face, reds more than blues. An approximation, not real scattering. <strong>2</strong> real screen-space scattering: the skin's lighting is blurred over a few millimetres around each pixel, so pores and hard shadow edges soften while highlights stay sharp. Needs <code>r_hdr 1</code>.</li><li><code>r_skinSSSStrength 1</code> — mode 2: how much of the skin lighting is replaced by the scattered version.</li><li><code>r_skinSSSWidth 8</code> — mode 2: scattering distance in millimetres; 8 matches real skin.</li><li><code>r_skinSSSTransmission 0</code> — light shining through thin parts (ears, fingers) from behind; 0 off.</li><li><code>r_skinSSSMixedHeads 0</code> <em>(restart)</em> — 1: also scatter <code>*_head</code> textures, which mix face, hair and ears in one image. Eyes and hair may then blur.</li></ul>\n<p><code>r_skinSSSCompare 1</code> shows the left half without and the right half with skin scattering.</p>\n<p class=\"mod\"><strong>Mod authors:</strong> <code>skinScatter &lt;0..1&gt;</code> sets a material's scattering strength (0 turns it off); <code>skinMask &lt;image&gt;</code> limits it to skin on a mixed texture (red channel).</p>\n<div class=\"how\"><h3>How it works</h3><p>Mode 2 renders the scene, then blurs only the diffuse light of skin pixels with a skin-shaped profile, in two passes, stopping at depth and normal edges so the blur doesn't leak onto the background. Mode 1 just changes the shading falloff per color channel.</p></div>\n<ul><li><code>r_skinSSSQuality 1</code> — blur taps per pass: 0 = 11, 1 = 17, 2 = 25.</li><li><code>r_skinSSSFollowSurface 1</code> — how strongly depth and normal edges stop the blur.</li><li><code>r_skinSSSWrap 0.3</code> — mode 1: how far light wraps.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_skinSSSDebug 1..8</code> — classification, skin mask, raw skin lighting, blur passes, final change, everything but skin, blur radius.</li><li><code>r_skinSSSList [used|all|skin]</code> — which materials scatter and why.</li><li><code>r_skinSSSKernel</code> — print the blur profile.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-skin-sss.md\" target=\"_blank\" rel=\"noopener\">rend2-skin-sss.md</a>.</p>"
   }
  ],
  "media": []
 },
 "characters/hair-and-eyes": {
  "group": "Characters and animation",
  "title": "Hair and eyes",
  "cvar": "",
  "sections": [
   {
    "title": "Hair and eyes",
    "html": "<p>Hair gets a stretched highlight shaped by the strand direction, like real hair, and eyes a smooth wet shine. There is no switch: it is always on where a material supports it. On stock models that is only the few hair surfaces named exactly <code>hair</code>/<code>hairs</code> (some Jedi, Ugnaught and Weequay heads). Most stock hair is painted into the head texture, and eyes share textures with the face, so they need modded assets.</p>\n<p class=\"cost\"><strong>Cost:</strong> light, on these materials only.</p>\n<p class=\"mod\"><strong>Mod authors:</strong> declare <code>materialType hair</code> or <code>materialType eye</code> inside a lit stage; <code>materialType standard</code> opts out. <code>materialMap</code> holds hair direction in RG and coverage in B. On a mixed skin/eye texture use the same image as <code>skinMask</code>.</p>\n<div class=\"how\"><h3>How it works</h3><p>Hair reflects light differently along and across the strand direction, which stretches the highlight. Eyes use a smooth, non-metallic surface — a wet highlight, not a simulated eyeball.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_materialModelDebug 1..7</code> — type, tangent, bitangent, hair direction, hair highlight, eye highlight, mask.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-hair-eye-materials.md\" target=\"_blank\" rel=\"noopener\">rend2-hair-eye-materials.md</a>.</p>"
   }
  ],
  "media": []
 },
 "characters/softer-ragdolls": {
  "group": "Characters and animation",
  "title": "Softer ragdolls",
  "cvar": "r_ragSoftLimits",
  "sections": [
   {
    "title": "Softer ragdolls",
    "html": "<p>When a character dies, the ragdoll swings its limbs until each joint hits its hard limit and stops dead, so bodies look stiff and snap. With soft limits a limb slows down as it nears the end of its range and springs back a little. Works in SP and MP.</p>\n<p class=\"cost\"><strong>Cost:</strong> light (CPU), only while ragdolls are active.</p>\n<ul><li><code>r_ragSoftLimits 0</code> — 1 on.</li><li><code>r_ragSoftLimitZone 0.18</code> — how much of the end of each joint range is springy.</li><li><code>r_ragSoftLimitStiffness 0.25</code> — spring strength.</li><li><code>r_ragNeutralPull 0.0002</code> — weak pull toward the joint's middle position.</li></ul>\n<p>The original hard limit stays as the final stop.</p>"
   }
  ],
  "media": []
 },
 "characters/softer-animation-sp": {
  "group": "Characters and animation",
  "title": "Softer animation (SP)",
  "cvar": "r_animSoftLimits",
  "sections": [
   {
    "title": "Softer animation (SP)",
    "html": "<p>Animated limbs ease in and rebound slightly when they reach the end of their usual movement, instead of stopping abruptly. Works with the existing animations; single player only.</p>\n<p class=\"cost\"><strong>Cost:</strong> light (CPU), plus a one-time scan of the animation files.</p>\n<ul><li><code>r_animSoftLimits 0</code> — <strong>0</strong> off, <strong>1</strong> all characters, <strong>2</strong> everyone except the player.</li><li><code>r_animSoftLimitBounce 0.35</code> — how much a stopped motion bounces back.</li><li><code>r_animSoftLimitZone 0.18</code> — width of the soft zone at each end of the range.</li><li><code>r_animSoftLimitMargin 0.1</code> — extra range allowed beyond the measured motion; applied when the animations are scanned.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The renderer scans each animation file once to measure how far every joint moves, then eases motion near those limits. Only the displayed pose changes; gameplay and animation choice don't.</p></div>\n<h4>Debug</h4>\n<ul><li><code>developer 1</code> — print the range-scan results.</li></ul>"
   }
  ],
  "media": []
 },
 "reflections/screen-space-reflections-ssr": {
  "group": "Reflections",
  "title": "Screen-space reflections (SSR)",
  "cvar": "r_ssr",
  "sections": [
   {
    "title": "Screen-space reflections (SSR)",
    "html": "<p>Rend2's reflections come from cubemaps: 360° snapshots taken at a few points of the map when it loads (only with <code>r_cubeMapping 1</code>, which is off by default). They never show characters, sabers or anything that moves, and they are often misplaced. SSR reflects what is actually on screen: a polished floor mirrors the duelists standing on it, and a lit corridor shows in a wet wall. What SSR can't see — things off screen or behind other objects — falls back to the cubemap, or to no reflection without cubemaps. Works best with <code>r_hdr 1</code>; without it bright reflections are clipped.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; high quality and rough-surface rays can be heavy.</p>\n<ul><li><code>r_ssr 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_ssrQuality 1</code> — 0 low, 1 medium, 2 high, 3 ultra.</li><li><code>r_ssrTemporal 0</code> <em>(restart)</em> — 1: average reflections over frames. Smoother and less noisy, with slight ghosting when things move fast. Needed for <a href=\"#f/reflections/rough-reflections\">rough reflections</a>.</li><li><code>r_ssrMaxRoughness 0.6</code> — surfaces rougher than this use only the cubemap; lower it if blurry reflections look wrong.</li><li><code>r_ssrEmitters 1</code> — sabers, blaster bolts and other glowing effects are reflected even when off screen.</li><li><code>r_ssrEmitterIntensity 1</code> — their brightness.</li></ul>\n<p><code>r_ssrCompare 1</code> shows cubemap-only reflections on the left and SSR on the right.</p>\n<div class=\"how\"><h3>How it works</h3><p>For each glossy pixel the shader marches a ray through the on-screen depth image until it hits a surface, and uses that pixel's color as the reflection. A confidence value fades SSR out near screen edges and where the ray misses, so it blends into the cubemap without a seam. Sabers and effects are added separately as analytic light sources, which is why they reflect from off screen too.</p></div>\n<ul><li><code>r_ssrMaxDistance 1024</code> — longest reflection ray, in world units.</li><li><code>r_ssrThickness 8</code> — how thick on-screen surfaces are assumed to be; too low lets rays pass behind thin objects.</li><li><code>r_ssrEdgeFade 0.1</code> — fade band at screen edges.</li><li><code>r_ssrBlendStrength 1</code> — 0 cubemap only … 1 full SSR where reliable.</li><li><code>r_ssrRoughOcclusion 1</code> <em>(restart)</em> — on rough surfaces SSR keeps the cubemap's darkening in baked shadow; 0 lets light reflections \"ghost\" on shadowed walls.</li><li><code>r_ssrEmitterMaxRoughness 0.35</code> — rougher surfaces don't reflect sabers and effects (their dynamic light already makes a highlight there).</li><li><code>r_ssrSteps 0</code>, <code>r_ssrRefineSteps 0</code> — march and refinement steps; 0 from the quality.</li><li><code>r_ssrHalfRes -1</code> — trace at half resolution: -1 from the quality, 0 full, 1 half.</li><li><code>r_ssrHiZ -1</code> — faster hierarchical tracing: -1 from the quality, 0 off, 1 on.</li><li><code>r_ssrTemporalWeight 0.9</code> — how much history frame averaging keeps.</li><li><code>r_depthPrepass 1</code> — the depth pre-pass SSR, AO and contact shadows rely on; keep it on.</li></ul>\n<p>Speed options. Defaults are measured to be fastest; change only to compare.</p>\n<ul><li><code>r_ssrReceiverCull -1</code> — skip pixels that need no reflection early: -1 automatic, 0 off, 1 always.</li><li><code>r_ssrResolveCache 1</code> — turn half-resolution hits into colors once per frame, then upsample.</li><li><code>r_ssrFusedResolve 1</code> — without frame averaging, resolve reflections inside the final blend instead of a separate pass.</li><li><code>r_ssrHitCache 0</code> — 1: reuse still-valid hits from previous frames. Checking an old hit costs about as much as tracing a new one, so it is off.</li><li><code>r_ssrFootprintDepth 1</code> — blurry reflections don't smear a foreground object into the background. Needs hierarchical tracing.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_ssrDebug 1..12</code> — normal, roughness, reflectance, hit/miss, hit distance, confidence, raw SSR, cubemap, combined, replaced part, saber/effect reflections, reused hits.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-ssr.md\" target=\"_blank\" rel=\"noopener\">rend2-ssr.md</a>, <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-ssr-validation.md\" target=\"_blank\" rel=\"noopener\">validation</a>.</p>"
   }
  ],
  "media": []
 },
 "reflections/rough-reflections": {
  "group": "Reflections",
  "title": "Rough reflections",
  "cvar": "r_ssrStochastic",
  "sections": [
   {
    "title": "Rough reflections",
    "html": "<p>On a rough floor, a lamp's reflection should stretch into a long streak toward you at shallow angles. Ordinary SSR traces one mirror ray and blurs it, which gives a round smudge instead. This mode traces rays scattered the way a rough surface really scatters light, so reflections take the right shape. It looks grainy for a moment after fast movement, until the frame history settles. Needs <code>r_ssr 1</code> and <code>r_ssrTemporal 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> heavy compared with ordinary SSR.</p>\n<ul><li><code>r_ssrStochastic 0</code> <em>(restart)</em> — 1 on.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Each pixel traces one ray in a random direction drawn from its surface's roughness, shares results with its neighbours, and averages over frames.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_ssrDebug 7</code> — raw reflection before blending.</li></ul>"
   }
  ],
  "media": []
 },
 "reflections/fog-in-reflections": {
  "group": "Reflections",
  "title": "Fog in reflections",
  "cvar": "r_ssrReflectedFog",
  "sections": [
   {
    "title": "Fog in reflections",
    "html": "<p>With volumetric fog, a distant object seen in a reflection should be as hazy as when you look at it directly. This makes the reflected ray pass through the fog too. On by default; also used by water reflections. Needs <code>r_volumetricFog 2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium.</p>\n<ul><li><code>r_ssrReflectedFog 1</code> <em>(restart)</em> — 0 off.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The fog between the reflecting surface and the reflected hit is sampled along the ray and applied to the reflection. Cubemap fallback and saber/effect reflections don't get it.</p></div>"
   }
  ],
  "media": []
 },
 "fog/volumetric-fog": {
  "group": "Fog and light beams",
  "title": "Volumetric fog",
  "cvar": "r_volumetricFog",
  "sections": [
   {
    "title": "Volumetric fog",
    "html": "<p>The original fog only fades surfaces toward a color with distance. Volumetric fog fills the space itself: sun beams fall through windows and are cut by shadows, sabers and blaster bolts glow in the haze around them, and lamps light the fog near them.</p>\n<p>It lights the fog a map already has (its fog volumes). <strong>A map without fog shows nothing new</strong> until you add a fog source: <a href=\"#f/fog/ground-haze\">ground haze</a>, <a href=\"#f/weather/rain-haze\">rain haze</a> or authored <a href=\"#f/fog/local-fog-volumes-mod-authors\">local fog</a>. Sun beams need realtime sun (<a href=\"#f/shadows/sun-shadows\"><code>r_forceSun 1</code></a> on stock maps).</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; fine grids and the extra options below can make it heavy.</p>\n<ul><li><code>r_volumetricFog 0</code> <em>(restart)</em> — <strong>0</strong> off. <strong>1</strong> Rend2's original volumetric fog: fog lit only by the map's baked light, no sun beams or dynamic lights (see <a href=\"#f/fog/original-rend2-volumetric-fog\">the end of this page</a>). <strong>2</strong> the volumetric fog described here.</li><li><code>r_volumetricFogQuality 1</code> <em>(restart)</em> — 0 low, 1 medium, 2 high: the size of the 3D grid the fog is computed in.</li><li><code>r_volumetricFogSunScale 1</code> — strength of sunlight in fog (beams).</li><li><code>r_volumetricFogDlightScale 1</code> — strength of dynamic lights in fog.</li><li><code>r_volumetricFogStaticScale 1</code> — strength of the map's baked light in fog.</li><li><code>r_volumetricFogAnisotropy 0.2</code> — how much fog glows when you look toward a light: 0 the same from every side, higher brighter toward the light (stronger beams against the sun), negative brighter with the light behind you.</li><li><code>r_volumetricFogDlightShadows 1</code> — dynamic lights cast shadows into fog (needs <code>r_dlightMode 2</code>).</li><li><code>r_volumetricFogBloom 0</code> — let bright fog glow add to bloom.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The space in front of the camera is divided into a 3D grid of small cells shaped to the view (froxels: frustum voxels). Every frame each cell gets its fog density and the light that reaches it — baked light from the map's light grid, the sun with its shadow maps, dynamic lights with theirs — and then light and fog are summed along each view ray. Where the map's baked light includes the sun, that part is replaced by the realtime sun, so it can cast beams. The grid is coarse, so results are jittered and averaged over frames.</p></div>\n<ul><li><code>r_volumetricFogGridScale 0</code> <em>(restart)</em> — screen pixels per cell; 0 from the quality.</li><li><code>r_volumetricFogSlices 0</code> <em>(restart)</em> — depth slices; 0 from the quality.</li><li><code>r_volumetricFogFar 0</code> — how far the grid reaches; 0 automatic (4096). Beyond it, the last slice is extended.</li><li><code>r_volumetricFogTemporal 1</code> — jitter and average over frames; 0 is noisier but reacts instantly.</li><li><code>r_volumetricFogHistoryWeight 0.9</code> — how much history to keep.</li><li><code>r_volumetricFogReset 0</code> — set by the game to clear history after a camera cut.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 1..29</code> — density, sun (unshadowed / shadowed), dynamic lights, baked light, scattering, transmittance, history, slices, density by source (map fog, ground haze, noise, local volumes, effects), dynamic lights per cell. Later numbers are listed in the sections below.</li><li><code>r_volumetricFogFreeze 1</code> — freeze the current fog volume and camera.</li><li><code>r_vfog</code> — inspect or change the fog state.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-volumetric-fog.md\" target=\"_blank\" rel=\"noopener\">rend2-volumetric-fog.md</a>.</p>"
   }
  ],
  "media": []
 },
 "fog/ground-haze": {
  "group": "Fog and light beams",
  "title": "Ground haze",
  "cvar": "r_volumetricFogHeight",
  "sections": [
   {
    "title": "Ground haze",
    "html": "<p>Low mist that is thick near the ground and thins with height, on any map, without fog brushes. A simple way to get visible beams on maps without fog. Needs <code>r_volumetricFog 2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_volumetricFogHeight 0</code> — 1 on.</li><li><code>r_volumetricFogHeightOpaqueDistance 3000</code> — at ground level, the distance where the haze becomes opaque. Lower is thicker.</li><li><code>r_volumetricFogHeightFalloff 256</code> — how quickly it thins with height; larger means a taller layer.</li><li><code>r_volumetricFogHeightColor \"0.7 0.75 0.8\"</code> — haze color.</li><li><code>r_volumetricFogHeightBase auto</code> — world height of the ground; auto picks the lowest floor (a map's <code>env.json</code> can override it).</li><li><code>r_volumetricFogHeightMaxDensity 1</code> — density cap below the base height.</li><li><code>r_volumetricFogHeightTopHeight 0</code> — height where the haze fades out completely; 0 never.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 12</code> — ground-haze density.</li></ul>"
   }
  ],
  "media": []
 },
 "fog/uneven-fog": {
  "group": "Fog and light beams",
  "title": "Uneven fog",
  "cvar": "r_volumetricFogNoise",
  "sections": [
   {
    "title": "Uneven fog",
    "html": "<p>Breaks uniform fog into drifting patches and wisps, so mist looks less like a smooth wall. Choose which fog sources get it.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium.</p>\n<ul><li><code>r_volumetricFogNoise 0</code> — add bits: <strong>1</strong> ground haze, <strong>2</strong> map fog volumes, <strong>4</strong> whole-map fog, <strong>8</strong> local volumes marked for noise. Example: <code>3</code> for ground haze and map fog.</li><li><code>r_volumetricFogNoiseScale 4096</code> — size of the large patches, in world units.</li><li><code>r_volumetricFogNoiseContrast 1</code> — patchiness: 0 uniform, 1 between none and double density, higher sparser clumps. Average density stays the same.</li><li><code>r_volumetricFogNoiseWind \"0 0 0\"</code> — drift speed and direction, world units per second.</li><li><code>r_volumetricFogNoiseDetailScale 900</code>, <code>r_volumetricFogNoiseDetailContrast 0</code> — a second, finer noise layer; 0 contrast turns it off (cheaper).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 13..15</code> — noise, density without and with noise.</li></ul>"
   }
  ],
  "media": []
 },
 "fog/shadows-inside-thick-fog": {
  "group": "Fog and light beams",
  "title": "Shadows inside thick fog",
  "cvar": "r_volumetricSelfShadow",
  "sections": [
   {
    "title": "Shadows inside thick fog",
    "html": "<p>In thick smoke or dense mist, the side facing the light should be bright and the far side dark, because the fog itself blocks light. Plain volumetric fog lets light through every cell unhindered, so dense clouds glow evenly. This makes the fog shadow itself.</p>\n<p class=\"cost\"><strong>Cost:</strong> heavy.</p>\n<ul><li><code>r_volumetricSelfShadow 0</code> <em>(restart)</em> — <strong>0</strong> off, <strong>1</strong> from the sun, <strong>2</strong> from the sun and the strongest dynamic lights.</li><li><code>r_volumetricSelfShadowMaxLights 2</code> — dynamic lights for mode 2 (0–4).</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>For each fog cell the renderer samples density along a short ray toward the light and dims the light by what it passes through. More samples, longer rays and more lights cost more.</p></div>\n<ul><li><code>r_volumetricSelfShadowSamples 6</code> — samples along the sun ray (dynamic lights use half).</li><li><code>r_volumetricSelfShadowDistance 768</code> — length of that ray, in world units.</li><li><code>r_volumetricSelfShadowOutsideHeightFog 1</code> — beyond the ray, count ground haze analytically; 0 ignore it.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 40..45</code> — density, optical depth toward the sun, fog shadow, geometry shadow, both.</li></ul>"
   }
  ],
  "media": []
 },
 "fog/light-bouncing-inside-fog": {
  "group": "Fog and light beams",
  "title": "Light bouncing inside fog",
  "cvar": "r_volumetricMultiScatter",
  "sections": [
   {
    "title": "Light bouncing inside fog",
    "html": "<p>Real clouds and steam are white inside because light bounces around in them many times. With self-shadowing alone thick white fog gets unnaturally dark at its core. This adds back the light those bounces would bring, while dark smoke stays dark. Needs <code>r_volumetricSelfShadow 1</code> or <code>2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium.</p>\n<ul><li><code>r_volumetricMultiScatter 0</code> — <strong>0</strong> off, <strong>1</strong> for the sun, <strong>2</strong> for the sun and self-shadowed dynamic lights.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>A few cheap extra scattering steps (\"octaves\") each let light penetrate deeper, with less energy and more evenly in all directions — an approximation of many bounces without tracing them.</p></div>\n<ul><li><code>r_volumetricMultiScatterOctaves 2</code> — extra steps, 1–3.</li><li><code>r_volumetricMultiScatterAttenuation 0.25</code> — how much deeper each step reaches; lower is deeper.</li><li><code>r_volumetricMultiScatterContribution 0.5</code> — light returned per step.</li><li><code>r_volumetricMultiScatterPhase 0.5</code> — how quickly the steps lose their direction.</li><li><code>r_volumetricMultiScatterLength 64</code> — typical size of a dense cloud; thin fog gets almost none.</li><li><code>r_volumetricMultiScatterShadowFill 0.25</code> — how far it may brighten geometry shadows inside thick fog (0–0.5).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 46..50</code> — single, multiple and combined scattering, their ratio, optical depth.</li></ul>"
   }
  ],
  "media": []
 },
 "fog/colored-fog-absorption": {
  "group": "Fog and light beams",
  "title": "Colored fog absorption",
  "cvar": "r_volumetricFogRGBExtinction",
  "sections": [
   {
    "title": "Colored fog absorption",
    "html": "<p>Normally fog dims everything behind it evenly, so it can only darken or tint by its own glow. With this, fog can absorb colors differently: deep water swallows red first and turns things blue-green, colored smoke tints the scene behind it.</p>\n<p class=\"cost\"><strong>Cost:</strong> heavy (memory and time).</p>\n<ul><li><code>r_volumetricFogRGBExtinction 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_volumetricFogHeightExtinction \"1 1 1\"</code> — ground haze: relative absorption of red, green and blue.</li></ul>\n<p>Underwater fog and water/slime/lava already define their colors; this lets the fog keep them. Effect smoke stays neutral.</p>\n<p class=\"mod\"><strong>Mod authors:</strong> <code>fogExtinctionColor 3 0.5 0.5</code> in a map fog shader, <code>\"Extinction\": [3,0.5,0.5]</code> in a local volume.</p>\n<div class=\"how\"><h3>How it works</h3><p>Each fog cell stores three transparencies, one per color channel, instead of one; compositing then needs two draws.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 51..56</code> — absorption, transparency, color shift, difference, chroma, far transparency.</li></ul>"
   }
  ],
  "media": []
 },
 "fog/local-fog-volumes-mod-authors": {
  "group": "Fog and light beams",
  "title": "Local fog volumes (mod authors)",
  "cvar": "",
  "sections": [
   {
    "title": "Local fog volumes (mod authors)",
    "html": "<p>Place a pocket of steam, a low smoke bank or a box of dusty air on a map without recompiling it. Volumes are lit and shadowed like any other fog. Needs <code>r_volumetricFog 2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light for a few small volumes; many overlapping ones medium.</p>\n<p>Add a <code>FogVolumes</code> array to <code>cubemaps/&lt;map&gt;/env.json</code>:</p>\n<pre><code>{&quot;FogVolumes&quot;:[{&quot;Shape&quot;:&quot;sphere&quot;,&quot;Origin&quot;:[0,0,128],&quot;Radius&quot;:96,\n&quot;Opaque&quot;:250,&quot;Color&quot;:[0.55,0.55,0.58],&quot;Softness&quot;:0.6}]}</code></pre>\n<p><code>Origin</code> and <code>Radius</code> place it, <code>Opaque</code> is the distance at which it becomes opaque, <code>Color</code> its color. Boxes use <code>Size</code> and optional <code>Angles</code>; ellipsoids are also supported.</p>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 16..19</code> — local density, share, bounds, volumes per slice.</li><li><code>r_fogvol</code> — print volumes and statistics. <code>r_fogvol add ...</code> adds a test volume, <code>r_fogvol test &lt;count&gt; [spread]</code> a stress test, <code>r_fogvol remove &lt;index&gt; | clear</code> removes them, <code>r_fogvol slices</code> shows per-slice lists, <code>r_fogvol dump</code> prints test volumes as a <code>FogVolumes</code> array to paste into <code>env.json</code>.</li></ul>"
   }
  ],
  "media": []
 },
 "fog/different-kinds-of-fog-mod-authors": {
  "group": "Fog and light beams",
  "title": "Different kinds of fog (mod authors)",
  "cvar": "",
  "sections": [
   {
    "title": "Different kinds of fog (mod authors)",
    "html": "<p>Steam, dust and dark smoke scatter light differently: steam is bright and glows toward the light, smoke absorbs. Fog sources can set this individually; without it the global <code>r_volumetricFogAnisotropy</code> applies.</p>\n<ul><li>Map fog shaders: <code>fogAlbedo 0.9 0.4 0.3</code> (share of light scattered rather than absorbed, per color) and <code>fogAnisotropy 0.6</code> (how strongly it glows toward the light), next to <code>fogParms</code>.</li><li>Local volumes: <code>\"Anisotropy\": 0.6</code>. Effect media: <code>anisotropy 0.6</code>.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 35..39</code> — absorption, albedo, scattering lobes, mixed anisotropy, sun phase.</li></ul>"
   }
  ],
  "media": []
 },
 "fog/original-rend2-volumetric-fog": {
  "group": "Fog and light beams",
  "title": "Original Rend2 volumetric fog",
  "cvar": "r_volumetricFog 1",
  "sections": [
   {
    "title": "Original Rend2 volumetric fog",
    "html": "<p><code>r_volumetricFog 1</code> keeps Rend2's original volumetric fog: the map's fog volumes are lit by its baked light only, with no sun beams, shadows or dynamic lights. It is still used for views without the new fog grid (portals, mirrors, sky portals).</p>\n<p class=\"cost\"><strong>Cost:</strong> medium.</p>\n<ul><li><code>r_volumetricFogSamples 48</code> <em>(restart)</em> — samples per ray; fewer is faster but banded.</li><li><code>r_volumetricFogDefaultScale 1.0</code> <em>(restart)</em> — density of fog volumes that don't set their own scale.</li><li><code>r_volumetricFogScale 1.0</code> — temporary density multiplier (not saved).</li></ul>"
   }
  ],
  "media": []
 },
 "effects/volumetric-smoke": {
  "group": "Smoke and effects",
  "title": "Volumetric smoke",
  "cvar": "r_volumetricParticles",
  "sections": [
   {
    "title": "Volumetric smoke",
    "html": "<p>Original smoke is a stack of flat transparent pictures. It doesn't block what is behind it in proportion to its thickness, lamps don't color it, and sun beams don't enter it. With this, smoke from effects also becomes real fog in the volumetric fog grid: it is lit and shadowed like the rest of the fog, hides the background according to its depth, and sabers glow inside it. The original sprites are still drawn on top.</p>\n<p>Stock effects have no volume data, so the game recognizes known smoke effects automatically. Mods can also give their effects volume directly.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; heavy when large clouds overlap.</p>\n<ul><li><code>r_volumetricParticles 0</code> — 1 on. Without it nothing below is shown.</li><li><code>fx_physicalization 0</code> — automatic volume for stock smoke: <strong>0</strong> off, <strong>1</strong> every recognized effect, <strong>2</strong> only effects in <code>fx_physicalizationOptIn</code>, <strong>3</strong> every recognized effect except <code>fx_physicalizationOptOut</code>.</li><li><code>fx_physicalizationStrength 1</code> — density of the automatic smoke, 0–16.</li><li><code>fx_physicalizationOptIn \"\"</code>, <code>fx_physicalizationOptOut \"\"</code> — effect names for modes 2 and 3, separated by spaces, commas or semicolons, e.g. <code>\"volumetric/black_smoke;rocket/explosion\"</code>. A list only picks among recognized effects; it can't turn an unknown effect into smoke.</li><li><code>fx_physicalizationComposite 0</code> — 1: also smoke inside composite explosions.</li><li><code>fx_physicalizationEmission 0</code> — 1: known stock flames and fireballs <a href=\"#f/effects/glowing-gas\">glow</a>.</li><li><code>fx_physicalizationAdaptive 0</code> — 1: fit size, softness and density of automatic smoke to each effect.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> add this to an EFX <code>Particle</code> or <code>OrientedParticle</code>; it works even with <code>fx_physicalization 0</code>:</p>\n<pre><code>volumetricMedia\n{\n    extinction 0.04 0.06\n    albedo 0.07 0.07 0.07\n    radiusScale 1\n    aspect 1 1 0.8\n    softness 0.7\n}</code></pre>\n<p><code>extinction</code> sets how much light the smoke blocks, <code>albedo</code> its color (dark here), the rest its shape.</p>\n<div class=\"how\"><h3>How it works</h3><p>Each accepted particle adds a soft 3D blob of density to the fog cells it covers, every frame. Authored data wins; automatic matching is conservative and only accepts effects it recognizes. The most important particles are uploaded first, up to a limit.</p></div>\n<ul><li><code>r_volumetricParticlesMax 128</code> — particles uploaded per frame.</li><li><code>r_volumetricParticlesScale 1</code> — density multiplier.</li><li><code>r_volumetricParticlesHistory 0.3</code> — how much frame averaging is kept where smoke changes; lower reduces smearing behind moving smoke.</li><li><code>fx_physicalizationAggregate 0</code> — experimental: merge exactly overlapping automatic clouds.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 26..28</code> — particle density, history reduction, bounds.</li><li><code>r_volumetricParticlesDebug 1</code> — print culling statistics.</li><li><code>r_volparticles</code> — submission, culling and upload limits.</li><li><code>fx_physicalizationDebug 1</code> — classification and submitted smoke.</li><li><code>fxaudit &lt;efx&gt;</code> — whether an effect qualifies (SP, <code>developer 1</code>).</li><li><code>fx_physicalizationSources 0</code> — 1: track which effect each particle belongs to, for <code>fxsources</code> (SP/MP, <code>developer 1</code>). No visual change.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/legacy-fx-physicalization.md\" target=\"_blank\" rel=\"noopener\">legacy-fx-physicalization.md</a>.</p>"
   }
  ],
  "media": []
 },
 "effects/lit-smoke-sprites": {
  "group": "Smoke and effects",
  "title": "Lit smoke sprites",
  "cvar": "r_particleLighting",
  "sections": [
   {
    "title": "Lit smoke sprites",
    "html": "<p>Ordinary smoke sprites keep their painted color everywhere: bright white smoke in a dark room. With this, they take the light around them: dark in the dark, orange next to fire, blue next to a saber. Glowing additive effects (sparks, flames) stay unlit. Works on stock effects.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium.</p>\n<ul><li><code>r_particleLighting 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_particleLightingMix 1</code> — 0 the painted color … 1 fully lit.</li><li><code>r_particleLightingScale 1</code> — brightness of the light; 1 leaves smoke in average map light unchanged.</li><li><code>r_particleLightingFloor 0.03</code> — minimum brightness, so smoke never goes fully black.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Each sprite samples baked, sun and dynamic light from the volumetric fog's light grid at its position. Only the color changes, not shape or motion.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_particleLightingDebug 1..5</code> — light field, baked only, sun only, dynamic only, which sprites are lit.</li></ul>"
   }
  ],
  "media": []
 },
 "effects/glowing-gas": {
  "group": "Smoke and effects",
  "title": "Glowing gas",
  "cvar": "r_volumetricEmission",
  "sections": [
   {
    "title": "Glowing gas",
    "html": "<p>Lets fog and smoke glow by themselves: luminous gas, a fireball volume, hot smoke. It glows even with no lamp nearby, and adds to bloom, but doesn't light the surfaces around it. Stock content doesn't glow unless <code>fx_physicalizationEmission 1</code>; mods can author it.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_volumetricEmission 1</code> — global glow strength; 0 off.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> local fog volumes take <code>\"Emissive\": [r,g,b]</code> and <code>\"EmissiveDensity\": value</code>. In EFX <code>volumetricMedia</code> add <code>emissive 6 3 1.2</code>, <code>emissiveDensity 0.02</code> and optionally <code>emissiveTint 1</code>.</p>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 30..34</code> — scattered light, glow, both, integrated glow, history.</li></ul>"
   }
  ],
  "media": []
 },
 "sky/atmosphere": {
  "group": "Sky, atmosphere and clouds",
  "title": "Atmosphere",
  "cvar": "r_atmosphere",
  "sections": [
   {
    "title": "Atmosphere",
    "html": "<p>In real air distant mountains and buildings turn pale and bluish, and the sky is brighter near the horizon. JKA draws the far distance as crisp as the near one. This adds that haze, and optionally replaces the painted skybox with a computed sky that matches the sun.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium.</p>\n<ul><li><code>r_atmosphere 0</code> — 1 on.</li><li><code>r_atmosphereAerialScale 1</code> — <strong>the main look setting.</strong> 1 is physically correct, but JKA views span only a few hundred metres of air, so the haze is barely visible. 8–30 makes it clearly visible.</li><li><code>r_atmosphereSky 0</code> — <strong>0</strong> keep the skybox, add horizon haze and sun glow. <strong>1</strong> blend skybox and computed sky. <strong>2</strong> computed sky only. Maps with sky portals always use 0.</li><li><code>r_atmosphereSkyBlend 0.5</code> — mode 1: share of the computed sky.</li><li><code>r_atmosphereStart 0</code> — no haze closer than this distance (world units).</li><li><code>r_atmosphereSunGlow 1</code> — mode 0: glow around the sun over the skybox. Lower it if the skybox's painted sun is elsewhere.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The renderer models two kinds of scattering along long view paths: air molecules (Rayleigh), which make the sky blue and distances bluish, and dust/haze (Mie), which makes a whitish glow around the sun. The result is applied to distant surfaces and the sky before volumetric fog.</p></div>\n<ul><li><code>r_atmosphereRayleigh 1</code>, <code>r_atmosphereMie 1</code>, <code>r_atmosphereOzone 1</code> — density of air, of haze, and of ozone (sky color only).</li><li><code>r_atmosphereMieG 0.8</code> — how tightly the haze glow hugs the sun.</li><li><code>r_atmosphereSunIntensity 1</code> — sun brightness.</li><li><code>r_atmosphereSunColor 1</code> — sun color: <strong>0</strong> the map's sun as seen above the atmosphere, <strong>1</strong> as seen on the ground, <strong>2</strong> white.</li><li><code>r_atmosphereSunSize 0.53</code> — sun disc size in degrees.</li><li><code>r_atmosphereUnitScale 0.03</code> — metres per world unit (a 64-unit player is about 1.8 m).</li><li><code>r_atmosphereGroundZ auto</code> — world height of the ground; auto picks the lowest floor.</li><li><code>r_atmosphereAltitude 0</code> — ground altitude above sea level, in metres.</li></ul>\n<p>The original <code>r_drawSun</code> still decides whether a sun disc is drawn, so it isn't drawn twice.</p>\n<h4>Debug</h4>\n<ul><li><code>r_atmosphereDebug 1..8</code> — air glow, haze glow, transparency, distance haze, computed sky alone, overlap with fog, lookup tables, distance bands.</li><li><code>r_atmosphereInfo</code> — configuration and status.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-atmosphere.md\" target=\"_blank\" rel=\"noopener\">rend2-atmosphere.md</a>.</p>"
   }
  ],
  "media": []
 },
 "sky/volumetric-clouds": {
  "group": "Sky, atmosphere and clouds",
  "title": "Volumetric clouds",
  "cvar": "r_clouds",
  "sections": [
   {
    "title": "Volumetric clouds",
    "html": "<p>Real 3D clouds above the map, with shaded undersides and bright edges toward the sun, drifting with the wind. Optional cloud shadows move across the ground and through fog.</p>\n<p class=\"cost\"><strong>Cost:</strong> heavy.</p>\n<ul><li><code>r_clouds 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_cloudCoverage 0.5</code> — 0 clear sky, 0.5 scattered clouds, 1 overcast.</li><li><code>r_cloudShadows 0</code> — 1: clouds shade the sunlit world and the sun in fog.</li><li><code>r_cloudWindSpeed 10</code>, <code>r_cloudWindDir 45</code> — wind in m/s and its direction in degrees.</li><li><code>r_cloudBase 1500</code>, <code>r_cloudTop 4000</code> — cloud layer height, in metres above the ground.</li><li><code>r_cloudDensity 1</code> — how thick the clouds are.</li><li><code>r_cloudScale 2</code> <em>(restart)</em> — render resolution divisor: 1 full, 2 half, 4 quarter. Lower numbers look sharper and cost much more.</li><li><code>r_cloudSkyPortal 0</code> — 1: also draw clouds on maps with sky portals (approximate).</li><li><code>r_cloudLegacy 1</code> — SP <code>fx_cloudlayer</code> entities: <strong>0</strong> ignored (their old haze is drawn), <strong>1</strong> used as a hint for the cloud style, <strong>2</strong> drawn as a literal ring (testing).</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>For each pixel a ray is marched through a layer of noise-shaped cloud density, checking light toward the sun at each step. The result is rendered at reduced resolution and averaged over frames.</p></div>\n<ul><li><code>r_cloudSteps 64</code>, <code>r_cloudStepLength 150</code> — maximum steps per ray and step length in metres.</li><li><code>r_cloudMaxDistance 60</code> — how far clouds are drawn, in km.</li><li><code>r_cloudShadowSteps 5</code> — light steps toward the sun.</li><li><code>r_cloudMSOctaves 3</code> — approximated multiple scattering; 1 single scattering only (darker clouds).</li><li><code>r_cloudAmbient 1</code> — sky light on clouds.</li><li><code>r_cloudAnisotropy 0.6</code> — strength of the bright edge toward the sun.</li><li><code>r_cloudDetail 1</code> — 0 skip the fine erosion noise (cheaper, blobbier).</li><li><code>r_cloudShapeScale 12</code>, <code>r_cloudDetailScale 1.5</code>, <code>r_cloudWeatherScale 40</code> — sizes (km) of the shape noise, detail noise and coverage map.</li><li><code>r_cloudTemporal 0.9</code> — frame averaging; 0 off.</li><li><code>r_cloudShadowExtent 8</code> — size of the cloud-shadow area around the camera, in km.</li><li><code>r_cloudShadowInterval 1</code> — frames between cloud-shadow updates.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_cloudDebug 1..11</code> — ray interval, density, noises, sun light, scattering, history, radiance, legacy mask, coverage map, shadow map.</li><li><code>r_cloudInfo</code> — configuration and status.</li><li><code>r_cloudLayerTest</code> — test cloud-layer entity handling.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-volumetric-clouds.md\" target=\"_blank\" rel=\"noopener\">rend2-volumetric-clouds.md</a>.</p>"
   }
  ],
  "media": []
 },
 "sky/sky-portal-speed-up": {
  "group": "Sky, atmosphere and clouds",
  "title": "Sky portal speed-up",
  "cvar": "r_skyPortalCull",
  "sections": [
   {
    "title": "Sky portal speed-up",
    "html": "<p>On maps with a sky portal (a separate scene shown through the sky), the portal scene is shaded only where the main view leaves the sky visible. The image is unchanged; on by default.</p>\n<ul><li><code>r_skyPortalCull 1</code> — 0 always shade the whole portal view.</li></ul>"
   }
  ],
  "media": []
 },
 "weather/rain-streaks": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Rain streaks",
  "cvar": "r_rainStreaks",
  "sections": [
   {
    "title": "Rain streaks",
    "html": "<p>Original rain is thin bright lines that look the same in a lit street and a dark alley. Modern streaks are shaped by fall speed and wind, lit by the sun and the map's light, fade naturally up close and in the distance, and stop at roofs.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium in rain.</p>\n<ul><li><code>r_rainStreaks 0</code> — 1 modern streaks.</li><li><code>r_rainStreakOpacity 1</code>, <code>r_rainStreakWidth 1</code>, <code>r_rainStreakLength 1</code> — opacity and size multipliers.</li><li><code>r_rainStreakLighting 1</code> — 0 fixed brightness, as originally; 1 lit by the sun and the map.</li><li><code>r_rainDynamicLighting 1</code> — dynamic lights (sabers, blasters) on drops and splashes: <strong>0</strong> none, <strong>1</strong> the strongest nearby light, <strong>2</strong> several nearby lights.</li><li><code>r_rainDynamicMaxLights 4</code> — lights per drop in mode 2, 1–8.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_rainStreakDebug 1..5</code> — coverage, distance fade, lighting, roof contact, variation.</li><li><code>r_rainDynamicDebug 1..3</code> — static light only, dynamic light only, number of lights used.</li></ul>"
   }
  ],
  "media": []
 },
 "weather/rain-haze": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Rain haze",
  "cvar": "r_rainVolume",
  "sections": [
   {
    "title": "Rain haze",
    "html": "<p>Heavy rain hides the distance: courtyards fade into grey, and lamps glow through the downpour. This adds that haze where rain falls; covered interiors stay clear instead of getting a screen-wide grey tint. Needs <code>r_volumetricFog 2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_rainVolume 0</code> — 1 on.</li><li><code>r_rainVolumeStrength 0.00018</code> — haze density in normal rain.</li><li><code>r_rainVolumeHeavyMultiplier 4</code> — extra density in heavy rain.</li><li><code>r_rainVolumeGroundStrength 0</code> — splash mist close to the ground in heavy rain; 0 off.</li><li><code>r_rainVolumeGroundHeight 96</code> — height of that mist layer.</li><li><code>r_rainVolumeMaxDistance 6000</code> — how far the haze reaches.</li><li><code>r_rainVolumeAnisotropy 0.72</code> — how strongly rain glows when you look toward a light.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 71..74</code> — rain density, weather exposure, unshadowed light, final rain haze.</li></ul>"
   }
  ],
  "media": []
 },
 "weather/distant-rain": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Distant rain",
  "cvar": "r_rainLOD",
  "sections": [
   {
    "title": "Distant rain",
    "html": "<p>Nearby rain is drawn as individual drops; farther away the drops thin out and the <a href=\"#f/weather/rain-haze\">rain haze</a> takes over. This removes the wall of tiny distant lines and the cost of drawing them. On by default; without rain haze, the ordinary streaks stay.</p>\n<ul><li><code>r_rainLOD 1</code> — 0 off.</li><li><code>r_rainLODNear 1200</code> — full drop density up to this distance.</li><li><code>r_rainLODMid 3600</code> — drops are gone beyond this distance.</li><li><code>r_rainLODDensityFloor 0.25</code> — lowest drop density between the two.</li><li><code>r_rainLODFarVolumeCoupling 1</code> — 1 rain haze fades in as drops fade out; 0 haze at every distance (the earlier behaviour).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_rainLODDebug 1</code> — distance bands: green near, yellow middle, blue far.</li><li><code>r_speeds 100</code> — print the current rain distance state and weather counts.</li></ul>"
   }
  ],
  "media": []
 },
 "weather/wet-surfaces": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Wet surfaces",
  "cvar": "r_weatherWetness",
  "sections": [
   {
    "title": "Wet surfaces",
    "html": "<p>Rain-exposed surfaces darken and become glossy: armor and metal gain sharp reflections, stone darkens, fabric mostly just darkens. Areas under roofs stay dry. Each surface reacts according to its <a href=\"#f/materials/automatic-materials-from-stock-textures-auto-pbr\">material class</a>, so it works best with <code>r_autoPBR 2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_weatherWetness 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_weatherWetnessStrength 1</code> — overall wetness, 0–1.</li><li><code>r_weatherWetnessEntityFacing 0.85</code> — wetness of the vertical sides of characters and props.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The weather map tells how exposed each point is; the material class decides how much wetness darkens it, smooths it and flattens fine bumps.</p></div>\n<ul><li><code>r_weatherWetnessRoughness 1</code>, <code>r_weatherWetnessDarkening 1</code>, <code>r_weatherWetnessNormal 1</code> — scale the per-class smoothing, darkening and bump flattening.</li><li><code>r_weatherWetnessBias 2</code> — tolerance of the roof test; raise it if exposed edges stay dry.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 1..4</code> — exposure, wetness, roughness, dry/wet split.</li><li><code>r_weatherSurfaceDebug 16</code>, <code>21</code> — detected material class, surface normal.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-weather-material.md\" target=\"_blank\" rel=\"noopener\">rend2-weather-material.md</a>.</p>"
   }
  ],
  "media": []
 },
 "weather/wet-after-the-rain": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Wet after the rain",
  "cvar": "r_weatherWetnessPersistence",
  "sections": [
   {
    "title": "Wet after the rain",
    "html": "<p>Surfaces get wet gradually when rain starts and dry slowly after it stops, instead of switching instantly. Steep surfaces dry faster. Needs <code>r_weatherWetness 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_weatherWetnessPersistence 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_weatherWetnessAccumulationRate 0.20</code> — how fast surfaces get wet.</li><li><code>r_weatherWetnessDryRate 0.02</code> — how fast they dry; 0.02 is about 50 seconds.</li><li><code>r_weatherWetnessSteepDryScale 2</code> — steep surfaces dry this much faster.</li><li><code>r_weatherWetnessAcidAccumulationScale 1</code>, <code>r_weatherWetnessAcidDryScale 1</code> — the same rates in acid rain.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>A world-space texture over the level stores how much water each spot holds; rain adds to it and time removes it.</p></div>\n<ul><li><code>r_weatherWetnessResolution 256</code> <em>(restart)</em> — resolution of that texture (64–1024).</li><li><code>r_weatherWetnessUpdateHz 10</code> — updates per second.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherWetnessPersistenceDebug 1</code> — stored water.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-weather-wetness-persistence.md\" target=\"_blank\" rel=\"noopener\">rend2-weather-wetness-persistence.md</a>.</p>"
   }
  ],
  "media": []
 },
 "weather/puddles": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Puddles",
  "cvar": "r_weatherPuddles",
  "sections": [
   {
    "title": "Puddles",
    "html": "<p>Glossy puddles form on flat exposed floors and ground, with no map changes. On textures with a height map (texture packs), water fills cracks and low spots first. Needs <code>r_weatherWetness 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_weatherPuddles 0</code> — 1 on.</li><li><code>r_weatherPuddleCoverage 0.35</code> — share of flat exposed ground covered.</li><li><code>r_weatherPuddleScale 192</code> — size of the puddle pattern.</li><li><code>r_weatherPuddleEdgeModel 0</code> — 1: realistic edges, going from dry ground through a dark damp fringe and a thin film to standing water.</li><li><code>r_weatherPuddleUseHeightMap 1</code> — follow texture height maps where available.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>A world-space noise pattern, limited to flat (<code>r_weatherPuddleSlope</code>) and rain-exposed surfaces, decides where water stands. With a height map, a virtual water level fills the relief from the bottom.</p></div>\n<ul><li><code>r_weatherPuddleRoughness 0.04</code> — smoothness of standing water.</li><li><code>r_weatherPuddleSlope \"0.90 0.98\"</code> — how flat a surface must be (surface normal Z where puddles begin and reach full size).</li><li><code>r_weatherPuddleHeightSoftness 0.08</code> — sharpness of the water line in relief.</li><li><code>r_weatherPuddleWaterLevelBias 0</code> — above 0 floods more of the relief, below 0 only the deepest cracks.</li><li><code>r_weatherPuddleFringeDarkening 0.18</code>, <code>r_weatherPuddleFilmRoughness 0.18</code> — edge model: darkening of the damp fringe, smoothness of the thin film.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 5..15</code> — slope, noise, exposure, mask, roughness, eligibility, height, low/high areas, pattern layers, final mask.</li><li><code>r_weatherSurfaceDebug 33..36</code> — edge model: zones (grey dry, brown damp, cyan film, blue water), roughness, darkening, normal.</li></ul>"
   }
  ],
  "media": []
 },
 "weather/rain-rings-in-puddles": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Rain rings in puddles",
  "cvar": "r_weatherPuddleRipples",
  "sections": [
   {
    "title": "Rain rings in puddles",
    "html": "<p>Raindrops make spreading rings in puddles that distort reflections. On by default once puddles are on.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium.</p>\n<ul><li><code>r_weatherPuddleRipples 1</code> — <strong>0</strong> off. <strong>1</strong> a repeating pattern of rings. <strong>2</strong> rings where drops actually land. <strong>3</strong> real rings nearby, pattern farther away.</li><li><code>r_weatherPuddleRippleStrength 0.35</code> — how much rings bend reflections.</li><li><code>r_weatherPuddleRippleScale 20</code> — ring size.</li><li><code>r_weatherPuddleRippleRate 1.1</code> — rings per second (mode 1).</li><li><code>r_weatherPuddleRippleDistance 1500</code> — modes 2–3: how far real rings are drawn.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>In modes 2 and 3 every rain impact pushes a small water-height field, which spreads and settles like waves.</p></div>\n<ul><li><code>r_weatherPuddleRippleImpulse 0.45</code> — push per drop.</li><li><code>r_weatherPuddleRipplePropagation 10</code> — wave speed.</li><li><code>r_weatherPuddleRippleDamping 2.5</code> — how fast waves die out.</li><li><code>r_weatherPuddleRippleFieldScale 1</code> — area covered by the field.</li><li><code>r_weatherPuddleRippleResolution 256</code> <em>(restart)</em> — field resolution (64–1024).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 17..20</code> — ring height, normal offset, masking, final normal.</li><li><code>r_weatherPuddleRippleDebug 1..4</code> — impacts, height, velocity, final normal.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-puddle-ripples.md\" target=\"_blank\" rel=\"noopener\">rend2-puddle-ripples.md</a>.</p>"
   }
  ],
  "media": []
 },
 "weather/water-running-down-walls": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Water running down walls",
  "cvar": "r_weatherRunoff",
  "sections": [
   {
    "title": "Water running down walls",
    "html": "<p>Thin streams of water run down exposed slopes and walls in the direction of gravity. Needs <code>r_weatherWetness 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_weatherRunoff 0</code> — 1 on.</li><li><code>r_weatherRunoffStrength 1</code> — visibility of the streaks.</li><li><code>r_weatherRunoffSpeed 24</code> — flow speed.</li><li><code>r_weatherRunoffScale 48</code> — streak size.</li><li><code>r_weatherRunoffEntities 0</code> — 1: also on characters, props and moving objects.</li><li><code>r_weatherRunoffTrailPersistence 0</code> — seconds that trails stay dark and glossy after the water passes; needs <a href=\"#f/weather/wet-after-the-rain\">wet after the rain</a>.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Gravity projected onto each surface gives the flow direction; an animated pattern of thin streaks moves along it and changes color and gloss. Walls check for rain a little in front of them.</p></div>\n<ul><li><code>r_weatherRunoffProbe 1.5</code> — that distance; 0 off.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 22..26</code> — slope class, gravity direction, mask, flow, roughness.</li><li><code>r_weatherRunoffTrailDebug 1..2</code> — stored trails, final trail mask.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-weather-runoff.md\" target=\"_blank\" rel=\"noopener\">rend2-weather-runoff.md</a>.</p>"
   }
  ],
  "media": []
 },
 "weather/rain-splashes": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Rain splashes",
  "cvar": "r_rainSplashes",
  "sections": [
   {
    "title": "Rain splashes",
    "html": "<p>Small splashes where drops hit the ground, roofs and puddles.</p>\n<p class=\"cost\"><strong>Cost:</strong> light; medium in very heavy rain.</p>\n<ul><li><code>r_rainSplashes 0</code> — 1 on.</li><li><code>r_rainSplashSize 6</code>, <code>r_rainSplashLifetime 350</code>, <code>r_rainSplashOpacity 0.5</code> — radius, lifetime in ms, opacity.</li><li><code>r_rainSplashSurfaceResponse 0</code> <em>(restart)</em> — 1: splashes depend on the surface: metal, soil, plants, puddles and real water each react differently.</li><li><code>r_rainSplashHardDryStrength 1</code>, <code>r_rainSplashHardWetStrength 1</code>, <code>r_rainSplashMetalStrength 1</code>, <code>r_rainSplashSoilStrength 1</code>, <code>r_rainSplashVegetationStrength 1</code>, <code>r_rainSplashPuddleStrength 1</code>, <code>r_rainSplashWaterStrength 1</code> — splash strength per surface type.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The GPU follows the falling drops and detects where they cross the weather map's surface; each crossing spawns a short-lived splash.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_rainSplashDebug 1..3</code> — impact points, crossing test, drop paths.</li><li><code>r_rainSplashSurfaceDebug 1</code> — surface-type map.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-rain-splashes.md\" target=\"_blank\" rel=\"noopener\">rend2-rain-splashes.md</a>.</p>"
   }
  ],
  "media": []
 },
 "weather/water-on-the-camera-lens": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Water on the camera lens",
  "cvar": "r_rainLens",
  "sections": [
   {
    "title": "Water on the camera lens",
    "html": "<p>Beads, running drops and a wet film on the screen when you stand in rain or come out of water. Under cover the water keeps running down and dries instead of vanishing. Needs <code>r_hdr 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light while wet; nothing when dry.</p>\n<ul><li><code>r_rainLens 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_rainLensQuality 1</code> <em>(restart)</em> — 0–2: resolution, number of drops, blur quality.</li><li><code>r_rainLensFilmModel 0</code> — <strong>0</strong> long-lived beads and drops. <strong>1</strong> mostly a draining film with short-lived small drops.</li><li><code>r_rainLensDensity 1.0</code> — how much rain reaches the lens.</li><li><code>r_rainLensDropSize 1.0</code> — drop size (not the amount of water).</li><li><code>r_rainLensRefraction 1.0</code> — how strongly drops bend the image.</li><li><code>r_rainLensBlur 1.0</code> — drop blur.</li><li><code>r_rainLensFilm 1.0</code> — visibility of the film and trails.</li><li><code>r_rainLensReflection 1.0</code> — reflections and glints of the sun and lights in drops; 0 bending only.</li><li><code>r_rainLensInertia 0</code> — water lags behind sharp camera movement.</li><li><code>r_rainLensPeripheralBias 1.0</code> — keep big drops and flows away from the screen center.</li></ul>\n<p>Film model (<code>r_rainLensFilmModel 1</code>):</p>\n<ul><li><code>r_rainLensEmergeTime 1.0</code>, <code>r_rainLensEmergeWater 1.0</code> — how long the lens drains and how much water stays after leaving water.</li><li><code>r_rainLensFilmMicro 1.0</code> — fine distortion in the film.</li><li><code>r_rainLensImpactSplat 0</code> — slanted drops splat and smear along their direction; 0 off.</li><li><code>r_rainLensMist 0.04</code> — mist reaching the lens when not facing the rain.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> add <code>LensWaterEmitters</code> to <code>cubemaps/&lt;map&gt;/env.json</code> or an EFX <code>lensWater</code> primitive to wet the lens near a waterfall or spray. Scripts can call <code>r_we lenswater splash 1</code>.</p>\n<div class=\"how\"><h3>How it works</h3><p>A small simulation of drops and film produces a distortion map, and the final image is bent, blurred and given reflections through it.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_rainLensDebug 1..9</code> — blur weight, normal, offset, before/after split, drops, film, sticking, transient state, control panel. Forces the effect on.</li><li><code>rainlens_event splash|spray|emerge</code>, <code>rainlens_profile light|rain|heavy|acid|auto</code>, <code>rainlens_clear</code>, <code>rainlens_stats</code> — test commands.</li><li><code>r_rainLensFieldHeight</code>, <code>r_rainLensAgentLimit</code>, <code>r_rainLensPinning</code>, <code>r_rainLensMerge</code>, <code>r_rainLensFilmDecay</code>, <code>r_rainLensWetDecay</code>, <code>r_rainLensHeavyFlow</code> — tuning of resolution, drop budget, sticking, merging, drying and flows.</li><li><code>r_rainLensPBO</code>, <code>r_rainLensMipBlur</code> — compare upload and blur methods for profiling.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-rain-lens.md\" target=\"_blank\" rel=\"noopener\">rend2-rain-lens.md</a>.</p>"
   }
  ],
  "media": []
 },
 "weather/acid-rain": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Acid rain",
  "cvar": "r_acidRainVisuals",
  "sections": [
   {
    "title": "Acid rain",
    "html": "<p>Gives acid rain one consistent sickly tint across streaks, haze, puddles, runoff, wet surfaces and lens water, instead of each using its own color. Only affects looks, not damage.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_acidRainVisuals 0</code> — 1 on.</li><li><code>r_acidRainTint \"0.78 1.00 0.62\"</code> — the tint.</li><li><code>r_acidRainStrength 1</code> — its strength.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_precipitationInfo</code> — print the current precipitation type, intensity and what uses it.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-acid-rain-profile.md\" target=\"_blank\" rel=\"noopener\">rend2-acid-rain-profile.md</a>.</p>"
   }
  ],
  "media": []
 },
 "weather/weather-on-specific-materials-mod-authors": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Weather on specific materials (mod authors)",
  "cvar": "",
  "sections": [
   {
    "title": "Weather on specific materials (mod authors)",
    "html": "<p>Keep a roof wet but free of puddles, a cloth dry of standing water, or an indoor shader completely dry. Without an override every material reacts automatically.</p>\n<p>Add <code>weatherResponse &lt;wetness&gt; [&lt;puddle&gt; [&lt;runoff&gt;]]</code> to a shader or stage, values 0–4, 1 normal:</p>\n<pre><code>weatherResponse 1.3 1 0.3</code></pre>\n<p>A single <code>weatherResponse 0</code> disables all three.</p>\n<h4>Debug</h4>\n<ul><li><code>r_weatherSurfaceDebug 27..31</code> — on/off, wetness, puddle and runoff scale, why a surface is excluded.</li><li><code>r_weatherMaterialList</code> — print the drawn shaders with an override or automatic exclusion.</li></ul>"
   }
  ],
  "media": []
 },
 "weather/rain-culling": {
  "group": "Rain, wet surfaces and lens water",
  "title": "Rain culling",
  "cvar": "r_weatherCull",
  "sections": [
   {
    "title": "Rain culling",
    "html": "<p>Rain is drawn in chunks around the camera; chunks outside the view are skipped. The rain looks the same; on by default.</p>\n<ul><li><code>r_weatherCull 1</code> — 0 draw every chunk.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_weatherDebugChunks 1</code> — show chunk bounds; 2 also prints them.</li></ul>"
   }
  ],
  "media": []
 },
 "water/modern-water": {
  "group": "Water, waves and waterfalls",
  "title": "Modern water",
  "cvar": "r_waterSurface",
  "sections": [
   {
    "title": "Modern water",
    "html": "<p>Stock water is a scrolling texture with a fixed tint. Modern water reflects its surroundings, bends the view of the bottom, is clear where shallow and darker where deep, and gets the brighter reflections at grazing angles that real water has. Stock water and slime are found automatically; nothing has to be edited.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; high reflection quality can make it heavy.</p>\n<ul><li><code>r_waterSurface 0</code> <em>(restart)</em> — 1 on; 0 restores the original water.</li><li><code>r_waterSurfaceSSR 1</code> — weight of on-screen reflections over cubemap reflections; 0 cubemap only. Independent of <code>r_ssr</code>.</li><li><code>r_waterSurfaceRoughness 0.06</code> — smaller gives sharper highlights and reflections.</li><li><code>r_waterSurfaceAbsorption 1</code> — how fast water gets darker with depth (shared with <a href=\"#f/water/underwater-fog\">underwater fog</a>).</li><li><code>r_waterSurfaceReflection 1</code>, <code>r_waterSurfaceRefraction 1</code> — strength of reflection and of bending; 1 physically correct.</li><li><code>r_waterSurfaceNormal 1</code> — strength of small ripples; 0 removes them.</li><li><code>r_waterSurfaceExperimental 0</code> — 1: also treat surfaces with water-like shader names (<code>pool</code>, <code>lake</code>, <code>river</code>…) as water, for maps where detection misses some.</li></ul>\n<p><code>r_waterSurfaceDebug 9</code> compares old and new water side by side.</p>\n<p class=\"mod\"><strong>Mod authors:</strong> water-body and <code>Liquids</code> rules in <code>cubemaps/&lt;map&gt;/env.json</code> choose which surfaces form one body and its look and motion; see the design notes.</p>\n<div class=\"how\"><h3>How it works</h3><p>At map load water surfaces are found from map contents and grouped into bodies. When drawn, reflection (on-screen tracing, falling back to cubemaps) is mixed with refraction by the viewing angle (Fresnel); the depth image behind the water gives its thickness and so how much light it absorbs.</p></div>\n<ul><li><code>r_waterSurfaceIOR 1.333</code> — refractive index of the liquid.</li><li><code>r_waterSurfaceDepthScale 1</code> — scale of the measured water thickness.</li><li><code>r_waterSurfaceDepthReject 1</code> — refraction never shows objects in front of the water.</li><li><code>r_waterSSROptimize 7</code> — reflection speed-ups, add bits: <strong>1</strong> coarser geometry in the reflection pre-pass, <strong>2</strong> skip hidden water, <strong>4</strong> simpler pre-pass shader. 0 turns them off for comparison.</li><li><code>r_waterSSRWaveReprojection 0</code> <em>(restart)</em> — 1: use exact previous-frame wave geometry for frame-averaged reflections.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterSurfaceDebug 1..11</code> — classification, normals, reflection, depth, refraction, roughness; 9 old/new split at <code>r_waterSurfaceSplit 0.5</code>.</li><li><code>r_waterMotionDebug 1..10</code> — water motion and history validity.</li><li><code>r_waterInfo</code> — list modern-water surfaces. <code>r_waterBodies</code> — bodies and profiles. <code>r_waterOverride</code> — inspect or override classification (run without arguments for syntax).</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-bodies.md\" target=\"_blank\" rel=\"noopener\">rend2-water-bodies.md</a>, <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-surface.md\" target=\"_blank\" rel=\"noopener\">rend2-water-surface.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/underwater-fog": {
  "group": "Water, waves and waterfalls",
  "title": "Underwater fog",
  "cvar": "r_volumetricWater",
  "sections": [
   {
    "title": "Underwater fog",
    "html": "<p>Underwater the original game tints the whole screen one color. With this, water, slime and lava are real fog inside <a href=\"#f/fog/volumetric-fog\">volumetric fog</a>: objects fade with their actual distance through the liquid, red light disappears first, sunlight dims with depth, and a moving light pattern (caustics) plays on the bottom. Needs <code>r_volumetricFog 2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium.</p>\n<ul><li><code>r_volumetricWater 0</code> <em>(restart)</em> — which liquids, add: <strong>1</strong> water, <strong>2</strong> slime, <strong>4</strong> lava. 7 for all.</li><li><code>r_volumetricWaterExtinction 0.0014</code> — how fast water hides distant objects.</li><li><code>r_volumetricWaterAlbedo \"0.10 0.45 0.75\"</code> — color of light scattered by water.</li><li><code>r_volumetricWaterColor</code> — relative absorption of red, green and blue; by default red goes first.</li><li><code>r_volumetricWaterCaustics 0.35</code> — strength of the caustic pattern from the sun; 0 off.</li><li><code>cg_underwaterTint 1</code> — the original full-screen tint; skipped automatically for liquids drawn as fog.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Liquid brushes add absorption and scattering to the fog cells inside them. Sun and dynamic light lose strength with the distance they travel through the liquid.</p></div>\n<ul><li><code>r_volumetricWaterAnisotropy 0.75</code> — how strongly water glows toward the light.</li><li><code>r_volumetricWaterSunPath 1</code> — dim sun and dynamic light by the path through the liquid.</li><li><code>r_volumetricWaterSurfaces 1</code> <em>(restart)</em> — also dim sunlight and add caustics on underwater surfaces.</li><li><code>r_volumetricWaterCausticScale 160</code>, <code>r_volumetricWaterCausticSpeed 0.06</code>, <code>r_volumetricWaterCausticFocus 48</code> — caustic size, drift speed, depth of full contrast.</li><li><code>r_volumetricSlimeExtinction 0.005</code>, <code>r_volumetricSlimeAlbedo \"0.25 0.7 0.2\"</code>, <code>r_volumetricSlimeColor \"1.6 0.5 0.9\"</code>, <code>r_volumetricSlimeAnisotropy 0.4</code> — the same for slime.</li><li><code>r_volumetricLavaExtinction 0.02</code>, <code>r_volumetricLavaAlbedo \"0.6 0.15 0.02\"</code>, <code>r_volumetricLavaColor \"0.6 1.2 1.2\"</code>, <code>r_volumetricLavaAnisotropy 0.3</code> — the same for lava (no glow).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_volumetricFogDebug 59..70</code> — liquid density, brushes, camera membership, boundaries, transparency, underwater sun, path length, scattering.</li><li><code>r_liquids</code> — list liquid brushes and profiles.</li><li><code>r_volumetricWaterActive</code> — set by the renderer: which liquids are drawn as fog on this map (read by the game).</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-volumetric-fog.md#underwater-medium-r_volumetricwater\" target=\"_blank\" rel=\"noopener\">rend2-volumetric-fog.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/looking-up-from-underwater": {
  "group": "Water, waves and waterfalls",
  "title": "Looking up from underwater",
  "cvar": "r_waterSnell",
  "sections": [
   {
    "title": "Looking up from underwater",
    "html": "<p>From below, the world above is visible only through a bright circle overhead (Snell's window); at flatter angles the surface turns into a mirror reflecting the underwater scene. Needs modern water.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium.</p>\n<ul><li><code>r_waterSnell 0</code> — 1 on.</li><li><code>r_waterSnellOptics 0</code> <em>(restart)</em> — <strong>0</strong> basic, <strong>1</strong> sunlight passes through the surface, <strong>2</strong> also the atmosphere along the path above.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Light bends when crossing the water surface (Snell's law); beyond a critical angle it can't leave and reflects back (total internal reflection).</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_waterSnellDebug 1..6</code> — crossing direction, internal reflection, reflection strength, ray direction, source.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-realism-followup.md\" target=\"_blank\" rel=\"noopener\">rend2-water-realism-followup.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/waves": {
  "group": "Water, waves and waterfalls",
  "title": "Waves",
  "cvar": "r_waterWaves",
  "sections": [
   {
    "title": "Waves",
    "html": "<p>Coherent waves roll across the water instead of a scrolling texture. Wind can turn and strengthen them outdoors, and shallow water moves less than deep pools. Without <a href=\"#f/water/wave-geometry\">wave geometry</a> the waves change lighting only; the surface itself stays flat.</p>\n<p class=\"cost\"><strong>Cost:</strong> light; medium with wave geometry.</p>\n<ul><li><code>r_waterWaves 0</code> — 1 on.</li><li><code>r_waterWaveAmplitude 1</code>, <code>r_waterWaveLength 1</code>, <code>r_waterWaveSpeed 1</code> — wave height, length and speed.</li><li><code>r_waterWind 0</code> — 1: outdoor waves follow the wind.</li><li><code>r_waterWaveQuality 1</code> — 0, 1, 2: 2, 4 or 8 wave layers.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>A few analytic wave trains are summed to give height and slope. Dispersion makes long waves faster than short ones and all waves slower in shallow water; wind changes are crossfaded.</p></div>\n<ul><li><code>r_waterWaveDispersion 1</code> — <strong>0</strong> all waves the same speed, <strong>1</strong> speed depends on wavelength and depth, <strong>2</strong> physically calibrated (still water only).</li><li><code>r_waterWaveGravity 313.6</code> — gravity for mode 2, in units/s².</li><li><code>r_waterWaveShallow 1</code> — damp waves in shallow bodies.</li><li><code>r_waterWaveMicro 1</code> — fine slope detail, on top of <code>r_waterSurfaceNormal</code>.</li><li><code>r_waterWindScale 1</code> — strength of the wind response.</li><li><code>r_waterWindTransition 4</code> — seconds to blend into a new wind.</li><li><code>r_waterWaveChoppiness 1</code> — reserved; no effect yet.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterWaveDebug 1..10</code> — height, normals, profile, depth mask, displacement, split, multiplier, legacy deform.</li><li><code>r_waterWaveTime -1</code> — freeze waves at a time in seconds; -1 running.</li><li><code>r_waterWindDebug 1..4</code> — wind, current and target direction, blend.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-waves.md\" target=\"_blank\" rel=\"noopener\">rend2-water-waves.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/wave-geometry": {
  "group": "Water, waves and waterfalls",
  "title": "Wave geometry",
  "cvar": "r_waterGeometry",
  "sections": [
   {
    "title": "Wave geometry",
    "html": "<p>Lets waves actually move the water surface, so its outline rises and falls against shores and objects, and the camera crossing the surface follows the moving waterline. Needs modern water.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; dense geometry can be heavy.</p>\n<ul><li><code>r_waterGeometry 0</code> <em>(restart)</em> — 1: split water into a fine mesh.</li><li><code>r_waterTessellation 0</code> <em>(restart)</em> — 1: let the GPU add detail near the camera and less far away (OpenGL 4.3). Faster and finer than the fixed mesh alone.</li><li><code>r_waterTessPixels 96</code> — tessellation: target edge length on screen; smaller is denser and slower.</li><li><code>r_waterGeometryEdge 64</code> <em>(restart)</em> — fixed mesh: longest edge in world units.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The water is subdivided within vertex budgets, and each vertex is moved by the wave height. Waves shorter than the mesh stay in shading only. The waterline uses a small tolerance band so the screen doesn't flicker between above and below water.</p></div>\n<ul><li><code>r_waterGeometryBodyVerts 250000</code>, <code>r_waterGeometryMapVerts 1000000</code> <em>(restart)</em> — vertex budgets per body and per map.</li><li><code>r_waterTessMax 16</code> <em>(restart)</em> — maximum tessellation level (8, 16, 32); the base mesh is that much coarser.</li><li><code>r_waterTessLodDistance 0</code> — distance with the finest detail.</li><li><code>r_waterTessMinEdge 0</code> — shortest edge in world units; 0 lets <code>r_waterTessMax</code> decide.</li><li><code>r_waterlineHysteresis 1.5</code> — the waterline tolerance band.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterGeometryDebug 1</code> — geometry view. <code>r_waterGeometryInfo</code> — meshes and budgets.</li><li><code>r_waterTessForce &lt;level&gt;</code> — force one tessellation level everywhere; 0 by distance.</li><li><code>r_waterlineDebug 1..2</code> — waterline state, and its height graph.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-geometry.md\" target=\"_blank\" rel=\"noopener\">rend2-water-geometry.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/ripples-from-movement-and-impacts": {
  "group": "Water, waves and waterfalls",
  "title": "Ripples from movement and impacts",
  "cvar": "r_waterInteraction",
  "sections": [
   {
    "title": "Ripples from movement and impacts",
    "html": "<p>Shots, falling objects and characters send real ripples across a pool, which spread, reflect from edges and fade. Needs modern water.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium, within memory budgets.</p>\n<ul><li><code>r_waterInteraction 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_waterInteractionStrength 1</code> — strength of all disturbances.</li><li><code>r_waterInteractionQuality 1</code> <em>(restart)</em> — detail: 0 coarse, 1 medium, 2 fine.</li><li><code>r_waterBathymetry 0</code> <em>(restart)</em> — 1: measure the depth under each body, so that with <code>r_waterInteractionVariableDepth 1</code> ripples slow down in shallow water as real waves do.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Each body gets a small grid of water height and vertical speed that is simulated every frame.</p></div>\n<ul><li><code>r_waterInteractionSpeed 1</code>, <code>r_waterInteractionDamping 1</code> — spread speed and damping.</li><li><code>r_waterInteractionMaxBodies 8</code>, <code>r_waterInteractionMaxTexels 524288</code>, <code>r_waterInteractionMemoryMB 16</code> <em>(restart)</em> — budgets.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterInteractionDebug 1..9</code> — domain, mask, height, velocity, slope, sources, foam, sleep, mapping.</li><li><code>r_waterInteractionInfo</code> — fields and budgets. <code>r_waterBathymetryDebug 1</code> — depth-measurement audit.</li><li><code>r_waterImpulse [strength] [radius]</code> — poke the water you are aiming at.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-interaction.md\" target=\"_blank\" rel=\"noopener\">rend2-water-interaction.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/splashes-wakes-and-rain-on-water": {
  "group": "Water, waves and waterfalls",
  "title": "Splashes, wakes and rain on water",
  "cvar": "r_waterSplashes",
  "sections": [
   {
    "title": "Splashes, wakes and rain on water",
    "html": "<p>Jumping in or climbing out throws spray, shots raise splashes, characters leave wakes, and rain dimples pools. Wakes and rain need <a href=\"#f/water/ripples-from-movement-and-impacts\">ripples</a> (<code>r_waterInteraction 1</code>) to show.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium while active.</p>\n<ul><li><code>r_waterSplashes 0</code> — 1: entry, exit and projectile splashes.</li><li><code>r_waterSplashStrength 1</code>, <code>r_waterSplashQuality 1</code> — strength and particle quality (0–2).</li><li><code>r_waterSplashProjectiles 1</code> — splashes from shots and thrown sabers.</li><li><code>r_waterSplashFoam 1</code> — foam left by splashes.</li><li><code>r_waterWakes 1</code> — wakes behind moving characters.</li><li><code>r_waterWakeStrength 1</code> — wake strength.</li><li><code>r_waterRainRipples 1</code> — rain on real water bodies.</li><li><code>r_waterRainRippleQuality 1</code> <em>(restart)</em> — 0, 1, 2.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Game events push impulses into the body's ripple grid and spawn spray particles. Rain adds a statistical sample of its impacts plus fine disturbance.</p></div>\n<ul><li><code>r_waterWakeMinSpeed 35</code>, <code>r_waterWakeFoamSpeed 260</code> — speed where wakes start and where wake foam is full.</li><li><code>r_waterWakeMaxEntities 16</code> — characters making wakes per frame.</li><li><code>r_waterRainRippleDensity 0.22</code> — share of rain drops turned into individual ripples.</li><li><code>r_waterRainRippleImpulse 0.55</code>, <code>r_waterRainRippleMicro 1</code> — their strength and the fine rain roughness.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterSplashDebug 1</code>, <code>r_waterWakeDebug 1</code> — show and print events.</li><li><code>r_waterRainRippleDebug 1..4</code> — impact mask, source, field, energy.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-physical-water-splashes.md\" target=\"_blank\" rel=\"noopener\">rend2-physical-water-splashes.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/flowing-water": {
  "group": "Water, waves and waterfalls",
  "title": "Flowing water",
  "cvar": "r_waterFlow",
  "sections": [
   {
    "title": "Flowing water",
    "html": "<p>Rivers and streams move along their channel, carrying ripples and foam with them. With the flow field, water slows near banks and turns along walls; with impact, it piles up and foams where it runs into a wall. Needs modern water.</p>\n<p class=\"cost\"><strong>Cost:</strong> light; medium with the field and wall impact.</p>\n<ul><li><code>r_waterFlow 0</code> — 1: flowing water on streams the renderer recognizes.</li><li><code>r_waterFlowField 0</code> <em>(restart)</em> — 1: compute flow around banks and obstacles at map load.</li><li><code>r_waterFlowImpact 0</code> <em>(restart)</em> — 1: water piles up, sloshes, foams and wets walls it runs into. Needs the flow field.</li><li><code>r_waterFlowSpeed 1</code> — flow speed.</li><li><code>r_waterFlowDetail 1</code> — how strongly small detail is carried by the flow.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> set flow direction and speed in the water-body data when the automatic guess is wrong; <code>r_waterFlowOverride</code> helps while testing.</p>\n<div class=\"how\"><h3>How it works</h3><p>Each body gets a flow direction, or a flow map computed at load, that moves its surface detail, ripples and foam. Wall impact adds local disturbance and wetness; it is not a full fluid simulation.</p></div>\n<h4>Debug</h4>\n<ul><li><code>r_waterFlowDebug 1..7</code> — direction, speed, confidence, carried detail, field speed, wall impact, distance to shore.</li><li><code>r_waterFlowOverride</code> — inspect or set flow per body.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-flow.md\" target=\"_blank\" rel=\"noopener\">rend2-water-flow.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/foam": {
  "group": "Water, waves and waterfalls",
  "title": "Foam",
  "cvar": "r_waterFoamField",
  "sections": [
   {
    "title": "Foam",
    "html": "<p>White foam where water meets objects, along shores, behind wakes and under waterfalls. It drifts with the flow and fades over time. Needs modern water.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium.</p>\n<ul><li><code>r_waterFoamField 0</code> <em>(restart)</em> — 1: foam that persists and moves.</li><li><code>r_waterIntersectionFoam 0</code> — 1: a foam ring where objects stand in shallow water.</li><li><code>r_waterFoamDecay 0.55</code> — how fast foam fades.</li><li><code>r_waterFoamShoreline 1</code>, <code>r_waterFoamWaterfall 1</code>, <code>r_waterFoamImpact 1</code>, <code>r_waterFoamIntersection 1</code>, <code>r_waterFoamSteepness 0</code> — foam from shores, waterfalls, splashes and wakes, objects, steep waves; 0 turns a source off.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Each body has a small foam texture: sources add foam, flow moves it, and it decays. Objects are found where their depth meets the water surface.</p></div>\n<ul><li><code>r_waterFoamResolution 128</code> <em>(restart)</em> — foam texture size.</li><li><code>r_waterFoamAdvection 1</code>, <code>r_waterFoamDiffusion 0.12</code> — drift with the flow, spreading.</li><li><code>r_waterIntersectionFoamStrength 1</code>, <code>r_waterIntersectionFoamWidth 8</code>, <code>r_waterIntersectionFoamPersistence 1.5</code> — object foam strength, width and fade time.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterFoamDebug 1..3</code> — amount, velocity, sources.</li><li><code>r_waterIntersectionFoamDebug 1</code> — raw object mask. <code>r_waterIntersectionVerify</code> — check object detection.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-intersection-foam.md\" target=\"_blank\" rel=\"noopener\">rend2-water-intersection-foam.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/shorelines": {
  "group": "Water, waves and waterfalls",
  "title": "Shorelines",
  "cvar": "r_waterShoreline",
  "sections": [
   {
    "title": "Shorelines",
    "html": "<p>With wave geometry, the water edge moves up and down the shore. This softens the hard line where water meets ground, calms waves near rigid banks, and leaves a wet band that dries as the water pulls back. Needs <code>r_waterGeometry 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium.</p>\n<ul><li><code>r_waterShoreline 0</code> <em>(restart)</em> — 1 on.</li><li><code>r_waterShoreWetStrength 0.8</code>, <code>r_waterShoreWetPersistence 8</code> — wet band strength and drying time in seconds.</li><li><code>r_waterShoreFoam 1</code> — shore foam (needs the foam field).</li><li><code>r_waterShoreAttenuation 96</code> — distance over which waves recover from a bank.</li><li><code>r_waterShoreContactSoftness 6</code> — width of the soft water edge.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterShoreDebug 1..9</code> — depth, shore distance, wave reduction, contact, wet edges, foam.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-shoreline.md\" target=\"_blank\" rel=\"noopener\">rend2-water-shoreline.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/underwater-light-patterns-caustics": {
  "group": "Water, waves and waterfalls",
  "title": "Underwater light patterns (caustics)",
  "cvar": "r_waterCausticsMode",
  "sections": [
   {
    "title": "Underwater light patterns (caustics)",
    "html": "<p>Rippling bright lines on the bottom of pools, made by waves focusing sunlight. By default a generic pattern is used; mode 2 derives it from the actual waves above. Needs <a href=\"#f/water/underwater-fog\">underwater fog</a> with <code>r_volumetricWaterSurfaces 1</code>; mode 2 also modern water.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium; more light sources cost more.</p>\n<ul><li><code>r_waterCausticsMode 1</code> — <strong>0</strong> off, <strong>1</strong> generic pattern, <strong>2</strong> from the real water surface.</li><li><code>r_waterCausticsLightMask 1</code> <em>(restart)</em> — light sources, add: <strong>1</strong> sun, <strong>2</strong> dynamic lights, <strong>4</strong> baked light.</li><li><code>r_waterCausticsQuality 1</code> — mode 2 detail: 0, 1, 2.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>A curved water surface bends sunlight into bright lines and dark gaps. Mode 2 estimates that from the wave slopes above each underwater point.</p></div>\n<ul><li><code>r_waterCausticsSlope 1</code> — influence of wave slope.</li><li><code>r_waterCausticsFilter 1</code> — blur deeper patterns.</li><li><code>r_waterCausticsMaxDepth 2048</code> — depth where caustics fade out.</li><li><code>r_waterCausticsLocalMaxLights 1</code> — dynamic lights per point.</li><li><code>r_waterCausticsBakedMode 0</code> — baked light: <strong>0</strong> none, <strong>1</strong> from the surface, <strong>2</strong> generic pattern.</li><li><code>r_waterCausticsBakedStrength 0.35</code> — strength on baked light.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterCausticsDebug 1..7</code> — slope, refracted ray, focus, result, projection, attenuation, interaction.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-surface-caustics.md\" target=\"_blank\" rel=\"noopener\">rend2-surface-caustics.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/waterfalls-and-rapids": {
  "group": "Water, waves and waterfalls",
  "title": "Waterfalls and rapids",
  "cvar": "r_waterfall",
  "sections": [
   {
    "title": "Waterfalls and rapids",
    "html": "<p>Falling water breaks up into streaks, turns white where it is aerated, throws mist at the base and churns the pool it falls into. Rapids carry white water downstream. Supported stock waterfalls are recognized automatically; others keep their original look. Needs modern water.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium to heavy with all layers on.</p>\n<ul><li><code>r_waterfall 0</code> <em>(restart)</em> — 1 modern waterfall sheets.</li><li><code>r_waterfallMist 0</code> — 1 spray and mist at the base (best with volumetric fog).</li><li><code>r_waterfallImpact 0</code> — 1 ripples, turbulence and foam where it lands (needs ripples or the foam field).</li><li><code>r_waterWhitewater 0</code> <em>(restart)</em> — 1 white water in rapids and turbulent streams.</li><li><code>r_waterfallQuality 1</code> — 0–2.</li></ul>\n<p class=\"mod\"><strong>Mod authors:</strong> <code>Waterfalls</code> data in <code>cubemaps/&lt;map&gt;/env.json</code> selects sheets and places mist and impact points.</p>\n<div class=\"how\"><h3>How it works</h3><p>Each waterfall gets a profile (small, medium, heavy) that drives its breakup, spray and pool disturbance. White water is carried by the flow and decays.</p></div>\n<ul><li><code>r_waterfallSmallStrength 1</code>, <code>r_waterfallMediumStrength 1</code>, <code>r_waterfallHeavyStrength 1</code> — strength per profile.</li><li><code>r_waterfallWhitewater 1</code>, <code>r_waterfallRefraction 1</code> — aeration brightness, sheet refraction.</li><li><code>r_waterfallGeometry 1</code> <em>(restart)</em> — finer waterfall mesh (visual only).</li><li><code>r_waterfallMistBalance 0.65</code> — 0 spray particles only, 1 full fog mist.</li><li><code>r_waterfallMistDensity 1</code>, <code>r_waterfallMistRadius 1</code> — mist density and size.</li><li><code>r_waterfallMistLens 0</code> — 1: nearby spray wets the <a href=\"#f/weather/water-on-the-camera-lens\">camera lens</a>.</li><li><code>r_waterfallImpactImpulse 1</code>, <code>r_waterfallImpactTurbulence 1</code>, <code>r_waterfallImpactFoam 1</code>, <code>r_waterfallImpactRadius 1</code> — pool push, turbulence, foam, area.</li><li><code>r_waterWhitewaterStrength 1</code>, <code>r_waterWhitewaterThreshold 0.45</code>, <code>r_waterWhitewaterDecay 1.1</code>, <code>r_waterWhitewaterFoam 0.35</code> — white water look, how much turbulence starts it, fade speed, foam left behind.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterfallDebug 1..12</code> — classification, flow, motion, refraction, foam, spray, impacts, wireframe.</li><li><code>r_waterfallMistDebug 1</code>, <code>r_waterfallImpactDebug 1</code> — mist emitters, impact areas.</li><li><code>r_waterWhitewaterDebug 1..4</code> — flow, turbulence, white water, foam sources.</li><li><code>r_waterfalls</code> — list waterfall sheets.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-waterfalls.md\" target=\"_blank\" rel=\"noopener\">rend2-waterfalls.md</a>.</p>"
   }
  ],
  "media": []
 },
 "water/underwater-specks-and-bubbles": {
  "group": "Water, waves and waterfalls",
  "title": "Underwater specks and bubbles",
  "cvar": "r_waterParticles",
  "sections": [
   {
    "title": "Underwater specks and bubbles",
    "html": "<p>Drifting specks and tiny rising bubbles around the camera underwater, so clear and murky water feel different. Decorative only.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_waterParticles 0</code> — 1 on.</li><li><code>r_waterParticlesProfile 0</code> — <strong>0</strong> automatic (from the map, else clean), <strong>1</strong> clean, <strong>2</strong> murky, <strong>3</strong> dirty, <strong>-1</strong> none.</li><li><code>r_waterParticlesDensity 1</code>, <code>r_waterParticlesSize 1</code>, <code>r_waterParticlesSpeed 1</code> — amount, size, drift and rise speed.</li><li><code>r_waterParticlesBubbles -1</code> — share of bubbles 0–1; -1 from the profile.</li><li><code>r_waterParticlesDistance 160</code> — radius of the particle cloud around the camera.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_waterParticlesDebug 1..3</code> — placement, without fog in front, statistics.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-water-particles.md\" target=\"_blank\" rel=\"noopener\">rend2-water-particles.md</a>.</p>"
   }
  ],
  "media": []
 },
 "foliage/recognizing-stock-plants": {
  "group": "Grass, plants and wind",
  "title": "Recognizing stock plants",
  "cvar": "r_autoFoliage",
  "sections": [
   {
    "title": "Recognizing stock plants",
    "html": "<p>Stock plant models aren't marked as plants, so the renderer guesses from their names and structure which parts are leaves, fronds or stems. On its own this changes nothing visible, but <a href=\"#f/foliage/leaf-flutter\">leaf flutter</a>, <a href=\"#f/foliage/ferns-and-fronds\">frond motion</a>, <a href=\"#f/foliage/walking-through-vegetation\">walking through plants</a> and <a href=\"#f/foliage/cutting-plants-with-a-saber\">saber cuts</a> need it.</p>\n<ul><li><code>r_autoFoliage 0</code> — <strong>0</strong> off, <strong>1</strong> only safe matches, <strong>2</strong> also uncertain ones (experimental; may move things that shouldn't).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_autoFoliageDebug 1</code>, <code>2</code> — color recognized surfaces; 2 also uncertain candidates.</li><li><code>r_autoFoliageList [filter]</code> — print recognized plants and their frond skeletons.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-auto-grass.md\" target=\"_blank\" rel=\"noopener\">rend2-auto-grass.md</a>.</p>"
   }
  ],
  "media": []
 },
 "foliage/fuller-grass": {
  "group": "Grass, plants and wind",
  "title": "Fuller grass",
  "cvar": "r_grassCardMode",
  "sections": [
   {
    "title": "Fuller grass",
    "html": "<p>Original grass sprites always turn to face the camera, so they swivel as you walk and look thin from above. This draws each tuft as two or three fixed, crossing cards, so grass stays put and looks like a clump of blades from every angle.</p>\n<p class=\"cost\"><strong>Cost:</strong> medium in dense grass (more geometry and overdraw).</p>\n<ul><li><code>r_grassCardMode 0</code> — <strong>0</strong> original camera-facing sprites, <strong>1</strong> two crossed cards, <strong>2</strong> three cards, <strong>3</strong> three near, two far.</li><li><code>r_grassCardWidth 1</code> — card width.</li><li><code>r_grassCardLodDist 600</code> — mode 3: distance where the third card fades out.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_grassCardDebug 1..6</code> — color by card or direction, force 1/2/3 cards, distance level.</li></ul>"
   }
  ],
  "media": []
 },
 "foliage/wind-through-grass": {
  "group": "Grass, plants and wind",
  "title": "Wind through grass",
  "cvar": "r_foliageWind",
  "sections": [
   {
    "title": "Wind through grass",
    "html": "<p>Original grass sways each tuft in its own small circle, all in step. With this, gusts roll across a field: neighbouring grass leans together and waves of wind travel downwind. Works on stock grass.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_foliageWind 0</code> — 1 on.</li><li><code>r_foliageWindStrength 1</code> — sway strength.</li><li><code>r_foliageWindSpeed 1</code> — gust speed.</li><li><code>r_foliageWindDirection 30</code> — wind direction in degrees (0 = world +X). Plants use the same direction and speed.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_foliageWindDebug 1..4</code> — exaggerated motion, color by bend, freeze time, show large gusts.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-foliage-wind.md\" target=\"_blank\" rel=\"noopener\">rend2-foliage-wind.md</a>.</p>"
   }
  ],
  "media": []
 },
 "foliage/leaf-flutter": {
  "group": "Grass, plants and wind",
  "title": "Leaf flutter",
  "cvar": "r_leafFlutter",
  "sections": [
   {
    "title": "Leaf flutter",
    "html": "<p>Leaves and vines on plant models tremble slightly in the air while trunks and branches stay still. Needs <code>r_autoFoliage 1</code> or <code>2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_leafFlutter 0</code> — 1 on.</li><li><code>r_leafFlutterStrength 1</code> — amplitude; 1 is about 1.5 units.</li><li><code>r_leafFlutterSpeed 1</code> — speed.</li><li><code>r_leafFlutterNormal 0.25</code> — let lighting flicker with the motion; 0 motion only.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_leafFlutterDebug &lt;bits&gt;</code> — 1 exaggerate, 2 freeze, 4 highlight fluttering surfaces, 8 color by displacement.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-leaf-flutter.md\" target=\"_blank\" rel=\"noopener\">rend2-leaf-flutter.md</a>.</p>"
   }
  ],
  "media": []
 },
 "foliage/ferns-and-fronds": {
  "group": "Grass, plants and wind",
  "title": "Ferns and fronds",
  "cvar": "r_plantWind",
  "sections": [
   {
    "title": "Ferns and fronds",
    "html": "<p>Ferns and similar plants bend frond by frond in the wind and swing back naturally after being pushed, instead of moving as one rigid model. Needs <code>r_autoFoliage 1</code> or <code>2</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium (CPU) near many plants.</p>\n<ul><li><code>r_plantWind 0</code> — 1 wind on fronds.</li><li><code>r_plantWindStrength 1</code> — wind strength, 0–4.</li><li><code>r_plantStiffness 1</code> — higher is stiffer, with a faster swing.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>Each recognized plant gets a short chain of segments per frond, simulated on the CPU like a springy rod and used to bend the model and its shadow. Near plants get a full simulation, farther ones a coarse one, distant ones stay still.</p></div>\n<ul><li><code>r_plantSimDistance 1500</code> — full simulation within this distance.</li><li><code>r_plantWindDistance 3000</code> — beyond this, plants don't move.</li><li><code>r_plantSimMaxPlants 96</code> — plants simulated per frame (touched plants always are).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_plantDebug &lt;bits&gt;</code> — 1 frond lines, 2 color by segment, 4 by bone, 8 by contact side, 16 print cost, 32 saber cut steps.</li><li><code>r_plantSimInfo</code> — list simulated plants, cut plants and fallen pieces.</li></ul>"
   }
  ],
  "media": []
 },
 "foliage/walking-through-vegetation": {
  "group": "Grass, plants and wind",
  "title": "Walking through vegetation",
  "cvar": "r_foliageInteraction",
  "sections": [
   {
    "title": "Walking through vegetation",
    "html": "<p>Grass and ferns bend where your body or an NPC touches them and spring back after you pass. Grass you stood on stays a little flattened for a few seconds. Plants need <code>r_autoFoliage</code>; grass works as is.</p>\n<p class=\"cost\"><strong>Cost:</strong> light to medium in dense nearby vegetation.</p>\n<ul><li><code>r_foliageInteraction 0</code> — 1 on.</li><li><code>r_foliageInteractionStrength 1</code> — how far stems bend; 1 is about 45° at full contact.</li><li><code>r_foliageInteractionNPCs 1</code> — 0 only you, 1 also nearby NPCs and players.</li><li><code>r_grassTrample 0.25</code> — how much bend trampled grass keeps; 0 none.</li><li><code>r_grassTrampleRecovery 6</code> — seconds for trampled grass to stand up.</li></ul>\n<div class=\"how\"><h3>How it works</h3><p>The game sends simple capsule shapes for nearby characters. Plants push their frond chains away from them. Grass uses a small texture around the player that stores how far each spot is bent and lets it spring back; near grass is split into curved blades so it can bend smoothly.</p></div>\n<ul><li><code>r_foliageInteractionRadius 1</code> — size of the character shapes.</li><li><code>r_foliageInteractionMaxInteractors 8</code> — characters per frame, you first.</li><li><code>r_grassBendSegments 4</code> — segments per nearby grass blade; 1 flat cards.</li><li><code>r_grassBendLodDist 800</code> — distance where curved blades become flat cards.</li><li><code>r_foliageBendFieldSize 256</code> <em>(restart)</em> — grass bend texture resolution (64, 128, 256).</li><li><code>r_foliageBendFieldExtent 1024</code> — area it covers around you.</li><li><code>r_foliageBendFieldStrength 1</code> — grass bend strength; 0 grass doesn't react.</li><li><code>r_foliageBendFieldRecoveryTime 0.8</code> — seconds for grass to come back.</li><li><code>r_foliageBendFieldDamping 0.6</code> — 1 no overshoot, lower more sway.</li><li><code>r_foliageBendFieldImpulse 1</code> — extra drag when running through.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_foliageInteractionDebug &lt;bits&gt;</code> — 1 shapes, 2 exaggerate, 4 interaction only, 8 contact heat, 16 freeze, 32 player only.</li><li><code>r_foliageBendFieldDebug &lt;bits&gt;</code> — 1 bend overlay, 2 covered area, 4 freeze, 8 exaggerate.</li><li><code>r_foliageBendFieldClear</code> — reset grass.</li><li><code>r_foliageInteractionList</code> — characters sent, grass state, active Force waves.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-foliage-interaction.md\" target=\"_blank\" rel=\"noopener\">rend2-foliage-interaction.md</a>.</p>"
   }
  ],
  "media": []
 },
 "foliage/force-push-and-pull": {
  "group": "Grass, plants and wind",
  "title": "Force push and pull",
  "cvar": "r_foliageForceWaves",
  "sections": [
   {
    "title": "Force push and pull",
    "html": "<p>A Force push or pull, by you or an NPC, sweeps a wave through plants, grass and leaves in front of the caster: away for push, toward for pull, then they settle. Plants behind the caster or out of reach don't move. On by default once <code>r_foliageInteraction 1</code>.</p>\n<p class=\"cost\"><strong>Cost:</strong> light.</p>\n<ul><li><code>r_foliageForceWaves 1</code> — 0 off.</li><li><code>r_foliageForceWaveStrength 1</code> — strength, 0–4.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_foliageInteractionDebug 1</code> — wave front and reach: orange push, magenta pull.</li></ul>"
   }
  ],
  "media": []
 },
 "foliage/cutting-plants-with-a-saber": {
  "group": "Grass, plants and wind",
  "title": "Cutting plants with a saber",
  "cvar": "r_foliageSaberCut",
  "sections": [
   {
    "title": "Cutting plants with a saber",
    "html": "<p>Attacking with a saber cuts fern fronds and plant cards it sweeps through. A partial cut stays attached; a severed piece falls, lies on the ground and sinks away. A blade simply resting in a plant doesn't cut. Cuts last until the map changes. On by default once plants move (<code>r_autoFoliage</code> with <code>r_plantWind 1</code> or <code>r_foliageInteraction 1</code>).</p>\n<p class=\"cost\"><strong>Cost:</strong> medium while cutting, limited by the piece count.</p>\n<ul><li><code>r_foliageSaberCut 1</code> — 0 off.</li><li><code>r_foliageSaberCutPieces 48</code> — fallen pieces at once; 0 makes cut parts vanish.</li><li><code>r_foliageSaberCutPieceTime 20</code> — seconds a piece lies on the ground.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_foliageSaberCutReset</code> — restore all cut plants.</li><li><code>r_foliageSaberCutTest [height] [partial]</code> — cut the nearest plant; height is a fraction, default 0.5.</li><li><code>r_plantDebug 32</code> — recent blade sweeps; red ones cut.</li></ul>"
   }
  ],
  "media": []
 },
 "loading/shader-compilation": {
  "group": "Loading, OpenGL and diagnostics",
  "title": "Shader compilation",
  "cvar": "r_shaderProgramCache",
  "sections": [
   {
    "title": "Shader compilation",
    "html": "<p>Rend2 builds hundreds of shader programs for the video driver. Compiling them all at startup is slow, so this build compiles them on first use, in parallel helper processes, and saves the results on disk for the next start.</p>\n<ul><li><code>r_shaderProgramCache 1</code> <em>(restart)</em> — save compiled programs in <code>glslcache/</code>; later starts and <code>vid_restart</code> skip compiling. Needs driver support; otherwise programs are just compiled.</li><li><code>r_shaderProgramCacheMaxMB 512</code> — cache size limit; least-recently-used programs go first.</li><li><code>r_shaderProgramLazy 1</code> <em>(restart)</em> — compile a program when a map or effect first needs it instead of all at startup. The first appearance of a new effect can hitch briefly until it is cached.</li><li><code>r_shaderCompileProcesses -1</code> — helper processes for compiling: -1 automatic (half the CPU threads, at most 8), 0 compile inside the game.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>glslusage</code> — print which shader variants are used.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-shader-cache.md\" target=\"_blank\" rel=\"noopener\">rend2-shader-cache.md</a>.</p>"
   }
  ],
  "media": []
 },
 "loading/map-loading": {
  "group": "Loading, OpenGL and diagnostics",
  "title": "Map loading",
  "cvar": "r_loadPrefetch",
  "sections": [
   {
    "title": "Map loading",
    "html": "<p>Textures are decoded ahead of time on worker threads, prepared material textures are cached on disk, and several slow load steps are skipped or deferred.</p>\n<ul><li><code>r_loadPrefetch 1</code> — decode the textures a map will need on worker threads while it loads.</li><li><code>r_loadPrefetchMB 512</code> — memory for those textures.</li><li><code>r_loadThreads -1</code> — worker threads: -1 automatic, 0 none.</li><li><code>r_materialDDCObjects 1</code> — cache prepared textures of characters, objects and other models on disk.</li><li><code>r_materialDDCWorld 0</code> — 1: also cache the map's own surface textures.</li><li><code>r_uploadPBO 1</code> — upload textures through a staging buffer.</li><li><code>r_genNormalMapsLazy 0</code> — experimental: with <code>r_genNormalMaps 1</code>, generate normal maps only for surfaces that are actually lit by them.</li><li><code>fs_looseImageIndex 1</code> — Windows: remember which loose image files (outside pk3s) don't exist, so they aren't searched for again.</li><li><code>com_touchMemoryOnLoad 1</code> — SP: 0 skips the step that walks through all game memory at the end of loading.</li><li><code>ui_lazyMenuAssets 1</code> — load menu art only when a menu needs it.</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_loadProfile 1</code> — print nested load timings from engine, renderer and game; nested times overlap, don't add them up.</li><li><code>r_imageChecksums 1</code>, <code>imagechecksums</code> — write a hash of every loaded texture to <code>imagechecksums/&lt;map&gt;.txt</code>, to compare loads.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-map-load.md\" target=\"_blank\" rel=\"noopener\">rend2-map-load.md</a>, <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-material-ddc.md\" target=\"_blank\" rel=\"noopener\">rend2-material-ddc.md</a>.</p>"
   }
  ],
  "media": []
 },
 "loading/modern-opengl": {
  "group": "Loading, OpenGL and diagnostics",
  "title": "Modern OpenGL",
  "cvar": "r_gl43",
  "sections": [
   {
    "title": "Modern OpenGL",
    "html": "<p>On cards with OpenGL 4.3 some work moves to compute shaders: building light lists for <a href=\"#f/lighting/many-dynamic-lights-forward\">Forward+</a> and <a href=\"#f/fog/volumetric-fog\">volumetric fog</a>, and <a href=\"#f/water/wave-geometry\">water tessellation</a>. Features stay the same; older cards fall back automatically.</p>\n<ul><li><code>r_gl43 1</code> <em>(restart)</em> — 0 forces the older paths.</li><li><code>r_forwardPlusCompute 1</code> — build Forward+ light lists on the GPU; 0 on the CPU.</li><li><code>r_volumetricFogLightCompute 1</code> — build volumetric fog light lists on the GPU.</li><li><code>r_volumetricFogLightTile 8</code> — fog cells per side of a light tile (4, 8, 16).</li></ul>\n<h4>Debug</h4>\n<ul><li><code>r_forwardPlusVerify</code>, <code>r_vfogLightVerify</code> — compare GPU and CPU light lists.</li><li><code>r_vfogLightStats</code> — fog light-list sizes and cost.</li></ul>\n<p class=\"notes\">Design notes: <a href=\"https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/rend2-opengl43.md\" target=\"_blank\" rel=\"noopener\">rend2-opengl43.md</a>.</p>"
   }
  ],
  "media": []
 },
 "force/telekinesis": {
  "group": "Force powers",
  "title": "Telekinesis",
  "cvar": "setForceTelekinesis 3",
  "sections": [
   {
    "title": "Telekinesis",
    "html": "<p>Grabs an NPC or an object and lets you move it in space, like Grip in The Force Unleashed. Unlike Force Grip it does no damage to the NPC.</p>\n<ul><li>If you release the NPC or object while moving it, it is thrown in that direction.</li><li>Works together with Push and Pull.</li><li>Physical objects added with <code>add_object</code> can be moved as well.</li><li>You can't move while holding something: the movement keys move the held target.</li></ul>\n<h4>Command</h4>\n<ul><li><code>setForceTelekinesis 3</code></li></ul>"
   }
  ],
  "media": []
 },
 "force/soar": {
  "group": "Force powers",
  "title": "Soar",
  "cvar": "setForceSoar 3",
  "sections": [
   {
    "title": "Soar",
    "html": "<p>Gives more aerial movement, but needs some practice.</p>\n<ul><li>You can stick to a wall, not only push off it.</li><li>Wall runs and wall flips can be started when you touch a wall in mid-air.</li><li>A second jump becomes available after touching a wall (wall runs and wall flips).</li><li>You can hang in the air for a few attacks before landing.</li></ul>\n<h4>Command</h4>\n<ul><li><code>setForceSoar 3</code></li></ul>"
   }
  ],
  "media": []
 },
 "force/deflect": {
  "group": "Force powers",
  "title": "Deflect",
  "cvar": "setforcedeflect 3",
  "sections": [
   {
    "title": "Deflect",
    "html": "<p>Protects you from blaster fire without a saber, in the way Kylo Ren stops a bolt in Episode IX.</p>\n<ul><li>Level 1: freezes a blaster bolt; it disappears afterwards.</li><li>Level 2: you can also move while holding it.</li><li>Level 3: sends the bolt back at the enemy.</li></ul>\n<h4>Command</h4>\n<ul><li><code>setforcedeflect 3</code></li></ul>"
   }
  ],
  "media": []
 },
 "force/chain-lightning": {
  "group": "Force powers",
  "title": "Chain Lightning",
  "cvar": "setForceChainLightning 3",
  "sections": [
   {
    "title": "Chain Lightning",
    "html": "<p>Lightning that holds one NPC for two seconds and passes to the next one after one second. It is also triggered by Force Grip 4 + Lightning, but can be used as a separate power.</p>\n<ul><li>Level 1: up to 2 NPCs.</li><li>Level 2: up to 5 NPCs.</li><li>Level 3: no limit.</li></ul>\n<h4>Commands</h4>\n<ul><li><code>setForceChainLightning 3</code></li><li><code>bind J +force_chain_lightning</code></li></ul>"
   }
  ],
  "media": []
 }
};
