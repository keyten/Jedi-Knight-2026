"""Deterministic public proposal replay; does not read source answers."""
import argparse
import json
from pathlib import Path


def verify(a):
    records=[]
    for variant in ('direct','passive_bounce1'):
        for label in ('baseline','legacy_adapter','budgetmatched','new'):
            for shard in range(1,9):
                name=Path(variant)/label/f'search-{shard}.json'
                old=json.loads((a.previous/name).read_text())
                new=json.loads((a.repeat/name).read_text())
                keys=('key','candidates','forward_calls','rays','work')
                equal=all(old[k]==new[k] for k in keys)
                records.append(dict(variant=variant,policy=label,shard=shard,equal=equal))
    result=dict(schema='org.openjk.discovery-x16-repeat/1',checks=records,
        passed=all(r['equal'] for r in records),compared='IDs, parameters, observation support, work, model keys; wall clock excluded',
        private_answers_read=False)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(f"Deterministic replay: {sum(r['equal'] for r in records)}/{len(records)}")
    if not result['passed']:raise SystemExit(1)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('previous','repeat','output'):p.add_argument('--'+name,type=Path,required=True)
    verify(p.parse_args())
