"""Compare an offline Editor-decoded kernel snapshot with UNI-009..040 designs.

The snapshot contains commands, presets and documents; it is produced from the
private kernel by Ability_Command.ReadList and the five regional design catalogs.
No game process is opened and no asset is changed by this checker.
"""
import argparse
import json
from pathlib import Path

# Mechanical requirements from the regional design documents. Exact balance
# values remain free where the design specifies only relative strength.
PHYSICAL = {303,306,307,309,310,311,315,316,318,319,330,335,336,340}
MAGIC = {299,304,305,312,313,320,325,328,332,333,334,338,339}
AOE = {300,302,303,305,316,319,320,325,326,327,331,332,333,338}
ALLIES = {301,322,323,324,329,337}
STATUS = {
    298:{'Protect'},300:{'Darkness','Silence'},301:{'Protect'},302:{'Silence'},
    303:{'Silence'},307:{'BreakArmor'},308:{'Protect','Shell'},311:{'Darkness'},
    313:{'Silence'},314:{'Slow'},317:{'Petrify'},320:{'Poison'},
    321:{'Darkness','Silence','Slow'},322:{'Protect','Shell','Regen'},
    323:{'Poison','Silence','Slow'},324:{'Haste'},326:{'Poison','Silence','Darkness'},
    327:{'Slow'},329:{'Protect','Shell','Regen'},330:{'BreakArmor'},
    331:{'Poison','Silence','Darkness','Slow'},334:{'Death'},336:{'BreakArmor'},337:{'Haste'},
}
BUFFS = {298:({'Cheer','Reflex'},1),300:({'Jinx'},1),324:({'Cheer','Focus'},2),
         329:({'Cheer','Aim','Focus','Reflex','Luck'},2),334:({'Jinx'},5),337:({'Cheer','Focus'},2)}
TARGETS = {'Self':0,'Frontline':1,'Allies':2,'RandomFrontline':3,'RandomAlly':4,'Previous':5}

def flags(value):
    return set(str(value).split(', ')) if value else set()

