#!/usr/bin/env python3
"""Read generated PNGs, write their manifest and a standalone gallery. Never edits images."""
import hashlib
import html
import json
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ART = ROOT / "assets/mod-008-arcana"


def describe(relative, role):
    path = ROOT / relative
    if not path.is_file():
        return {"path":relative,"role":role,"status":"MISSING"}
    raw = path.read_bytes()
    with Image.open(path) as image:
        image.verify()
    with Image.open(path) as image:
        record = {"path":relative, "role":role, "status":"GENERATED_CONCEPT",
                  "sha256":hashlib.sha256(raw).hexdigest(), "bytes":len(raw),
                  "width":image.width, "height":image.height, "mode":image.mode,
                  "runtime_converted":False, "runtime_loaded":False}
        if "A" in image.getbands():
            record["alpha_range"] = list(image.getchannel("A").getextrema())
        return record


def main():
    cards = json.loads((HERE / "cards.proposed.json").read_text())["cards"]
    records = [describe(c["asset"],c["key"]) for c in cards]
    records += [describe("assets/mod-008-arcana/concepts/"+name,role) for name,role in
                [("frame-v1.png","shared.frame"),("card-back-v1.png","shared.back"),("tarot-icon-v1.png","shared.icon")]]
    missing = [r["role"] for r in records if r["status"] == "MISSING"]
    manifest = {"mod":"MOD-008", "tool":"image_gen.imagegen", "expected_card_art":78,
                "generated_card_art":sum(r["status"] != "MISSING" for r in records[:78]),
                "shared_assets":3,"missing":missing,"runtime_textures":0,
                "image_editing_by_this_script":False,"assets":records}
    (HERE / "assets-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    data = [{"name":c["item_name_en"],"title":c["name_en"],"kind":c["arcana"],"suit":c["suit"],
             "reference":c["ffx_reference"],"effect":c["mechanics_proposed"],
             "image":str((ROOT/c["asset"]).relative_to(ART)),"id":c["id"]} for c in cards]
    payload = json.dumps(data,ensure_ascii=False).replace("<","\\u003c")
    page = r'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Spira: Arcana of the Fayth — 78-card gallery</title>
