"""Dedicated, unbound Workshop host. Loopback only; edits a disposable copy."""
from __future__ import annotations
import argparse
import csv
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import secrets
import time

from bridge import Core, OPS, Request, ROOT, WorkshopError, ability, Policy, Economy
from store import Store, native_gil, native_story

OWNERS = ["Tidus", "Yuna", "Auron", "Kimahri", "Wakka", "Lulu", "Rikku", "Seymour",
          "Valefor", "Ifrit", "Ixion", "Shiva", "Bahamut", "Anima", "Yojimbo", "Cindy", "Sandy", "Mindy"]
AEON_REQUIREMENTS = {8:'Nirvana with its Crest applied',9:'World Champion with its Crest applied',
                     10:'Spirit Lance with its Crest applied',11:'Onion Knight with its Crest applied',14:'Masamune with its Crest applied'}

class Workshop:
    def __init__(self, store: Store, policy: Policy | None = None):
        self.store = store
        self.policy = policy if policy is not None else Policy()
        self.token = secrets.token_urlsafe(32)
        self.pending = None
        with (ROOT.parent / 'mod_004_refinement' / 'ability_ingredient_candidates.tsv').open() as f:
            self.catalog = {int(row['ability_id']): row for row in csv.DictReader(f, delimiter='\t')}
        self.items = {}
        for row in self.catalog.values():
            for prefix in ('base', 'milestone'):
                if row[prefix + '_item_id']:
                    self.items[int(row[prefix + '_item_id'])] = row[prefix + '_item']
        self.items.update({72:'Speed Sphere',73:'Ability Sphere',75:'Attribute Sphere',76:'Special Sphere',
                           78:'Wht Magic Sphere',79:'Blk Magic Sphere',80:'Master Sphere',81:'Lv.1 Key Sphere',
                           82:'Lv.2 Key Sphere',83:'Lv.3 Key Sphere',84:'Lv.4 Key Sphere',95:'Clear Sphere'})
        self.items.update({int(k):v for k,v in json.loads((ROOT/'host/item_names.json').read_text()).items()})
        # Reforge destinations come from the imported snapshot, not browser bytes.
        _, _, initial, _ = self.store.load()
        self.templates = {}
        for piece in initial.pieces:
            n = bytes(piece.native)
            if piece.id and n[4] < 7 and n[5] < 2 and not n[3] & 0x0C:
                key = f'{n[4]}:{n[5]}:{int.from_bytes(n[12:14],"little")}'
                self.templates.setdefault(key, n)

    def piece(self, piece, slot, progression=None):
        if not piece.id:
            return None
        n = piece.native
        aeon = 8 <= n[4] < 18 and n[5] < 2
        aeon_locked = aeon and (progression is None or self.store.core.aeon_access(piece, slot, progression) != 0)
        abilities = []
        for i in range(4 + piece.fifthUnlocked):
            word = ability(piece, i)
            row = self.catalog.get(word - 0x8000)
            abilities.append({'slot':i,'id':word,'instance':str(piece.abilities[i]),
                              'name':row['ability'] if row else 'Empty' if word==255 else f'Ability {word:04X}',
                              'rank':piece.rank if piece.mode==1 and word!=255 else piece.ranks[i],
                              'supported':word==255 or 0x8000 <= word <= 0x8082,
                              'locked':aeon and word==0x807B, 'available':i < n[11] or i==4})
        return {'slot':slot,'id':str(piece.id),'owner':OWNERS[n[4]] if n[4]<18 else 'Special',
                'kind':'Weapon' if n[5]==0 else 'Armor','equipped':n[6]!=255,
                'aeon':aeon,'requirement':AEON_REQUIREMENTS.get(n[4],'Obtain this Aeon'),
                'protected':bool(aeon_locked) if aeon else bool(n[3]&0x0C) or n[4]>6,'capacity':n[11],
                'mode':piece.mode,'rank':piece.rank,'total':sum(a['rank'] for a in abilities),
                'maximum':10*sum(x['id']!=255 and not x['locked'] for x in abilities),
                'fifth':bool(piece.fifthUnlocked),'abilities':abilities,
                'fifth_choices':[0x8000+i for i in self.catalog if self.store.core.can_customize(piece,4,0x8000+i)]}

    def view(self):
        raw, _, s, identity = self.store.load()
        progression = self.store.core.aeon_progress(raw)
        choices = []
        for index in sorted(self.catalog):
            kinds = [name for kind, name in enumerate(('Weapon', 'Armor'))
                     if self.store.core.fifth_cost(kind, 0x8000 + index, self.policy) is not None]
            if kinds:
                choices.append({'id':0x8000+index,'name':self.catalog[index]['ability'],'kinds':kinds})
        return {'revision':s.revision,'identity':identity[:8], 'token':self.token,
                'refinement_mode':self.policy.mode, 'gil':native_gil(raw),
                'pieces':[self.piece(p,i,progression) for i,p in enumerate(s.pieces) if p.id and (not 8<=p.native[4]<18 or progression.obtained & (1<<p.native[4]))],
                'materials':[{'id':i,'name':self.items.get(i,f'Item {i}'),'quantity':q} for i,q in enumerate(s.items) if q],
                'fifth_choices':choices,
                'templates':[{'id':key,'name':f'{OWNERS[n[4]]} · {"Weapon" if n[5]==0 else "Armor"} · style {index+1}'} for index,(key,n) in enumerate(self.templates.items())],
                'pending':False}

    @staticmethod
    def number(data, key, minimum, maximum, default=None):
        value = data.get(key, default)
        if type(value) is not int or not minimum <= value <= maximum:
            raise WorkshopError('Invalid selection: ' + key)
        return value

    def preview(self, data):
        raw, meta, s, _ = self.store.load()
        if data.get('op') not in OPS or data['op'] in ('create','swap','retire'):
            raise WorkshopError('This inventory lifecycle event is not a Workshop operation')
        slot = self.number(data,'slot',0,199)
        if data.get('piece') != str(s.pieces[slot].id) or data.get('revision') != s.revision:
            raise WorkshopError('The inventory changed; select the piece again')
        r = Request();r.op=OPS[data['op']];r.slot=slot;r.pieceId=s.pieces[slot].id;r.revision=s.revision
        r.value=self.number(data,'value',0,65535,0);r.policy=self.number(data,'policy',0,1,0)
        if data['op']=='reforge':
            template=self.templates.get(data.get('template'))
            if template is None:raise WorkshopError('Choose a verified imported model')
            r.gearTemplate[:]=template[:6]+bytes([255])+template[7:]
        if data['op']=='fuse':
            r.other=self.number(data,'other',0,199);r.otherId=s.pieces[r.other].id
            transfers=data.get('transfers')
            if not isinstance(transfers,list) or not 1<=len(transfers)<=2:raise WorkshopError('Choose one or two abilities')
            r.count=len(transfers)
            for i,part in enumerate(transfers):
                r.fromSlots[i]=self.number(part,'from',0,4);r.to[i]=self.number(part,'to',0,4)
        if data['op']=='evolve':r.to[0]=self.number(data,'ability_slot',0,4)
        progression=self.store.core.aeon_progress(raw)
        plan=self.store.core.preview(s,r,Economy(native_gil(raw),self.policy,self.store.core.customize_unlocked(native_story(raw)),progression))
        confirmation=secrets.token_urlsafe(24)
        self.pending=(confirmation,time.monotonic()+300,raw,meta,plan.after,plan.gilDebit,bytes(self.policy))
        random=data['op']=='refine' and plan.policy.mode==2
        displayed=plan.requirements if random else plan.costs
        costs=[{'id':i,'name':self.items.get(i,f'Item {i}'),'amount':n,'have':s.items[i]} for i,n in enumerate(displayed) if n]
        result={'confirmation':confirmation,'costs':costs,'operation':data['op'],
                'development_free_materials':bool(plan.policy.devFreeMaterials),'development_free_gil':bool(plan.policy.devFreeGil),'gil_debit':plan.gilDebit,'requirements_only':random,'gil_cost':plan.gilCost,'gil_have':plan.gilBefore,
                'before':self.piece(s.pieces[slot],slot,progression),'after':self.piece(plan.after.pieces[slot],slot,progression)}
        if random:
            # A random refinement preview discloses price and eligibility, not the
            # selected winner. Cancelling never advances the stored generator.
            result['after']=None;result['random']=True
            result['base_material']={'id':self.policy.baseItem,'amount':self.policy.baseAmount}
        return result

    def confirm(self, data):
        pending=self.pending
        if not pending or data.get('confirmation')!=pending[0] or time.monotonic()>pending[1]:
            raise WorkshopError('Preview expired; review the operation again')
        self.pending=None
        if bytes(self.policy)!=pending[6]:
            raise WorkshopError('Workshop settings changed; review the operation again')
        self.store.commit(pending[2],pending[3],pending[4],pending[5])
        return self.view()

