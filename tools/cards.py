"""Card text and media for the site.

The graphics cards follow docs/rendering-features/<page>.md: one card per `##`
section, and the modal shows that section as written in the docs. Here each
card only gets its one-line summary and the cvar shown on the card (None for
features without a switch). Sections not listed here are not shown; build.py
reports them so new docs don't go unnoticed.

`MEDIA` holds screenshots and videos per card id (`<page>/<section anchor>`):

    MEDIA = {
        "post-processing/tone-mapping": [
            {"type": "image", "src": "assets/media/tonemap/aces.jpg", "caption": "ACES"},
            {"type": "video", "src": "assets/media/tonemap/compare.mp4"},
            {"type": "youtube", "id": "VIDEO_ID"},
        ],
    }

Cards with no media show empty slots in the modal.
"""

PAGES = [
    ("post-processing", [
        ("Tone mapping", "How the bright HDR image is fitted to the screen: original filmic, ACES or AgX-like.", "r_toneMapMode"),
        ("Brightness and exposure", "Automatic adaptation to dark and bright scenes, or a physical camera with ISO, aperture and shutter.", "r_autoExposure"),
        ("Bloom", "A halo around sabers, lamps and glowing panels that grows with their real brightness.", "r_bloom"),
        ("Color grading", "A color filter for the whole image from a <code>.cube</code> LUT, per map or chosen by the player.", "r_colorGrading"),
        ("Motion blur", "Short streaks behind fast camera turns and moving characters; the HUD stays sharp.", "r_motionBlur"),
        ("Anti-aliasing (SMAA)", "Smooths jagged geometry edges.", "r_smaa"),
        ("Linear lighting (experimental)", "Textures and light are mixed in real light amounts instead of screen color values.", "r_linearLighting"),
        ("Distortion color fringes", "Force push and refractive effects split colors slightly at their edges.", "r_refractionChromaticAberration"),
    ]),
    ("materials", [
        ("Material classification", "Identifies surface types for procedural details; classification alone leaves the picture unchanged.", "r_materialClassification"),
        ("Fine material detail", "Close surfaces gain pits, brushing, grain or weave chosen for their material.", "r_detailTextureSynthesis"),
        ("Stone joints and relief", "Recesses painted stone joints and adds shallow cracks, with optional close-range displacement.", "r_voronoiFracturing"),
        ("Automatic materials from stock textures (Auto PBR)", "Guesses metal, leather, cloth, skin and other materials from stock texture names.", "r_autoPBR"),
        ("Shiny stock models (troopers, droids, weapons)", "Moves the old shiny effect of about 130 stock shaders to per-pixel lighting.", "r_autoPBRConvert"),
        ("Roughness variation", "Glossiness that varies across a texture instead of one value per surface.", "r_autoPBRRoughness"),
        ("Glowing surfaces", "Stock lamps, screens and lava become light sources for bloom and bounced light.", "r_autoEmissive"),
        ("Stable highlights", "Less sparkle and crawling of highlights on fine normal-map detail.", "r_specularAA"),
        ("Matte surfaces", "How rough matte surfaces such as cloth, plaster and stone catch light.", "r_diffuseBRDF"),
        ("Physical light units", "Light and emission in physical units, mostly for mod authors.", "r_pbrPhysical"),
        ("Relief detail (POM)", "Cracks, joints and grooves look recessed and shift correctly as you move.", "r_parallaxMapping"),
        ("Shadows inside relief", "Raised parts of a relief texture cast shadows into its grooves.", "r_pomSelfShadow"),
        ("Relief silhouettes", "Relief that changes the outline of a surface, not only its inside.", "r_pomSilhouette"),
    ]),
    ("lighting", [
        ("Ambient occlusion (GTAO)", "Soft darkening in corners, joints and under objects, more accurate than the original SSAO.", "r_aoMode"),
        ("Many dynamic lights (Forward+)", "Up to 256 dynamic lights instead of 32, each pixel checking only the lights that reach it.", "r_forwardPlus"),
        ("Bounced light (SSGI)", "Sabers, explosions and glowing surfaces light nearby surfaces with a weaker colored bounce.", "r_ssgi"),
        ("Lamp-shaped lights (LTC)", "Rectangle and line lights; stock lamps are detected and give long highlights.", "r_ltcAreaLights"),
        ("Saber light along the blade", "Saber light comes from the whole blade instead of one point.", "r_ltcSaberAreaLights"),
        ("Spot lights (mod authors)", "Cone lights from effects, with shadows and beams in fog.", "r_spotLights"),
        ("Projected patterns (mod authors)", "A spot light projects an image, such as a grate or colored glass, onto walls and fog.", "r_spotLightCookies"),
    ]),
    ("shadows", [
        ("Ray traced shadows", "Triangle-traced sun and dynamic-light shadows, with soft edges and shadow-map fallback coverage.", "r_sunShadowMode 2"),
        ("Sun shadows", "Realtime sun shadows, sharp at contact and softer with distance, without crawling as the camera moves.", "r_forceSun"),
        ("Character sun shadows", "Characters and models get their own sharp shadow layer, combined with softer scenery shadows.", "r_sunShadowModelLayer"),
        ("Contact shadows", "Small sun shadows under feet and behind small props, where shadow maps are too coarse.", "r_contactShadows"),
        ("Dynamic light shadows", "Sabers, explosions and other dynamic lights cast realtime shadows.", "r_dlightMode"),
    ]),
    ("characters", [
        ("Distant offscreen bodies", "Skips distant offscreen NPC bodies and shadows, keeping nearby and cinematic bodies.", "r_characterCull"),
        ("Smoother cinematic characters", "Smooths nearby character outlines in scripted scenes; gameplay has a separate switch.", "r_phongTessellation"),
        ("Light across the body", "Characters are lit at several points of the body or per pixel instead of one point.", "r_entityLightGrid"),
        ("Sun on characters", "Characters are lit from the real sun direction, matching their shadow on the ground.", "r_entitySunLight"),
        ("Ambient light from the surroundings", "Ambient light on characters differs by side, taken from nearby reflections.", "r_diffuseIBL"),
        ("Skin (subsurface scattering)", "Light scatters under the skin, softening faces and hands.", "r_skinSSS"),
        ("Hair and eyes", "Strand-shaped highlights on hair and a wet shine on eyes.", "r_materialModels"),
        ("Softer ragdolls", "Ragdoll joints slow down near their limits instead of stopping dead.", "r_ragSoftLimits"),
        ("Softer animation (SP)", "Limbs ease in and rebound slightly at the end of their movement.", "r_animSoftLimits"),
    ]),
    ("reflections", [
        ("Screen-space reflections (SSR)", "Reflections of what is on screen, combined with cubemaps for the rest.", "r_ssr"),
        ("Rough reflections", "Reflections on rough floors stretch into streaks at shallow angles.", "r_ssrStochastic"),
        ("Fog in reflections", "Reflected objects are as hazy as when you look at them directly.", "r_ssrReflectedFog"),
    ]),
    ("fog", [
        ("Volumetric fog", "Fog lit by the sun, lamps and sabers, with light beams and shadows in it.", "r_volumetricFog"),
        ("Ground haze", "Low mist near the ground that thins with height, on any map.", "r_volumetricFogHeight"),
        ("Uneven fog", "Breaks uniform fog into drifting patches and wisps.", "r_volumetricFogNoise"),
        ("Shadows inside thick fog", "Dense fog and smoke shade themselves: bright toward the light, dark behind.", "r_volumetricSelfShadow"),
        ("Light bouncing inside fog", "Steam and clouds stay bright inside, as light bounces within them.", "r_volumetricMultiScatter"),
        ("Colored fog absorption", "Fog absorbs colors unevenly, so it can tint what is behind it.", "r_volumetricFogRGBExtinction"),
        ("Local fog volumes (mod authors)", "Pockets of steam, smoke or dusty air placed on a map without recompiling it.", None),
        ("Different kinds of fog (mod authors)", "Steam, dust and smoke each scatter light in their own way.", None),
        ("Original Rend2 volumetric fog", "The original Rend2 volumetric fog, lit by baked light only.", "r_volumetricFog 1"),
    ]),
    ("effects", [
        ("Soft smoke contact", "Smoke fades into walls, floors and the near camera, with rounded billboard thickness.", "r_softParticles"),
        ("Smoke and glass ordering", "Depth-orders compatible smoke and glass passes and batches neighboring sprite draws.", "r_transparencySort"),
        ("Volumetric smoke", "Smoke from effects becomes real volume in the fog, lit and shadowed by it.", "r_volumetricParticles"),
        ("Lit smoke sprites", "Ordinary smoke sprites take the light of the place they are in.", "r_particleLighting"),
        ("Glowing gas", "Fog and smoke that glow by themselves.", "r_volumetricEmission"),
        ("Light from sparks and fire", "Sparks, bolts, fire and flashes light the walls and floor around them.", "r_fxLights"),
        ("Heat haze", "Hot air above fire and explosions bends what is behind it; shock waves ripple the scene.", "r_fxDistortion"),
        ("Extra sparks and embers", "Impacts and explosions throw bouncing sparks and drifting embers, simulated on the GPU.", "r_fxGpuParticles"),
        ("Fuller impacts and explosions", "Stock impacts and explosions get the sparks, dust, light and scorch marks they lack.", "r_fxLayering"),
    ]),
    ("sky", [
        ("Atmosphere", "Distant scenery fades into bluish air, and the sky can be computed from the sun.", "r_atmosphere"),
        ("Volumetric clouds", "3D clouds above the map, lit by the sun and drifting with the wind.", "r_clouds"),
        ("Sky portal speed-up", "Sky portal scenes are shaded only where the sky is actually visible.", "r_skyPortalCull"),
    ]),
    ("weather", [
        ("Rain streaks", "Rain lit by its surroundings, with streaks following speed and wind.", "r_rainStreaks"),
        ("Rain haze", "Heavy rain hides the distance; lamps glow through it.", "r_rainVolume"),
        ("Distant rain", "Far rain hands over from single drops to haze.", "r_rainLOD"),
        ("Wet surfaces", "Rain-exposed surfaces darken and turn glossy, each material in its own way.", "r_weatherWetness"),
        ("Wet after the rain", "Surfaces get wet gradually and dry slowly after the rain stops.", "r_weatherWetnessPersistence"),
        ("Puddles", "Puddles form on flat exposed ground, without map changes.", "r_weatherPuddles"),
        ("Rain rings in puddles", "Raindrops make spreading rings in puddles.", "r_weatherPuddleRipples"),
        ("Water running down walls", "Thin streams run down exposed slopes and walls.", "r_weatherRunoff"),
        ("Rain splashes", "Small splashes where drops hit the ground, roofs and puddles.", "r_rainSplashes"),
        ("Water on the camera lens", "Drops, trails and a wet film on the screen in rain or after leaving water.", "r_rainLens"),
        ("Acid rain", "One consistent tint for acid rain across streaks, haze, puddles and lens water.", "r_acidRainVisuals"),
        ("Weather on specific materials (mod authors)", "Per-shader control over wetness, puddles and runoff.", None),
        ("Rain culling", "Rain chunks outside the view are skipped.", "r_weatherCull"),
    ]),
    ("water", [
        ("Modern water", "Water with reflections, refraction and depth-dependent color instead of a scrolling texture.", "r_waterSurface"),
        ("Underwater fog", "Water, slime and lava as real fog: haze by distance, red light lost first, light patterns on the bottom.", "r_volumetricWater"),
        ("Looking up from underwater", "Snell's window overhead and a mirror-like surface at flatter angles.", "r_waterSnell"),
        ("Waves", "Coherent waves across the water that respond to wind.", "r_waterWaves"),
        ("Wave geometry", "Waves move the actual surface, against shores, objects and the camera.", "r_waterGeometry"),
        ("Ripples from movement and impacts", "Shots, objects and characters send ripples that spread and reflect from edges.", "r_waterInteraction"),
        ("Splashes, wakes and rain on water", "Spray when entering or leaving water, splashes from shots, wakes and rain dimples.", "r_waterSplashes"),
        ("Flowing water", "Rivers and streams move along their channel, carrying ripples and foam.", "r_waterFlow"),
        ("Foam", "Foam along shores, around objects, behind wakes and under waterfalls.", "r_waterFoamField"),
        ("Shorelines", "The water edge moves up and down the shore and wets it.", "r_waterShoreline"),
        ("Underwater light patterns (caustics)", "Rippling light lines on the bottom, from the same waves as the surface.", "r_waterCausticsMode"),
        ("Waterfalls and rapids", "Falling water with streaks, white aeration, mist and a churning pool.", "r_waterfall"),
        ("Underwater specks and bubbles", "Drifting specks and small bubbles that make clear and murky water feel different.", "r_waterParticles"),
    ]),
    ("foliage", [
        ("Recognizing stock plants", "Finds leaves, fronds and stems in stock plant models for the features below.", "r_autoFoliage"),
        ("Fuller grass", "Grass as fixed crossed cards that don't swivel with the camera.", "r_grassCardMode"),
        ("Wind through grass", "A shared breeze moves grass in gusts instead of every tuft circling in step.", "r_foliageWind"),
        ("Leaf flutter", "Leaves and vines tremble while trunks and branches stay still.", "r_leafFlutter"),
        ("Ferns and fronds", "Ferns bend frond by frond in the wind and swing back after being pushed.", "r_plantWind"),
        ("Walking through vegetation", "Grass and ferns bend where characters touch them and spring back.", "r_foliageInteraction"),
        ("Force push and pull", "Push and pull send a wave through plants, grass and leaves.", "r_foliageForceWaves"),
        ("Cutting plants with a saber", "Saber attacks cut fern fronds and plant cards.", "r_foliageSaberCut"),
    ]),
    ("loading", [
        ("Rendering fast paths", "Enabled submission and shading paths for BSP surfaces, models, grass and repeated props.", "r_worldSurfaceList"),
        ("Shader compilation", "Shader programs are cached on disk and built only when a level needs them.", "r_shaderProgramCache"),
        ("Map loading", "Textures are decoded ahead on worker threads, and slow load steps are cached or deferred.", "r_loadPrefetch"),
        ("Modern OpenGL", "On OpenGL 4.3, light lists and water tessellation move to compute shaders; older cards fall back.", "r_gl43"),
    ]),
]

