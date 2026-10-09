"""Deterministic source/transport contract checks for rapid whitewater."""
import math
from pathlib import Path


def profile_source(profile, speed, depth, slope):
    base = {"slow_stream": 0.025, "fast_stream": 0.58,
            "waterfall": 0.82, "heavy_waterfall": 1.0}.get(profile, 0.0)
    shallow = 1.0 - max(0.0, min(1.0, depth / 64.0)) if depth > 0.0 else 0.0
    factor = max(0.25, min(1.5, 0.35 + 0.45 * speed +
                           0.35 * shallow + 0.65 * slope))
    return base * factor


def main():
    root = Path(__file__).resolve().parents[2]
    update = (root / "shared/rd-rend2/glsl/water_foam_field.glsl").read_text()
    surface = (root / "shared/rd-rend2/glsl/watersurface.glsl").read_text()
    init = (root / "code/rd-rend2/tr_init.cpp").read_text()

    # Profile defaults: slow remains below formation, a fast shallow stream is
    # moderate, and waterfall impact conditions are strongest.
    slow = profile_source("slow_stream", 0.6, 24.0, 0.0)
    fast = profile_source("fast_stream", 1.0, 16.0, 0.0)
    fall = profile_source("waterfall", 1.0, 16.0, 0.4)
    assert slow < 0.1 < fast < fall
    assert fast > 0.45, (slow, fast, fall)

    # A concentration travels downstream and decays when its source stops.
    dt, velocity, decay = 0.1, 0.2, 1.1
    position, concentration = 0.25, 1.0
    for _ in range(10):
        position += velocity * dt
        concentration *= math.exp(-decay * dt)
    assert position > 0.25 and 0.0 < concentration < 0.5

    for name in ("r_waterWhitewater", "r_waterWhitewaterThreshold",
                 "r_waterWhitewaterStrength", "r_waterWhitewaterFoam",
                 "r_waterWhitewaterDecay", "r_waterWhitewaterDebug"):
        assert f'"{name}"' in init, name
    assert "previousUV = clamp(uv - u_WaterFoamFlow.xy" in update
    assert "turbulence *= exp(-u_WaterWhitewaterParams.y * dt)" in update
    assert "out_Color = vec4(concentration, mask, turbulence, whitewaterSource)" in update
    assert "roughness = mix(roughness, 0.78, whitewater)" in surface
    assert "color = mix(color, whitewaterColor, whitewater * 0.72)" in surface
    assert "r_waterWhitewaterDebug" in surface or "whitewaterDebug" in surface
    print("water whitewater contract checks: PASS")


if __name__ == "__main__":
    main()
