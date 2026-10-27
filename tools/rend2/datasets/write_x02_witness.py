#!/usr/bin/env python3
"""Collect bounded, non-asset X02 evidence after tests complete. No fitting."""
import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path
from prepare_blind import write


def load(path): return json.loads(Path(path).read_text(encoding='utf-8'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path('build/x02'))
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(); root=a.root
    runtime=load(root/'runtime/result.json'); peak=load(root/'runtime-peak/result.json')
    benchmark=load(root/'benchmarks/result.json')
    maps={}
    for name in ('hoth3','hoth2','mp_ffa3'):
        report=load(root/f'final/{name}-views/coverage.json')
        assert report['all_pages_complete']
        audit=load(root/f'{name}-source-audit.json')
        assert audit['source_sha256']==report['source']['map_sha256']
        source=report['source']; counts=report['groups']['map']['counts']
        runtime_name='mp/ffa3' if name=='mp_ffa3' else name
        assert runtime['maps'][runtime_name]['repeated_bytes_identical']
        assert runtime['maps'][runtime_name]['sha256']==peak['maps'][runtime_name]['sha256']
        for subset in benchmark[name]['subsets']:
            c=subset['partitions']
            subset['admission']=('conditional three-fold input; photometric/model assumptions still required'
                                 if min(c.values())>=32 else 'partition-only/insufficient local folds; use the global map split')
        facts=load(root/f'final/{name}.facts.json')
        facts['fixture_geometry_prior_count']=len(facts.pop('fixture_geometry_priors',[]))
        pages={k:v['counts'] for k,v in report['groups'].items() if k.startswith('page/')}
        hist={int(k):v for k,v in report['encoded_brightness_histogram'].items()}
        total=sum(hist.values()); quantiles={}
        for q in (.1,.25,.5,.75,.9):
            cumulative=0
            for code,n in sorted(hist.items()):
                cumulative+=n
                if cumulative>=q*total: quantiles[str(q)]=code; break
        quantile_counts=[]; previous=-1
        for upper in [quantiles.get(str(q),255) for q in (.1,.25,.5,.75,.9)]+[255]:
            aggregate=Counter()
            for key,value in report['groups'].items():
                if key.startswith('encoded_brightness/') and previous<int(key.split('/')[1])<=upper:
                    aggregate.update(value['counts'])
            quantile_counts.append(dict(encoded_code_exclusive_lower=previous,inclusive_upper=upper,
                                        counts=dict(aggregate),denominator=total,ties='whole encoded-code bins'))
            previous=upper
        maps[name]=dict(source=source,raw_file=runtime['maps'][runtime_name],quality_counts=counts,
                        per_page=pages,facts=facts,encoded_rgb_quantile_thresholds=quantiles,
                        encoded_rgb_quantile_counts=quantile_counts,
                        group_counts={k:v['counts'] for k,v in report['groups'].items() if
                            k.startswith(('surface_type/','style/','height256/','normal/'))},
                        partitions=report['partitions'],grid_partitions=report['grid_partitions'],
                        partition_exclusions=report['partition_exclusions'],
                        scan=load(root/f'final/{name}-scan.json'),prepare=load(root/f'final/{name}-prepare.json'),
                        original_source_audit=audit,subsets=benchmark[name]['subsets'])
    paths=['shared/rd-rend2/lightrecon/offline.h','shared/rd-rend2/lightrecon/offline.cpp',
           'shared/rd-rend2/lightrecon/surfaces.h','shared/rd-rend2/lightrecon/surfaces.cpp',
           'tools/lightrecon_test/lightrecon_dataset_quality.cpp','tools/lightrecon_test/lightrecon_dataset_roi.cpp',
           'tools/lightrecon_test/lightrecon_dataset_tool.cpp','tools/lightrecon_test/lightrecon_offline_test.cpp']
    paths += [str(path).replace('\\','/') for path in Path('tools/rend2/datasets').glob('*.py')]
    hashes={path:hashlib.sha256(Path(path).read_bytes().replace(b'\r\n',b'\n')).hexdigest() for path in sorted(set(paths))}
    gcc=(root/'ctest-gcc-final.log').read_text(); msvc=(root/'ctest-msvc-final.log').read_text()
    blind=(root/'blind-tests-final.log').read_text(); fog=(root/'gpu-fog-final.log').read_text()
    passed=r'^100% tests passed(?:, 0 tests failed)? out of 16\s*$'
    assert re.search(passed,gcc,re.M)
    assert re.search(passed,msvc,re.M)
    assert 'Ran 6 tests' in blind and 'OK' in blind
    assert 'PASS: 22 raster/compute/media shader permutations' in fog and '1/1 passed' in fog
    compiled=load(root/'compiled-fixtures-frozen/result.json')
    assert len(compiled)==12 and compiled['point_room']['atomic_cancel_preserves_destination']
    witness=dict(schema='org.openjk.observation-coverage-x02.v1',maps=maps,
        compiled_fixtures=compiled,
        tests=dict(gcc_cpu='16/16',msvc_cpu='16/16',blind_boundary='6/6',compiled_fixtures='12/12',
                   sp_mp_release='built; see build-msvc-final.log',fog_shader_permutations=22,
                   source_bsp_audits='3/3; unbound NaN UVs reported separately'),
        engine_peak_working_set_bytes=peak['engine_peak_working_set_bytes'],
        standalone_derivation=load(root/'hoth3-derive.json'),
        shader_disabled_path=load(root/'disabled-path/report.json'),source_code_sha256_lf=hashes,
        limitations=['No inverse solver/source recovery or stock source XYZ scores',
                     'UnknownProfile/unknown transfer/primaries/absolute scale on stock maps',
                     'Per-ROI partitions can be empty; geometry segmentation covers only saved ROI',
                     'Untrusted optimizer requires OS filesystem isolation to inputs/',
                     'MP map captured with SP world loader; MP DLL compilation is a separate check',
                     'No new q3map2 bake or gameplay image regression performed by X02'])
    write(a.output,witness)
    print(a.output)


if __name__=='__main__': main()