def serve(workshop: Workshop, port: int):
    class Handler(BaseHTTPRequestHandler):
        def send(self,code,body,kind='application/json'):
            raw=json.dumps(body).encode() if kind=='application/json' else body
            self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(raw)))
            self.send_header('Cache-Control','no-store')
            self.send_header('X-Content-Type-Options','nosniff')
            self.send_header('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; frame-ancestors 'none'")
            self.end_headers();self.wfile.write(raw)
        def do_GET(self):
            try:
                if self.path=='/api/state':self.send(200,workshop.view());return
                files={'/':('index.html','text/html; charset=utf-8'),'/app.js':('app.js','text/javascript'),'/style.css':('style.css','text/css')}
                if self.path not in files:self.send(404,{'error':'Not found'});return
                name,kind=files[self.path];self.send(200,(ROOT/'ui'/name).read_bytes(),kind)
            except (OSError,ValueError,KeyError,TypeError) as e:self.send(409,{'error':str(e)})
        def do_POST(self):
            try:
                if self.headers.get('X-Workshop-Token')!=workshop.token:raise WorkshopError('Session expired')
                expected=f'http://127.0.0.1:{self.server.server_port}'
                if self.headers.get('Origin',expected)!=expected:raise WorkshopError('Foreign origin')
                size=int(self.headers.get('Content-Length','0'))
                if not 0<size<=8192:raise WorkshopError('Invalid request size')
                data=json.loads(self.rfile.read(size))
                if self.path=='/api/preview':result=workshop.preview(data)
                elif self.path=='/api/confirm':result=workshop.confirm(data)
                elif self.path=='/api/cancel':workshop.pending=None;result={'cancelled':True}
                else:self.send(404,{'error':'Not found'});return
                self.send(200,result)
            except (OSError,ValueError,KeyError,TypeError) as e:self.send(409,{'error':str(e)})
        def log_message(self,*args):pass
    server=HTTPServer(('127.0.0.1',port),Handler)
    print(f'WORKSHOP http://127.0.0.1:{server.server_port}/ — isolated copy, no game connection',flush=True)
    server.serve_forever()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace',required=True,type=Path)
    parser.add_argument('--import-save',type=Path)
    parser.add_argument('--port',type=int,default=8771)
    parser.add_argument('--refinement-mode',type=int,choices=(1,2),default=2,
                        help='Prototype policy: 2=random (default), 1=whole equipment')
    args=parser.parse_args();store=Store(args.workspace)
    if args.import_save:store.create(args.import_save)
    serve(Workshop(store,Policy(mode=args.refinement_mode)),args.port)
