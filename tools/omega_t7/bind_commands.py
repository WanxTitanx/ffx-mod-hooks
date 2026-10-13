"""Jarvis-HOOK: bind installed Editor-owned Omega commands without writing data."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

KEYS=('darkja','holy','aeroja','firaja','blizzaja','thundaja','waterja','anti_flare','meteor','ultima','bio','silence','slowga','darkness','zombie','mental_break','armor_break','reflect','mighty_guard','regen','hastega','focus','cheer','luck','holy_flare','flare')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bindings',type=Path,required=True);p.add_argument('--kernel',type=Path,action='append',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    source=json.loads(a.bindings.read_text())
    if source.get('schema')!='spira.omega-native-commands.v1' or not source.get('installed'):
        raise ValueError('Installed Editor binding contract required')
    definitions={e['key']:e for e in source['bindings']};expected=None;evidence=[]
    for path in a.kernel:
        data=path.read_bytes();first,previous,last,width=(struct.unpack_from('<H',data,o)[0] for o in (0,8,10,12));start=struct.unpack_from('<I',data,16)[0]
        if (first,previous,width)!=(1,0,92) or start<20 or start+(last+1)*92>len(data):raise ValueError(f'Malformed native command table: {path}')
        records=[]
        for key in KEYS:
            definition=definitions[key];command=definition['command'];index=command&4095
            if command>>12!=6 or index>last:raise ValueError(f'Unresolved {key}')
            row=data[start+index*92:start+(index+1)*92]
            if row[0x1A]!=definition['targetFlags'] or row[0x28]!=definition['formula'] or row[0x2A]!=definition['power'] or row[0x2B]!=definition['hits'] or row[0x2D]!=definition['element']:raise ValueError(f'Editor contract drift: {key}')
            records.append({'key':key,'id':command,'gameplay_hex':row[16:].hex()})
        if expected is not None and expected!=records:raise ValueError(f'Gameplay mismatch across locales: {path}')
        expected=records;evidence.append({'path':str(path.resolve()),'sha256':hashlib.sha256(data).hexdigest()})
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps({'schema':'ffx.omega-t7.v1','enabled':False,'commands':expected},indent=2)+'\n')
    a.output.with_suffix('.sources.json').write_text(json.dumps({'editor':str(a.bindings.resolve()),'editor_sha256':hashlib.sha256(a.bindings.read_bytes()).hexdigest(),'kernels':evidence},indent=2)+'\n')
    print(f'26 approved Omega skills bound across {len(evidence)} tables; activation remains OFF.')

if __name__=='__main__':main()
