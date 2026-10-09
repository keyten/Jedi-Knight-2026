#!/usr/bin/env python3
"""Static contract checks for the shallow-water object intersection path."""

from pathlib import Path
from typing import Tuple
import math


ROOT = Path(__file__).resolve().parents[2]


def require(path: str, snippets: Tuple[str, ...]) -> None:
    text = (ROOT / path).read_text(encoding="utf-8")
    missing = [snippet for snippet in snippets if snippet not in text]
    if missing:
        raise AssertionError(f"{path}: missing {missing}")


def main() -> None:
    require("shared/rd-rend2/glsl/watersurface.glsl", (
        "float WaterIntersectionFoam",
        "bandConfidence * normalConfidence * depthConfidence",
        "intersectionMask * u_Water[35].z * u_Water[19].w",
        "interactionField.b * u_Water[19].w + intersectionFoam",
        "u_Water[35].w > 0.5",
    ))
    require("shared/rd-rend2/tr_watersurface.cpp", (
        "RB_WaterIntersectionInject",
        "qglReadPixels(x0, y0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT",
        "sim.intersectionFoam.swap(sim.intersectionNext)",
        "flowX * didx + flowY * didy",
        "r_waterIntersectionFoamPersistence",
    ))
    for path in ("code/rd-rend2/tr_init.cpp", "codemp/rd-rend2/tr_init.cpp"):
        require(path, (
            '"r_waterIntersectionFoam", "0"',
            '"r_waterIntersectionFoamWidth", "8"',
            '"r_waterIntersectionFoamStrength", "1"',
            '"r_waterIntersectionFoamPersistence", "1.5"',
            '"r_waterIntersectionFoamDebug", "0"',
        ))

    def confidence(distance: float, normal_dot: float, derivative: float,
                   width: float = 8.0) -> float:
        band = max(0.0, min(1.0, 1.0 - abs(distance) / width))
        crossing = 1.0 - abs(normal_dot)
        normal = max(0.0, min(1.0, (crossing - 0.15) / 0.55))
        depth = 1.0 - max(0.0, min(1.0, (derivative - width * 2.0) /
                                      (width * 6.0)))
        return band * normal * depth

    assert confidence(0.0, 0.0, 2.0) > 0.95, "vertical contact was rejected"
    assert confidence(0.0, 1.0, 2.0) == 0.0, "shallow floor became foam"
    assert confidence(20.0, 0.0, 2.0) == 0.0, "far geometry became foam"
    assert math.isclose(math.exp(-1.5 / 1.5), math.exp(-1.0), rel_tol=1e-6)
    print("water intersection foam contract checks: PASS")


if __name__ == "__main__":
    main()
