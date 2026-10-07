#!/usr/bin/env python3
"""Append an explicitly reviewed batch with source checks and replay protection.

This validates record association and text structure, not translation quality.
Each output has one assigned reviewer; temporary files stay beside that output.
"""
from __future__ import annotations
from collections import Counter
import argparse
import json
import os
from pathlib import Path
import re
import tempfile

TOKEN=re.compile(r'\{(?:CTRL|BYTE):[^{}]+\}')
STATUSES={'accept_legacy','revise','terminology_pending','technical_context_pending','nonlinguistic'}


def read_rows(path: Path):
    if path.stat().st_size>64*1024*1024:raise ValueError('Review file exceeds 64 MiB')
    return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]


def shape(text):
    return (TOKEN.findall(text),text.count('\n'),re.findall(r'\d+',TOKEN.sub('',text)),
            re.findall(r'@[A-Za-z0-9_]+@',text))


def atomic_text(path: Path,text: str):
    path.parent.mkdir(parents=True,exist_ok=True)
    descriptor,name=tempfile.mkstemp(prefix='.'+path.name+'-',suffix='.tmp',dir=path.parent)
    try:
        with os.fdopen(descriptor,'w',encoding='utf-8') as stream:
            stream.write(text);stream.flush();os.fsync(stream.fileno())
        os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)


def append_review(source_path: Path,decisions_path: Path,progress_path: Path,
                  batch: list,expected_count: int,limit: int | None=None):
    paths=[Path(p).resolve() for p in (source_path,decisions_path,progress_path)]
    if len(set(paths))!=3:raise ValueError('Source and output paths must be distinct')
    source_path,decisions_path,progress_path=paths
    source=read_rows(source_path)
    if len({r['uid'] for r in source})!=len(source):raise ValueError('Duplicate source UID')
    if limit is None:limit=len(source)
    if type(limit) is not int or not 0<limit<=len(source):raise ValueError('Invalid assigned limit')
    if type(expected_count) is not int or expected_count<0:raise ValueError('Invalid expected count')
    if not isinstance(batch,list) or not batch or expected_count+len(batch)>limit:
        raise ValueError('Batch crosses the assigned source range')
    current=read_rows(decisions_path) if decisions_path.exists() else []
    progress=json.loads(progress_path.read_text(encoding='utf-8')) if progress_path.exists() else {}
    if not isinstance(progress,dict):raise ValueError('Checkpoint must be an object')
    if len(current)>limit or any(row.get('uid')!=source[i]['uid'] for i,row in enumerate(current)):
        raise ValueError('Existing decisions are not the exact assigned source prefix')
    for i,row in enumerate(batch,expected_count):
        original=source[i]
        if not isinstance(row,dict) or row.get('uid')!=original['uid']:
            raise ValueError(f'Batch UID does not match source index {i}')
        status=row.get('decision');anchor=row.get('source_anchor')
        if status not in STATUSES:raise ValueError(f'Unknown decision for {original["uid"]}')
        english=original['en']
        empty_anchor_ok=english=='' and status in ('nonlinguistic','technical_context_pending')
        if not isinstance(anchor,str) or anchor not in english or (not anchor and not empty_anchor_ok):
            raise ValueError(f'Source anchor mismatch for {original["uid"]}')
        proposed=row.get('proposed_pt_br')
        if proposed is None and status=='accept_legacy':proposed=original.get('pt')
        if proposed is None and status=='nonlinguistic':proposed=english
        if status in ('revise','accept_legacy') and not isinstance(proposed,str):
            raise ValueError(f'Missing candidate for {original["uid"]}')
        if proposed is not None:
            if not isinstance(proposed,str) or shape(english)!=shape(proposed):
                raise ValueError(f'Text structure mismatch for {original["uid"]}')
    end=expected_count+len(batch)
    if len(current)>=end and current[expected_count:end]==batch:
        status='already_applied'
    elif len(current)==expected_count:
        current=current+batch;status='appended'
    else:
        raise ValueError('Stale expected count or conflicting replay; no output changed')
    # Every validation above precedes either write. If a process stops between the
    # two replacements, replay restores the checkpoint without duplicating rows.
    progress.update(reviewed_count=len(current),reviewed=len(current),next_index=len(current),
                    next_line=len(current)+1,last_uid=current[-1]['uid'],total_units=limit,
                    next_uid=source[len(current)]['uid'] if len(current)<limit else None,
                    decision_counts=dict(Counter(r['decision'] for r in current)),
                    status='complete' if len(current)==limit else 'in_progress')
    if 'reviewed_uids' in progress:progress['reviewed_uids']=[r['uid'] for r in current]
    if 'counts' in progress:progress['counts']=progress['decision_counts']
    if 'total' in progress:progress['total']=limit
    if status=='appended':
        atomic_text(decisions_path,''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in current))
    atomic_text(progress_path,json.dumps(progress,ensure_ascii=False,indent=2)+'\n')
    return dict(status=status,reviewed_count=len(current),limit=limit,next_uid=progress['next_uid'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--decisions',type=Path,required=True)
    parser.add_argument('--progress',type=Path,required=True)
    parser.add_argument('--expected-count',type=int,required=True)
    parser.add_argument('--limit',type=int)
    args=parser.parse_args()
    import sys
    try:
        batch=json.load(sys.stdin)
        result=append_review(args.source,args.decisions,args.progress,batch,args.expected_count,args.limit)
        print(json.dumps(result))
    except (OSError,ValueError,KeyError,TypeError) as error:
        parser.exit(1,f'Review batch rejected: {error}\n')


if __name__=='__main__':main()
