#!/usr/bin/env python3
"""
waage_log_web.py - Live-Log der FilaStation-Waage im Browser.

Zeigt ~/waage.log (das Log von waage.py) als eigene Seite, wie /log/live der
Spoolman-Waage: grosse Schrift (A-/A+), Farben, Filter, Live/Pause, Kopieren.
Liest nur die Logdatei; waage.py selbst bleibt unveraendert.

Aufruf:   python3 waage_log_web.py            (Port 8090)
Seite:    http://filastation-waage.local:8090/
Optionen: WAAGE_LOG=/pfad/zur/waage.log  WAAGE_LOG_PORT=8090
"""
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

LOG = os.environ.get("WAAGE_LOG") or os.path.expanduser("~/waage.log")
PORT = int(os.environ.get("WAAGE_LOG_PORT") or 8090)
FIRST_BYTES = 256 * 1024   # beim Oeffnen: das Ende der Datei, nicht alles
MAX_BYTES = 512 * 1024     # pro Abfrage hoechstens so viel

PAGE = r"""<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FilaStation-Waage &middot; Live-Log</title><style>
:root{--bg:#111418;--bar:#1b2027;--fg:#d8dee6;--dim:#7d8794;--ok:#5fd38d;--warn:#f0b54a;--err:#ff6b6b;--hl:#3b82f6}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font-family:system-ui,sans-serif}
#bar{position:sticky;top:0;z-index:2;display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:10px 14px;background:var(--bar);border-bottom:1px solid #2a313b}
#bar b{font-size:16px;margin-right:8px}
button,input{font:inherit;font-size:14px;border-radius:6px;border:1px solid #39424e;background:#252c35;color:var(--fg);padding:6px 10px}
button{cursor:pointer}button.on{background:var(--hl);border-color:var(--hl);color:#fff}
input{min-width:200px;flex:1;max-width:360px}
#st{color:var(--dim);font-size:13px;margin-left:auto}
pre{margin:0;padding:12px 16px 40px;font-family:ui-monospace,Consolas,Menlo,monospace;white-space:pre-wrap;word-break:break-word;line-height:1.45}
.ok{color:var(--ok)}.warn{color:var(--warn)}.err{color:var(--err)}.ts{color:var(--dim)}
</style></head><body>
<div id="bar"><b>FilaStation-Waage &middot; Live-Log</b>
<button id="sm">A&minus;</button><button id="lg">A+</button>
<button id="fw" class="on">&#9654; Live</button>
<input id="q" type="search" placeholder="Filter, z. B. Spule 36">
<button id="cp">Kopieren</button>
<span id="st"></span></div>
<pre id="out"></pre>
<script>
var API='/api/log',seq=null,lines=[],follow=true,q='',fs=16,MAX=5000;
try{fs=parseInt(localStorage.getItem('fslogfs'))||16;}catch(e){}
var out=document.getElementById('out'),st=document.getElementById('st'),fw=document.getElementById('fw');
function size(d){fs=Math.max(10,Math.min(32,fs+d));out.style.fontSize=fs+'px';
try{localStorage.setItem('fslogfs',fs);}catch(e){}}
size(0);
var ERR=/\[(ERROR|CRITICAL)\]|❌|fehlgeschlagen|Fehler|nicht erreichbar/i,
WARN=/\[WARNING\]|⚠|nicht eindeutig|mehreren Spulen|Warnung/i,
OK=/✅|geschrieben|ist aktuell|NFC Update|HTTP 20\d/i;
function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}
function one(l){var c=ERR.test(l)?'err':(WARN.test(l)?'warn':(OK.test(l)?'ok':''));
var m=l.match(/^\d{4}-(\d\d)-(\d\d) (\d\d:\d\d:\d\d)(?:,\d+)?\s?(.*)$/),
t=m?'<span class="ts">'+m[2]+'.'+m[1]+'. '+m[3]+'</span> '+esc(m[4]):esc(l);
return c?'<span class="'+c+'">'+t+'</span>':t;}
function render(){var f=q.toLowerCase(),o=[],n=0;
for(var i=0;i<lines.length;i++){if(f&&lines[i].toLowerCase().indexOf(f)<0)continue;o.push(one(lines[i]));n++;}
out.innerHTML=o.join('\n');
st.textContent=n+(f?' / '+lines.length:'')+' Zeilen · '+new Date().toLocaleTimeString();
if(follow)window.scrollTo(0,document.body.scrollHeight);}
function poll(){
fetch(API+(seq===null?'':'?since='+seq),{cache:'no-store'}).then(function(r){
if(!r.ok)throw r.status;
seq=parseInt(r.headers.get('X-Log-Seq')||seq||0);var rst=r.headers.get('X-Log-Reset')==='1';
return r.text().then(function(t){return{t:t,rst:rst};});}).then(function(d){
if(d.rst)lines=[];
if(d.t){var a=d.t.split('\n');for(var i=0;i<a.length;i++)if(a[i])lines.push(a[i]);
if(lines.length>MAX)lines=lines.slice(-MAX);}
if(d.rst||d.t)render();}).catch(function(){st.textContent='keine Verbindung zur FilaStation-Waage';});}
function setFollow(v){follow=v;fw.classList.toggle('on',v);fw.innerHTML=v?'&#9654; Live':'&#10074;&#10074; Pause';
if(v)window.scrollTo(0,document.body.scrollHeight);}
fw.onclick=function(){setFollow(!follow);};
document.getElementById('sm').onclick=function(){size(-2);};
document.getElementById('lg').onclick=function(){size(2);};
document.getElementById('q').oninput=function(e){q=e.target.value;render();};
window.addEventListener('wheel',function(e){if(e.deltaY<0&&follow)setFollow(false);
else if(!follow&&window.innerHeight+window.scrollY>=document.body.scrollHeight-20)setFollow(true);});
document.getElementById('cp').onclick=function(){var b=this,t=out.innerText;
var done=function(ok){b.textContent=ok?'Kopiert':'Fehlgeschlagen';setTimeout(function(){b.textContent='Kopieren';},1500);};
if(navigator.clipboard&&window.isSecureContext){navigator.clipboard.writeText(t).then(function(){done(true);},function(){done(false);});return;}
var x=document.createElement('textarea');x.value=t;document.body.appendChild(x);x.select();
var ok=false;try{ok=document.execCommand('copy');}catch(e){}document.body.removeChild(x);done(ok);};
poll();setInterval(function(){if(!document.hidden)poll();},2000);
</script></body></html>"""


