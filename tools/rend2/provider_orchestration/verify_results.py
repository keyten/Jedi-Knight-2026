#!/usr/bin/env python3
"""Validate the public non-vacuous outcome witness without licensed assets."""
import argparse
import json
from pathlib import Path


def verify(root):
    controlled=json.loads((root/'controlled.json').read_text())
    assert not controlled['failures']
    cases={c['name']:c.get('report') for c in controlled['cases']}
    def provider(report,name):
        return next(p for p in report['providers'] if p['name']==name)
    local=cases['local_lamp_unknown_sun']
    assert local['state']=='Completed' and local['heldout_interval_mse']<local['heldout_black_mse']
    assert local['sun_evidence']=='UnknownSun' and local['resolved_sources']==0
    assert provider(local,'solar.fit')['outcome']=='NoSupportedEvidence'
    budget=cases['optional_local_provider_budget']
    assert budget['state']=='Completed'
    p=provider(budget,'hidden')
    assert p['outcome']=='BudgetExceeded' and p['budget_scope']==1 and not p['search_complete']
    hidden=cases['hidden_with_solar_validation_unavailable']
    assert hidden['state']=='Completed'
    assert provider(hidden,'hidden')['evidence_available']
    assert provider(hidden,'solar.validation')['outcome']=='NoSupportedEvidence'
    assert cases['positive_sky_no_local_lamp']['conditional_sources']==1
    assert len(cases['exclusive_solar_scopes']['scenarios'])==2
    for report in cases.values():
        if not report:
            continue
        assert not any(p['exact_lighting_admissible'] for p in report['providers'])
        if report.get('resolved_sources') is not None:
            assert report['resolved_sources']==0
    stock=json.loads((root/'stock-t1-danger.json').read_text())
    assert not stock['physical_recovery_verified'] and stock['source_xyz_error'] is None
    for p in stock['profiles'].values():
        assert p['joint_state']!='NotStarted' and p['work']['models']>0
        assert not p['producer']['asset_available']
        assert p['producer']['heldout_interval_mse'] is None
        assert provider(p['producer'],'solar.fit')['outcome']=='NoSupportedEvidence'
    manifest=json.loads((root/'manifest.json').read_text())
    assert manifest['canonical_schema']==5 and manifest['controlled_repeated_identical']
    print('Optional absence/local budget, positive controls, exclusive scopes and honest stock failure verified')


if __name__=='__main__':
    p=argparse.ArgumentParser(__doc__)
    p.add_argument('root',type=Path,nargs='?',default=Path(__file__).parent/'results/v1')
    verify(p.parse_args().root)
