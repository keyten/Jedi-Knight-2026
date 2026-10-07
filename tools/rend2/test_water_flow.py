"""Deterministic checks for stock directional-water-flow audit decisions."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from water_audit import scroll_to_world


def close(a, b, epsilon=1e-5):
    return all(abs(x - y) <= epsilon for x, y in zip(a, b))


def main():
    # +U scroll moves a feature toward -U in world space. Rotated and mirrored
    # bases must rotate/mirror that velocity rather than treating UV as XY.
    assert close(scroll_to_world([1, 0], [0, 2, 0], [-3, 0, 0]), [0, -2, 0])
    assert close(scroll_to_world([0, 1], [2, 0, 0], [0, -3, 0]), [0, 3, 0])
    assert close(scroll_to_world([0, 1], [2, 0, 1], [0, 3, -2]), [0, -3, 2])

    audit = json.loads((Path(__file__).parents[2] / 'docs' / 'rend2-water-body-stock-audit.json').read_text())
    maps = audit['maps']
    # Opposed breakup layers are deliberately not promoted to physical flow.
    assert all(b['resolved_flow']['source'] == 'none' and
               b['legacy_motion_analysis']['confidence'] == 'ambiguous'
               for b in maps['t2_port.bsp']['bodies'] if 'textures/bespin/water2' in b['shaders'])
    # A visually scrolling material used by known still pools remains still.
    assert all(b['resolved_flow']['source'] == 'none' and
               'suppressed by still_pool' in b['resolved_flow']['decision']
               for b in maps['t3_bounty.bsp']['bodies'])
    # Stock Yavin motion survives UV rotation and resolves separately per body.
    yavin = maps['yavin1.bsp']['bodies']
    assert next(b for b in yavin if 'textures/common/water_yavin2' in b['shaders'])['resolved_flow']['source'] == 'legacy-derived'
    velocities = {tuple(round(x, 2) for x in b['resolved_flow']['velocity'])
                  for b in yavin if 'textures/h_evil/lakewater' in b['shaders']}
    assert len(velocities) == 3, velocities
    # The installed stock set exercises genuinely mirrored UV mappings.
    assert any(s['uv_mirror_sign'] < 0 for m in maps.values() for s in m['surfaces'])
    print('water flow audit checks: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