def read_from(since):
    """Liefert (text, neue_position, reset). Nur ganze Zeilen."""
    try:
        size = os.path.getsize(LOG)
    except OSError:
        return "", 0, since is None
    reset = since is None or since > size   # erster Abruf oder Datei neu angelegt
    start = max(0, size - FIRST_BYTES) if reset else since
    end = min(size, start + MAX_BYTES)
    with open(LOG, "rb") as f:
        f.seek(start)
        data = f.read(end - start)
    if reset and start > 0:                 # angeschnittene erste Zeile weglassen
        nl = data.find(b"\n")
        data = data[nl + 1:] if nl >= 0 else b""
        start = end - len(data)
    last = data.rfind(b"\n")                # unfertige letzte Zeile beim naechsten Mal
    data = data[:last + 1] if last >= 0 else b""
    return data.decode("utf-8", "replace"), start + len(data), reset


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype, extra=None):
        raw = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path in ("/", "/log", "/log/live"):
            self._send(200, PAGE, "text/html; charset=utf-8")
        elif u.path == "/api/log":
            s = parse_qs(u.query).get("since", [None])[0]
            since = int(s) if s and s.isdigit() else None
            text, pos, reset = read_from(since)
            self._send(200, text, "text/plain; charset=utf-8",
                       {"X-Log-Seq": str(pos), "X-Log-Reset": "1" if reset else "0"})
        else:
            self._send(404, "nicht gefunden", "text/plain; charset=utf-8")

    def log_message(self, *args):          # keine Zeile pro Abruf ins Journal
        pass


if __name__ == "__main__":
    print(f"Live-Log von {LOG} auf Port {PORT}", flush=True)
    try:
        ThreadingHTTPServer(("", PORT), Handler).serve_forever()
    except KeyboardInterrupt:
        sys.exit(0)
