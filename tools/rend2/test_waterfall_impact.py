"""Source-locality and opt-in contract checks for waterfall impact pools."""
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[2]
    water = (root / "shared/rd-rend2/tr_watersurface.cpp").read_text()
    foam = (root / "shared/rd-rend2/glsl/water_foam_field.glsl").read_text()
    docs = (root / "docs/rend2-waterfall-impact.md").read_text()
    for renderer in ("code/rd-rend2/tr_init.cpp", "codemp/rd-rend2/tr_init.cpp"):
        init = (root / renderer).read_text()
        assert '"r_waterfallImpact", "0"' in init
        for name in ("Radius", "Impulse", "Turbulence", "Foam", "Debug"):
            assert f'"r_waterfallImpact{name}"' in init, (renderer, name)

    # The source must resolve against actual interface triangles and retain an
    # unresolved state instead of falling back to an entire liquid brush.
    assert "R_WaterInteractionPointInTriangle" in water
    assert "source.poolBodyId = 0" in water
    assert "explicit WaterfallEmitter required" in water
    assert "WFIMPACT_EXISTING_FX" in water and '"fx_runner"' in water

    # Both outputs reuse local fields. The center is continuously downward;
    # secondary locations are bounded by the configured radius.
    assert "R_WaterfallImpactInteraction(now, fixedDt)" in water
    assert "-impulse * dt" in water
    assert "sqrtf(R_WaterfallImpactRandom(source)) * radius * 0.78f" in water
    assert "WATERFOAMSOURCE_WATERFALL" in water
    assert "source.radius * r_waterfallImpactRadius->value" in water
    assert "sourceGradient" in foam and "radialFlow" in foam

    assert "does not add a general fluid solver" in docs
    print("waterfall impact-pool contract checks: PASS")


if __name__ == "__main__":
    main()
