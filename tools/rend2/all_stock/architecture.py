#!/usr/bin/env python3
"""Source dependency audit, complementary to numerical invariance tests.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CORE = ROOT / 'shared/rd-rend2/lightrecon'
NUMERICS = ('producer', 'joint', 'uncertainty', 'amplitude_fit', 'point_refine',
            'emitter_shape', 'hidden_candidates', 'sun_fit', 'indirect', 'bake_forward',
            'bake_integral', 'bake_trace', 'candidate_pool', 'shadow_evidence', 'transport_mesh', 'sampling', 'scene_schedule', 'contributions', 'field_basis')


def audit():
    findings = []
    forbidden = re.compile(r'\b(?:r_pbrPhysical|r_forwardPlus|r_volumetricFog|qgl\w*|'
                           r'glBind\w*|GLuint|GLenum|cvar_t|tr_local|physicalPbr|'
                           r'maxShadowMaps|froxelTexture\w*)\b')
    for name in NUMERICS:
        source = (CORE / (name + '.cpp')).read_text()
        for header in re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)', source, flags=re.M):
            if re.search(r'tr_local|qgl|(?:^|/)GL/|SDL|vulkan|d3d', header, re.I):
                findings.append(dict(file=str((CORE / (name + '.cpp')).relative_to(ROOT)), header=header))
        # Search executable declarations/expressions, not comments or diagnostics.
        code = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', source, flags=re.S)
        for match in forbidden.finditer(code):
            findings.append(dict(file=str((CORE / (name + '.cpp')).relative_to(ROOT)), token=match.group()))
    cmake = (CORE / 'CMakeLists.txt').read_text()
    return dict(schema='org.openjk.solver-architecture-audit.v1', modules=list(NUMERICS),
                forbidden_runtime_dependencies=findings,
                dependency_check_pass=not findings and 'target_link_libraries' not in cmake,
                boundary='Producer::Run -> immutable ReconstructedLightingAsset plus data payloads',
                decoder_exception='RendererSettings describes presentation parity; source decoding is independently tested invariant',
                physical_validity='hard geometry/finite/nonnegative constraints; uncertain geometry is not certified free',
                ranking='validation objective + complexity + geometry/material priors; required-block gate first',
                priority_qualification='No independent stock physical-truth oracle; ranking is not a proof of physical configuration',
                uncertainty='runs before publication; remains conditional/unknown without calibration',
                budgets='CPU offline work/memory limits, not renderer resource limits',
                consumer_status='Unavailable; independent consumer gates do not enable renderer features')


if __name__ == '__main__':
    result = audit()
    print(json.dumps(result, indent=2))
    raise SystemExit(not result['dependency_check_pass'])
