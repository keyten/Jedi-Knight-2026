# Water realism follow-up decisions

This note records the implementation boundary of the post-review realism pass.

Implemented behind opt-in controls:

- calibrated physical analytic-wave dispersion (`r_waterWaveDispersion 2`);
- exact body-local liquid depth/confidence fields (`r_waterBathymetry 1`);
- conservative variable-depth interaction physics
  (`r_waterInteractionVariableDepth 1`);
- underwater rough-dielectric solar transmission and eta transport
  (`r_waterSnellOptics 1`), plus atmosphere/sky through the Snell window in
  mode 2.

The default picture and interaction cost remain unchanged. Normalized authored
dispersion remains mode 1; every new data/compute/large visual change is opt-in.

## Deliberately not implemented in this pass

### Per-patch puddle hydrology

The causal model is sound, but a useful implementation must first create a
persistent atlas and draw-time lookup for connected, rain-eligible surface
patches. The current material pass has only world position, the global weather
projection and material relief. A CPU Priority-Flood result with no exact
patch identifier/atlas lookup would collapse back into the rejected global
top-down ambiguity; using procedural noise as the purported basin result would
only rename the existing heuristic. No inactive cvar or fake partial path was
added. The existing height/POM micro-fill remains in place until the complete
surface-patch data path and a stock-map audit can be delivered together.

### Wave-aware temporal reprojection

Implemented as the opt-in, latched `r_waterSSRWaveReprojection`. The renderer
owns a compact previous-rendered geometry snapshot and a separate resolved
interaction-history atlas committed with the water SSR color/geometry/hit
history. Analytic wind transitions retain exact old/target spectra and omega;
waterfall, shoreline/skirt, supported legacy positional deforms and previous
entity transforms use the same vertex path. Solver ping-pong images remain
simulation-only. The shader exposes the same current/previous world and clip
positions needed by a future general velocity integration, but this change
intentionally adds neither a separate water velocity pass nor a placeholder
motion-vector cvar.

### Explicit exclusions

- dynamic rain-occluder overlays: stock maps provide too few meaningful moving
  overhead occluders for their update cost;
- planar reflections: deferred to a later capture/Ultra path because of their
  extra scene rendering cost and limited incremental benefit over water SSR;
- runoff drip particles: high simulation/attachment cost for a small visual
  contribution;
- spatial flow fields: only justified by a confirmed curved-stream/custom-map
  case; uniform body flow remains the supported baseline;
- GL 4.3 compute: the renderer retains its GL 3.2 fragment/FBO path.

Rain impacts remain intentionally split: sparse visible events may enter the
body solver, while high-density rain energy stays in the dedicated statistical
rain field. Waking every large body field for every drop would be less stable
and more expensive without improving the visible scale separation.