<style>
:root{color-scheme:dark;--bg:#091823;--panel:#102938;--ink:#efe8d6;--muted:#b3c7cc;--gold:#d3b273}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.55 system-ui,sans-serif}
header,main,footer{max-width:1500px;margin:auto;padding:30px 32px}header{border-bottom:1px solid #38505a}
.eyebrow{color:var(--gold);text-transform:uppercase;letter-spacing:.18em;font-size:12px}h1{font:clamp(28px,4vw,50px)/1.2 Georgia,serif;margin:10px 0}
p{max-width:850px;color:var(--muted)}nav{display:flex;gap:10px;flex-wrap:wrap;margin:18px 0}button,input,select{font:inherit;color:var(--ink);background:var(--panel);border:1px solid #64818b;border-radius:6px;padding:9px 14px}
button{cursor:pointer}button:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid #e3cb87;outline-offset:3px}
button[aria-pressed=true]{background:#294c5c;border-color:var(--gold)}label{display:flex;align-items:center;gap:10px}input{min-width:280px}
.toolbar{display:flex;flex-wrap:wrap;gap:20px;align-items:center}.count{color:var(--gold)}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:30px;margin-top:24px}
.card{border:0;background:transparent;padding:0;text-align:left;align-self:start}.card img{display:block;width:100%;aspect-ratio:2/3;object-fit:contain;border-radius:10px;background:var(--panel)}
.name{display:block;margin-top:10px;font-weight:600;line-height:1.4}.type{display:block;color:var(--muted);font-size:13px;margin-top:3px}
dialog{border:1px solid #7695a0;border-radius:12px;background:var(--bg);color:var(--ink);max-width:1050px;width:95vw;padding:24px}dialog::backdrop{background:#000c}
.detail{display:grid;grid-template-columns:minmax(0,1fr) minmax(230px,.7fr);gap:24px}.detail img{max-height:82vh;max-width:100%;object-fit:contain}h2{font:29px/1.25 Georgia,serif}#close{float:right;margin-bottom:16px}.note{font-size:13px;color:var(--muted)}
@media(max-width:650px){header,main,footer{padding:22px 18px}.grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}.detail{grid-template-columns:1fr}input{min-width:0;width:100%}.toolbar label{width:100%}.detail img{max-height:65vh}.name{font-size:13px}}
</style>
<header><div class="eyebrow">Jarvis-HOOK · MOD-008</div><h1>Spira: Arcana of the Fayth</h1>
<p>78 traditional Tarot identities, interpreted through Spira. Original frame: number above, name below. Court cards carry their traditional title without a printed number.</p>
<p class="note">Concept art and proposed effects. This gallery is a local review artifact; it does not inject menus, textures or effects into Final Fantasy X.</p></header>
<main><div class="toolbar"><label>Find a card <input id="search" type="search" placeholder="Name, number or FFX reference"></label><span class="count" id="count" aria-live="polite"></span></div>
<nav aria-label="Card family"><button data-filter="all" aria-pressed="true">All 78</button><button data-filter="major" aria-pressed="false">Major Arcana</button><button data-filter="wands" aria-pressed="false">Wands</button><button data-filter="cups" aria-pressed="false">Cups</button><button data-filter="swords" aria-pressed="false">Swords</button><button data-filter="pentacles" aria-pressed="false">Pentacles</button></nav>
<section class="grid" id="grid" aria-label="Tarot cards"></section></main>
<dialog id="preview" aria-labelledby="title"><button id="close">Close</button><div class="detail"><img id="art" alt=""><div><div class="eyebrow" id="family"></div><h2 id="title"></h2><p id="reference"></p><h3>Proposed effect</h3><p id="effect"></p><p class="note">One unique copy per save. No character restriction. Major Arcana have the strongest default individual effects. Internal IDs are separate from printed Tarot numbers.</p></div></div></dialog>
<footer class="note">Rider–Waite–Smith ordering · 22 Major Arcana + 56 Minor Arcana · Mode A: two cards · Mode B: two Majors, one Major + two Minors, or three Minors</footer>
<script>
const cards=__DATA__;let filter='all';const grid=document.getElementById('grid'),search=document.getElementById('search'),dialog=document.getElementById('preview');
function show(c){document.getElementById('art').src=c.image;document.getElementById('art').alt=c.title;document.getElementById('title').textContent=c.name;document.getElementById('family').textContent=c.kind==='major'?'Major Arcana':c.suit;document.getElementById('reference').textContent='FFX reference: '+c.reference;document.getElementById('effect').textContent=c.effect;dialog.showModal()}
function render(){grid.replaceChildren();const q=search.value.trim().toLowerCase();const visible=cards.filter(c=>(filter==='all'||c.kind===filter||c.suit===filter)&&c.name.toLowerCase().includes(q));document.getElementById('count').textContent=visible.length+' cards';for(const c of visible){const b=document.createElement('button');b.className='card';b.setAttribute('aria-label','View '+c.name);const img=document.createElement('img');img.loading='lazy';img.src=c.image;img.alt=c.title;const name=document.createElement('span');name.className='name';name.textContent=c.name;const type=document.createElement('span');type.className='type';type.textContent=c.kind==='major'?'Major Arcana':c.suit[0].toUpperCase()+c.suit.slice(1);b.append(img,name,type);b.onclick=()=>show(c);grid.append(b)}}
document.querySelectorAll('[data-filter]').forEach(b=>b.onclick=()=>{filter=b.dataset.filter;document.querySelectorAll('[data-filter]').forEach(x=>x.setAttribute('aria-pressed',String(x===b)));render()});search.oninput=render;document.getElementById('close').onclick=()=>dialog.close();render();
</script></html>'''.replace("__DATA__",payload)
    (ART / "gallery.html").write_text(page)
    print(json.dumps({"generated_cards":manifest["generated_card_art"],"assets":len(records),"missing":missing,"gallery":str(ART/"gallery.html")},indent=2))


if __name__ == "__main__":
    main()
