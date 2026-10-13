"""Jarvis-HOOK: compare parsed effective encounters with exact runtime pairs."""
from pathlib import Path
import argparse
import csv
import json

REGIONS = {'kami':'Thunder Plains', 'mcfr':'Macalania Forest',
           'maca':'Lake Macalania', 'mcyt':'Macalania Temple', 'bika':'Bikanel',
           'azit':'Home', 'hiku':'Airship', 'bvyt':'Bevelle', 'stbv':'Bevelle',
           'nagi':'Calm Lands / Stolen Fayth', 'lmyt':'Stolen Fayth chamber',
           'mtgz':'Mt. Gagazet', 'zkrn':'Zanarkand approach', 'dome':'Zanarkand Dome',
           'omeg':'Omega Ruins', 'sins':'Inside Sin', 'ssbt':'Inside Sin events',
           'zzzz':'Monster Arena / special formations'}

def audit(catalog, tables):
    pairs = {(f,m['monster']) for m in catalog['monsters'] for f in m['fields']}
    fields = {f for a in catalog['areas'] for f in a['fields']}
    excluded = {e['id'] for e in catalog['excluded']} | set(range(334, 345))
    rows = []
    for t in tables:
        potential = [f for f in t['formations'] if f['Danger'] > 0]
        active = [f for f in potential if f['Weight'] > 0]
        missing = [f['battle'] for f in active if f['missing']]
        names = {m['id']: m['name'] for f in active for m in f.get('monsters') or []}
        known = {m['id'] for f in potential for m in f.get('monsters') or []}
        covered = sorted(m for m in names if (t['field'], m) in pairs)
        uncovered = sorted(m for m in names if (t['field'], m) not in pairs and m not in excluded)
        deliberate = sorted(m for m in names if m in excluded and (t['field'], m) not in pairs)
        invalid = sorted(m for f,m in pairs if f == t['field'] and m not in known)
        if invalid:
            raise ValueError(f"Configured field/model pairs lack effective encounter evidence: {t['field']}/{invalid}")
        if missing:
            state = 'missing effective formation evidence'
        elif not active:
            state = 'event/boss-only; no ordinary random group'
        elif t['Map'].startswith('kami'):
            state = 'intentionally outside SIN progression'
        elif t['field'] not in fields:
            state = 'no reviewed regional UNI/model pool yet'
        elif uncovered:
            state = 'reviewed models covered; special/unreviewed models remain'
        else:
            state = 'all reviewed ordinary models covered'
        fmt = lambda ids: '|'.join(f"m{i:03} {names[i]}" for i in ids)
        region=REGIONS.get(t['Map'][:4],t['Map'][:4])
        if t['field'] in (611,612,613):region='Calm Lands'
        elif t['field']==610:region='Home'
        rows.append({'region':region,
                     'native_field_id':t['field'], 'map_name':t['Map'],
                     'encounter_block_owner':t['canonical'], 'data_offset':t['DataOffset'],
                     'formation_offset':t['FormationOffset'], 'active_formations':len(active),
                     'covered_models':fmt(covered), 'unreviewed_models':fmt(uncovered),
                     'explicit_exclusions':fmt(deliberate), 'coverage':state,
                     'missing_files':'|'.join(missing)})
    return sorted(rows, key=lambda r:r['native_field_id'])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--encounters',required=True,type=Path)
    parser.add_argument('--catalog',type=Path,default=Path(__file__).parent/'catalog.json')
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    rows=audit(json.loads(args.catalog.read_text()),json.loads(args.encounters.read_text()))
    if not rows:raise ValueError('No progression maps in the evidence')
    with args.output.open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]),lineterminator='\n');writer.writeheader();writer.writerows(rows)
    print(f"SIN coverage: {len(rows)} native field tables; aliases resolved through shared data/formation offsets.")

if __name__=='__main__':main()