def audit(snapshot, catalog):
    checks=[]
    def check(condition, scope, requirement):
        checks.append({'scope':scope,'requirement':requirement,'pass':bool(condition)})
    commands={r['row']:r for r in snapshot['commands']}
    check(len(snapshot['commands'])==43 and set(commands)==set(range(298,341)), 'kernel', 'all 43 authored rows exist exactly once')
    docs={d['Fields']['Id']:d['Fields'] for d in snapshot['documents']}
    runtime={c['preset']:c for c in catalog['curses'] if 'preset' in c}
    check(set(docs)=={p['Id'] for p in snapshot['presets']}, 'design', 'every new preset has its regional design document')
    for p in snapshot['presets']:
        identity=p['Id'];r=runtime[identity];d=docs[identity]
        check(r['threat']==d['Threat'],identity,'documented threat tier')
        check([(s['Operand'],s['Name'],s['Target']) for s in r['skills']]==
              [(s['Operand'],s['Name'],TARGETS[s['Target']]) for s in p['Skills']],identity,'ordered commands and exact target-selection policy')
        for lower,upper in [('damageOnly','DamageOnly'),('replayNative','ReplayNative'),('forbiddenField','ForbiddenField')]:
            check(r[lower]==p[upper],identity,upper+' agrees with the authored recipe')
        check(r['requiredMonster']==p['RequiredMonster'],identity,'signature monster restriction')
        check(bool(p['Skills']) or identity in ('UNI-010','UNI-040'),identity,'only native replay and damage-triggered Zombie omit an added skill')
        for s in p['Skills']:
            check(s['Operand']>>12==6 and commands[s['Operand']&4095]['name']==s['Name'],identity,'own monmagic2 command '+hex(s['Operand']))
    for index,c in commands.items():
        scope=f'{index}: {c["name"]}';status=STATUS.get(index,set())
        check(set(c['chance'])==status,scope,'only documented status effects')
        check(set(c['duration'])<=status,scope,'no leftover status duration')
        if index in PHYSICAL:
            check(c['DamageFormula']=='Normal' and 'Physical' in flags(c['DamageFlgs']) and c['DamageTypeFlgs']=='Hp',scope,'physical HP damage')
        elif index in MAGIC:
            check(c['DamageFormula']=='Magic' and 'Magical' in flags(c['DamageFlgs']) and c['DamageTypeFlgs']=='Hp',scope,'magic HP damage')
        elif index==323:
            check(c['DamageFormula']=='Healing' and {'Heals','CleansesStatuses'}<=flags(c['DamageFlgs']),scope,'heal and cleanse in one command')
        elif index in (314,327):
            check(c['DamageFormula']=='TargetTickCounter' and c['DamageTypeFlgs']=='Ctb',scope,'native Slow CTB behavior without HP damage')
        else:
            check(c['DamageFormula']=='NoDamage' and c['DamageTypeFlgs']==0,scope,'support/status command has no damage')
        check(c['AttackPower']>0 if index in PHYSICAL|MAGIC|{323,314,327} else c['AttackPower']==0,scope,'damage versus support potency')
        target=flags(c['TargetFlgs'])
        check(('Multi' in target)==(index in AOE|ALLIES),scope,'single-target versus group scope')
        check(('Enemies' in target)==(index not in ALLIES|{298,308}),scope,'enemy versus allied side')
        check(('SelfOnly' in target)==(index==298),scope,'Sand Mantle self target')
        check(flags(c['ElementFlgs'])==({'Water'} if index==333 else set()),scope,'documented element, including non-elemental Gagazet spells')
        check(flags(c['Misc2Flgs'])==({'AbsorbDamage'} if index in (299,328) else {'DelayS'} if index in (300,309,327,333) else set()),scope,'drain and Delay occur only where specified')
        buffs,value=BUFFS.get(index,(set(),0))
        check(flags(c['StatBuffFlgs'])==buffs and c['StatBuffValue']==value,scope,'exact stat-buff set and stack count')
        check(c['StatusFlgs']==0 and c['SpecialBuffFlgs']==0,scope,'no extra status/special payload inherited from donors')
    check(set(commands[321]['chance'].values())=={50},'UNI-023','three independent 50-percent status chances')
    check(commands[304]['AttackPower']>commands[305]['AttackPower'],'UNI-015','single-target strike exceeds the following party strike')
    check(commands[318]['AttackPower']>commands[319]['AttackPower'],'UNI-022','single-target fist exceeds the following quake')
    check(all(commands[n]['AttackPower']<commands[330]['AttackPower'] for n in (335,336,340)),'T6 physical','each paired strike stays below the T5 physical singularity')
    check(commands[338]['AttackPower']<commands[332]['AttackPower'],'UNI-038','paired magic strike stays below the T5 magic singularity')
    check((commands[315]['Anim1Id'],commands[315]['Anim2Id'],commands[315]['CasterAnimId'])==(811,811,1),'UNI-020','Rhythm Impact retains its own command/visual while selecting physical caster motion')
    check((commands[318]['Anim1Id'],commands[318]['Anim2Id'],commands[318]['CasterAnimId'])==(814,814,10),'UNI-022','Demolishing Fist retains its own command/visual with the native Blast Punch caster')
    check((commands[309]['Anim1Id'],commands[309]['Anim2Id'],commands[309]['CasterAnimId'])==(805,876,23),'UNI-017','Dragging Strike keeps its own cast and separately owned impact with the native Delay Attack caster')
    return checks

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--snapshot',type=Path,required=True)
    p.add_argument('--catalog',type=Path,default=Path(__file__).with_name('catalog.json'))
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();checks=audit(json.loads(a.snapshot.read_text()),json.loads(a.catalog.read_text()))
    failed=[c for c in checks if not c['pass']]
    a.report.write_text(json.dumps({'checks':len(checks),'passed':len(checks)-len(failed),'failures':failed,'details':checks},indent=2)+'\n')
    print(f'SIN_SKILL_DESIGN_AUDIT {len(checks)-len(failed)}/{len(checks)} passed')
    for f in failed: print(f'{f["scope"]}: {f["requirement"]}')
    raise SystemExit(bool(failed))