# Doc sections deliberately left off the site.
SKIP = {"loading": ["Testing and internal flags"]}

MEDIA = {
    'post-processing/bloom': [{'type': 'image', 'src': 'assets/media/gallery/post-processing/bloom-before.jpg', 'caption': 'Before: Original dynamic glow (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/post-processing/bloom-after.jpg', 'caption': 'After: HDR bloom (yavin1b)'}],
    'water/modern-water': [{'type': 'image', 'src': 'assets/media/gallery/water/modern-water-before.jpg', 'caption': 'Before: Original scrolling water (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/water/modern-water-after.jpg', 'caption': 'After: Modern water surface (yavin1b)'}],
    'foliage/fuller-grass': [{'type': 'image', 'src': 'assets/media/gallery/foliage/fuller-grass-before.jpg', 'caption': 'Before: Original camera-facing sprites (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/foliage/fuller-grass-after.jpg', 'caption': 'After: Fixed crossed grass cards (yavin1b)'}],
    'foliage/wind-through-grass': [{'type': 'image', 'src': 'assets/media/gallery/foliage/wind-through-grass-before.jpg', 'caption': 'Before: Original individual sway (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/foliage/wind-through-grass-after.jpg', 'caption': 'After: Shared wind and gusts, default strength (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/foliage/wind-through-grass-comparison.gif', 'caption': 'Animated comparison: before left, after right (yavin1b)'}],
    'foliage/ferns-and-fronds': [{'type': 'image', 'src': 'assets/media/gallery/foliage/ferns-and-fronds-before.jpg', 'caption': 'Before: Frond wind disabled (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/foliage/ferns-and-fronds-after.jpg', 'caption': 'After: Frond wind at its default strength (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/foliage/ferns-and-fronds-comparison.gif', 'caption': 'Animated comparison: before left, after right (yavin1b)'}],
    'foliage/force-push-and-pull': [{'type': 'image', 'src': 'assets/media/gallery/foliage/force-push-and-pull-before.jpg', 'caption': 'Before: Force push with plant response disabled (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/foliage/force-push-and-pull-after.jpg', 'caption': 'After: Force push with plant response enabled (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/foliage/force-push-and-pull-comparison.gif', 'caption': 'Animated comparison: before left, after right (yavin1b)'}],
    'post-processing/color-grading': [{'type': 'image', 'src': 'assets/media/gallery/post-processing/color-grading-before.jpg', 'caption': 'Before: No color grading (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/post-processing/color-grading-after.jpg', 'caption': 'After: Example warm LUT (yavin1b)'}],
    'fog/ground-haze': [{'type': 'image', 'src': 'assets/media/gallery/fog/ground-haze-before.jpg', 'caption': 'Before: Height fog disabled (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/fog/ground-haze-after.jpg', 'caption': 'After: Height fog enabled (yavin1b)'}],
    'lighting/ambient-occlusion-gtao': [{'type': 'image', 'src': 'assets/media/gallery/lighting/ambient-occlusion-gtao-before.jpg', 'caption': 'Before: Ambient occlusion disabled (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/lighting/ambient-occlusion-gtao-after.jpg', 'caption': 'After: GTAO enabled (yavin1b)'}],
    'post-processing/tone-mapping': [{'type': 'image', 'src': 'assets/media/gallery/post-processing/tone-mapping-before.jpg', 'caption': 'Before: Original Rend2 filmic curve (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/post-processing/tone-mapping-after.jpg', 'caption': 'After: ACES fitted curve (yavin1b)'}],
    'post-processing/brightness-and-exposure': [{'type': 'image', 'src': 'assets/media/gallery/post-processing/brightness-and-exposure-before.jpg', 'caption': 'Before: Exposure compensation 0 (hoth3)'}, {'type': 'image', 'src': 'assets/media/gallery/post-processing/brightness-and-exposure-after.jpg', 'caption': 'After: Exposure compensation +1 stop (hoth3)'}],
    'post-processing/anti-aliasing-smaa': [{'type': 'image', 'src': 'assets/media/gallery/post-processing/anti-aliasing-smaa-before.jpg', 'caption': 'Before: No SMAA (mp/ffa3)'}, {'type': 'image', 'src': 'assets/media/gallery/post-processing/anti-aliasing-smaa-after.jpg', 'caption': 'After: SMAA 1x, ultra quality (mp/ffa3)'}],
    'materials/fine-material-detail': [{'type': 'image', 'src': 'assets/media/gallery/materials/fine-material-detail-before.jpg', 'caption': 'Before: Synthesized detail off (t1_sour)'}, {'type': 'image', 'src': 'assets/media/gallery/materials/fine-material-detail-after.jpg', 'caption': 'After: Synthesized detail on (t1_sour)'}],
    'water/looking-up-from-underwater': [{'type': 'image', 'src': 'assets/media/gallery/water/looking-up-from-underwater-before.jpg', 'caption': 'Before: Plain underside of the surface (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/water/looking-up-from-underwater-after.jpg', 'caption': 'After: Snell/refraction surface (yavin1b)'}],
    'water/waterfalls-and-rapids': [{'type': 'image', 'src': 'assets/media/gallery/water/waterfalls-and-rapids-before.jpg', 'caption': 'Before: Original scrolling waterfall layers (yavin1b)'}, {'type': 'image', 'src': 'assets/media/gallery/water/waterfalls-and-rapids-after.jpg', 'caption': 'After: Modern aerated waterfall sheet (yavin1b)'}],
    "effects/soft-smoke-contact": [
        {"type": "image", "src": "assets/media/particles/floor-before.png", "caption": "Before: flat smoke contact (controlled stock-texture fixture)"},
        {"type": "image", "src": "assets/media/particles/floor-after.png", "caption": "After: rounded billboard contact, same fixture"},
    ],
    "effects/smoke-and-glass-ordering": [
        {"type": "image", "src": "assets/media/particles/glass-before.png", "caption": "Before: late glass reflection over smoke (controlled fixture)"},
        {"type": "image", "src": "assets/media/particles/glass-after.png", "caption": "After: glass passes kept in their depth-ordered packet"},
    ],
    "effects/light-from-sparks-and-fire": [
        {"type": "image", "src": "assets/media/fx/lights-before.jpg", "caption": "Before: the fire lights nothing"},
        {"type": "image", "src": "assets/media/fx/lights-after.jpg", "caption": "After: the walls and floor take its light"},
    ],
    "effects/heat-haze": [
        {"type": "image", "src": "assets/media/fx/distortion-before.jpg", "caption": "Before"},
        {"type": "image", "src": "assets/media/fx/distortion-after.jpg", "caption": "After (r_fxDistortionStrength 2.5): the floor seam wobbles through the heat"},
    ],
    "effects/extra-sparks-and-embers": [
        {"type": "image", "src": "assets/media/fx/gpu-particles-before.jpg", "caption": "Before: stock explosion"},
        {"type": "image", "src": "assets/media/fx/gpu-particles-after.jpg", "caption": "After: embers rising around the fireball"},
    ],
    "effects/fuller-impacts-and-explosions": [
        {"type": "image", "src": "assets/media/fx/layering-before.jpg", "caption": "Before: stock blaster impact"},
        {"type": "image", "src": "assets/media/fx/layering-after.jpg", "caption": "After: added sparks and light"},
    ],
}

