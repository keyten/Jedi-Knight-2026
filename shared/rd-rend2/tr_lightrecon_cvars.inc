// Intentionally no include guard: one registration/declaration list for SP/MP.
// Every entry defaults to 0, is archived and live (no new permutations/resources).
// Build policy is sampled ONLY at the next map load/reload, not on cvar changes.
LIGHTRECON_CONTROL(r_lightReconBuild, 3, "Canonical lighting asset: 0 off, 1 cache only, 2 cache or build, 3 force build; next map load/reload (vid_restart alone does not request work)")
LIGHTRECON_CONTROL(r_lightReconUse, 1, "Master use of canonical reconstructed lighting; live, requires each consumer opt-in, data and an available adapter")
LIGHTRECON_CONSUMER(r_lightReconFog, Fog, "Use canonical incoming radiance for fog; live request, requires a prepared field and available fog adapter")
LIGHTRECON_CONSUMER(r_lightReconEntities, Entities, "Use canonical irradiance for entities/Ghoul2; live request, requires prepared irradiance and an available entity adapter")
LIGHTRECON_CONSUMER(r_lightReconSpecular, Specular, "Use canonical incoming radiance for specular; live request, requires a prepared field and available specular adapter")
LIGHTRECON_CONSUMER(r_lightReconShadows, Shadows, "Use canonical emitters and visibility for shadows; live request, requires prepared visibility and an available shadow adapter")
LIGHTRECON_CONSUMER(r_lightReconSun, Sun, "Use the unique resolved canonical sun; live request, requires known emission and an available sun adapter")
LIGHTRECON_CONSUMER(r_lightReconParticles, Particles, "Use canonical incoming radiance for particles; live request, requires a prepared field and available particle adapter")
LIGHTRECON_CONSUMER(r_lightReconIndirect, Indirect, "Use canonical indirect irradiance; live request, requires prepared irradiance and an available indirect adapter")
LIGHTRECON_CONSUMER(r_lightReconWorldDiffuse, WorldDiffuse, "Use canonical physical emitters for world diffuse; live request, requires usable sources and an available world adapter")
LIGHTRECON_CONSUMER(r_lightReconArea, Area, "Use canonical rectangle/tube emitters via area/LTC lighting; live request, requires usable area sources and an available area adapter")
LIGHTRECON_CONSUMER(r_lightReconEnvironment, Environment, "Use the unique resolved canonical sky/environment for IBL; live request, requires prepared incoming radiance and an available environment adapter")
