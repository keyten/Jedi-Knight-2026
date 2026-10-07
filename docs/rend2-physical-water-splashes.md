# Physical water splashes (`r_waterSplashes`)

Physical water splashes are event-driven and separate from GPU rain impact
splashes. Player and NPC entry/exit use the existing `EV_WATER_TOUCH` and
`EV_WATER_LEAVE` transitions emitted by pmove; footsteps use `EV_FOOTSPLASH`.
Only missiles (including a thrown saber represented as `ET_MISSILE`) use a
client-side water crossing test, because the game does not emit a matching
water-touch event for them.

Each event carries world position, surface normal, incoming velocity, normal
and tangent velocity components, approximate radius, event type, saturated
energy, foam amount and an optional body ID. Cgame leaves the body ID at zero,
so rend2 resolves the body using its authoritative merged water-body data.

Outputs are deliberately layered:

- A directional impulse is stamped into the body-local interaction field.
- The stock `env/water_impact` EFX supplies ballistic spray and its short crown.
  Existing high-speed pmove splashes are not emitted twice.
- Strong entry events inject a temporary, advected foam patch into the blue
  channel of the interaction texture.
- Lens water is emitted only when the event lies inside its small physical
  reach around the current camera. Camera water crossing still uses the
  existing emerge event.

Spray has a 2048-unit distance cutoff, a reduced distant LOD and a 100 ms
quality-dependent budget. The renderer independently caps pending interaction
events at 128.

## Cvars

| Cvar | Default | Purpose |
| --- | ---: | --- |
| `r_waterSplashes` | `0` | Master switch. |
| `r_waterSplashQuality` | `1` | Spray budget: 0 low, 1 medium, 2 high. |
| `r_waterSplashStrength` | `1` | Interaction impulse scale. |
| `r_waterSplashFoam` | `1` | Foam injection scale. |
| `r_waterSplashProjectiles` | `1` | Projectile/thrown-saber crossings. |
| `r_waterSplashDebug` | `0` | Velocity/normal lines and event/body/output diagnostics. |

The interaction wave and foam require `r_waterSurface 1` and
`r_waterInteraction 1` (both latched). Spray remains useful without the
interaction field. Suggested test setup:

```
set r_waterSurface 1
set r_waterInteraction 1
set r_waterSplashes 1
vid_restart
```

Use `t2_rancor` or `t3_hevil` to compare a shallow walk, a jump, and weapon
shots. `r_waterSplashDebug 1` prints calculated energy, resolved body, impulse,
spray count and foam amount.