FORCE = [
    dict(id="force/telekinesis", title="Telekinesis", color="blue", cvar="setForceTelekinesis 3",
         short="Hold an NPC or an object and move it in space, similar to Grip in The Force Unleashed but without damage. Releasing it while moving throws it. Movement keys move the held target.",
         doc="""Grabs an NPC or an object and lets you move it in space, like Grip in The Force Unleashed. Unlike Force Grip it does no damage to the NPC.

- If you release the NPC or object while moving it, it is thrown in that direction.
- Works together with Push and Pull.
- Physical objects added with `add_object` can be moved as well.
- You can't move while holding something: the movement keys move the held target.

**Command**
- `setForceTelekinesis 3`"""),
    dict(id="force/soar", title="Soar", color="green", cvar="setForceSoar 3",
         short="Stick to walls, start wall runs and flips in mid-air, jump again after touching a wall, and stay in the air for a few attacks.",
         doc="""Gives more aerial movement, but needs some practice.

- You can stick to a wall, not only push off it.
- Wall runs and wall flips can be started when you touch a wall in mid-air.
- A second jump becomes available after touching a wall (wall runs and wall flips).
- You can hang in the air for a few attacks before landing.

**Command**
- `setForceSoar 3`"""),
    dict(id="force/deflect", title="Deflect", color="amber", cvar="setforcedeflect 3",
         short="Stop blaster bolts without a saber. Level 1 freezes a bolt, level 2 lets you move while doing it, level 3 sends it back.",
         doc="""Protects you from blaster fire without a saber, in the way Kylo Ren stops a bolt in Episode IX.

- Level 1: freezes a blaster bolt; it disappears afterwards.
- Level 2: you can also move while holding it.
- Level 3: sends the bolt back at the enemy.

**Command**
- `setforcedeflect 3`"""),
    dict(id="force/chain-lightning", title="Chain Lightning", color="purple", cvar="setForceChainLightning 3",
         short="Lightning that passes from one NPC to the next: up to 2, 5 or any number of targets. Bind it with <code>+force_chain_lightning</code>.",
         doc="""Lightning that holds one NPC for two seconds and passes to the next one after one second. It is also triggered by Force Grip 4 + Lightning, but can be used as a separate power.

- Level 1: up to 2 NPCs.
- Level 2: up to 5 NPCs.
- Level 3: no limit.

**Commands**
- `setForceChainLightning 3`
- `bind J +force_chain_lightning`"""),
]
