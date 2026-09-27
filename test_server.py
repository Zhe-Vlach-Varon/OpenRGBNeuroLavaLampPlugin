#!/usr/bin/env python3
"""
Local test server for the NeuroLavaLamp OpenRGB plugin.

Mimics the real endpoints so you can point the plugin at localhost:

  GET /            -> control web page (set live state, RGB color, schedule)
  GET /v1/rgb      -> {"live": bool, "rgb": [r,g,b]}          (HTTP polling URL)
  GET /v1/events   -> SSE stream of the same JSON payload     (SSE Stream URL)
  GET /schedule    -> [{"timestamp","live","streamers","title"}] (Schedule API URL)

Run:
  python3 test_server.py [port]        # default port 8765
  PORT=9000 python3 test_server.py

No dependencies beyond the Python standard library.
"""

import json
import os
import sys
import threading
import time
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST = "0.0.0.0"  # reachable from other machines/VMs too; page shows localhost URL
PORT = 8765

# ---------------------------------------------------------------------------
# Shared state
# ---------------------------------------------------------------------------

_lock = threading.Lock()
_state = {
    "live": False,                 # stream live flag sent to the plugin
    "rgb": [255, 100, 30],         # current color
    "auto_cycle": False,           # rotate hue on a timer (tests animations)
    "cycle_interval_ms": 2000,
    "hue": 20.0,                   # internal: current cycle hue
    "sse_enabled": True,           # when off, /v1/events returns 503 so the
                                   # plugin falls back to HTTP polling of /v1/rgb
    "schedule_live": True,         # schedule entry "live" flag
    "streamers": ["Neuro"],        # ["Neuro"] | ["Evil"] | both | []
    "title": "Test Stream",
    "timestamp_offset_hours": 0.0, # schedule timestamp = now + offset
}

_sse_clients = []   # wfile objects of connected SSE clients
_sse_lock = threading.Lock()


def _hsl_to_rgb(h, s, l):
    h %= 360.0
    c = (1 - abs(2 * l - 1)) * s
    x = c * (1 - abs((h / 60.0) % 2 - 1))
    m = l - c / 2
    if h < 60:
        r, g, b = c, x, 0
    elif h < 120:
        r, g, b = x, c, 0
    elif h < 180:
        r, g, b = 0, c, x
    elif h < 240:
        r, g, b = 0, x, c
    elif h < 300:
        r, g, b = x, 0, c
    else:
        r, g, b = c, 0, x
    return [int(round((r + m) * 255)), int(round((g + m) * 255)), int(round((b + m) * 255))]


def _payload():
    """The exact JSON object the plugin expects from /v1/rgb and SSE data lines."""
    with _lock:
        return {"live": bool(_state["live"]), "rgb": list(_state["rgb"])}


def broadcast(payload=None):
    """Push an SSE event to all connected clients (and log it for the web UI)."""
    if payload is None:
        payload = _payload()
    chunk = ("data: %s\n\n" % json.dumps(payload)).encode("utf-8")
    with _sse_lock:
        clients = list(_sse_clients)
    for wfile in clients:
        try:
            wfile.write(chunk)
            wfile.flush()
        except Exception:
            pass  # dead client; its handler thread cleans itself up


def schedule_payload():
    with _lock:
        offset_h = float(_state["timestamp_offset_hours"])
        live = bool(_state["schedule_live"])
        streamers = list(_state["streamers"])
        title = str(_state["title"])
    ts = (datetime.now(timezone.utc) + timedelta(hours=offset_h)).strftime("%Y-%m-%dT%H:%M:%SZ")
    return [{"timestamp": ts, "live": live, "streamers": streamers, "title": title}]


def _api_state():
    with _lock:
        out = dict(_state)
    out["sse_clients"] = len(_sse_clients)
    return out


def _auto_cycle_loop():
    """Background thread: rotate the hue and broadcast new colors while enabled."""
    last_tick = 0.0
    while True:
        time.sleep(0.2)
        with _lock:
            if not _state["auto_cycle"]:
                continue
            interval = max(100, int(_state["cycle_interval_ms"])) / 1000.0
            now = time.monotonic()
            if now - last_tick < interval:
                continue
            last_tick = now
            _state["hue"] = (_state["hue"] + 25.0) % 360.0
            _state["rgb"] = _hsl_to_rgb(_state["hue"], 1.0, 0.5)
        broadcast()


# ---------------------------------------------------------------------------
# Web UI
# ---------------------------------------------------------------------------

INDEX_HTML = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Neuro Lava Lamp Test Server</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: system-ui, sans-serif; background:#14161a; color:#e6e6e6; margin:0; padding:24px; }
  h1 { font-size:20px; margin:0 0 4px; }
  .sub { color:#8b93a7; font-size:13px; margin-bottom:20px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(320px,1fr)); gap:16px; align-items:start; }
  .card { background:#1d2027; border:1px solid #2a2f3a; border-radius:10px; padding:16px; }
  .card h2 { font-size:13px; text-transform:uppercase; letter-spacing:.08em; color:#9aa3b5; margin:0 0 12px; }
  label.row { display:flex; align-items:center; gap:8px; margin:8px 0; font-size:14px; }
  input[type=range] { flex:1; }
  .swatch { width:46px; height:30px; border-radius:6px; border:1px solid #444; display:inline-block; }
  button { background:#2d6cdf; color:white; border:none; padding:8px 12px; border-radius:6px; cursor:pointer; font-size:13px; }
  button.secondary { background:#394050; }
  .btns { display:flex; gap:8px; flex-wrap:wrap; margin-top:12px; }
  .mono { font-family:ui-monospace,monospace; font-size:12px; color:#a8d0ff; background:#14171d; padding:2px 6px; border-radius:4px; }
  table { width:100%; border-collapse:collapse; font-size:13px; }
  td { padding:5px 6px; border-bottom:1px solid #2a2f3a; vertical-align:top; }
  .log { height:150px; overflow-y:auto; background:#14171d; border-radius:6px; padding:8px; font-family:ui-monospace,monospace; font-size:12px; color:#9fe8a8; white-space:pre-wrap; }
  .muted { color:#8b93a7; font-size:12px; }
</style>
</head>
<body>
<h1>Neuro Lava Lamp &mdash; Test Server</h1>
<div class="sub">Point the plugin at <span class="mono">http://localhost:__PORT__</span> &middot; SSE clients connected: <b id="clients">?</b></div>

<div class="grid">
  <div class="card">
    <h2>Stream state (sent to /v1/rgb and /v1/events)</h2>
    <label class="row"><input type="checkbox" id="live"> Live</label>
    <div style="display:flex;align-items:center;gap:10px;margin-top:6px;">
      <span class="swatch" id="swatch"></span>
      <span class="mono" id="rgbtext"></span>
    </div>
    <label class="row">R <input type="range" id="r" min="0" max="255"><span class="mono" id="rv" style="width:30px;text-align:right"></span></label>
    <label class="row">G <input type="range" id="g" min="0" max="255"><span class="mono" id="gv" style="width:30px;text-align:right"></span></label>
    <label class="row">B <input type="range" id="b" min="0" max="255"><span class="mono" id="bv" style="width:30px;text-align:right"></span></label>

    <hr style="border-color:#2a2f3a;border-width:1px 0 0">
    <label class="row"><input type="checkbox" id="autocycle"> Auto-cycle colors (tests animations)</label>
    <label class="row muted">Interval <input type="number" id="cyclems" min="100" max="60000" step="100" style="width:90px"> ms</label>
    <label class="row"><input type="checkbox" id="sseon"> SSE enabled (off = plugin falls back to polling)</label>

    <div class="btns">
      <button onclick="sendCurrent()">Send current state</button>
      <button class="secondary" onclick="sendBlack()">Send black (0,0,0)</button>
      <button class="secondary" onclick="sendRandom()">Random color</button>
    </div>
  </div>

  <div class="card">
    <h2>Schedule (/schedule) &mdash; Evil-only rule</h2>
    <label class="row"><input type="checkbox" id="schedlive"> Stream is live</label>
    <span class="muted">Streamers:</span>
    <label class="row"><input type="checkbox" id="neuro" checked> Neuro</label>
    <label class="row"><input type="checkbox" id="evil"> Evil</label>
    <label class="row">Title <input type="text" id="title" value="Test Stream" style="flex:1"></label>
    <span class="muted">Timestamp offset (relative to now)</span>
    <label class="row"><input type="range" id="offset" min="-24" max="3" step="0.5"> <span class="mono" id="offv"></span></label>
    <div class="btns">
      <button class="secondary" onclick="setOffset(0)">Now</button>
      <button class="secondary" onclick="setOffset(1)">+1h</button>
      <button class="secondary" onclick="setOffset(-20)">&minus;20h (outside window)</button>
    </div>
    <p class="muted">Plugin rule: Evil checked + Neuro unchecked &rarr; &ldquo;Evil-only stream detected&rdquo;. Timestamp must be within now+2h &hellip; now&minus;18h to count.</p>
  </div>

  <div class="card">
    <h2>Plugin settings to paste</h2>
    <table>
      <tr><td>SSE Stream URL</td><td class="mono">http://localhost:__PORT__/v1/events</td></tr>
      <tr><td>HTTP Polling URL</td><td class="mono">http://localhost:__PORT__/v1/rgb</td></tr>
      <tr><td>Schedule API URL</td><td class="mono">http://localhost:__PORT__/schedule</td></tr>
    </table>
    <h2 style="margin-top:16px">Event log (SSE broadcasts)</h2>
    <div class="log" id="log"></div>
  </div>
</div>

<script>
const $ = id => document.getElementById(id);
let state = null;

function rgbCss(rgb){ return `rgb(${rgb[0]},${rgb[1]},${rgb[2]})`; }

async function refresh(){
  const res = await fetch('/api/state');
  state = await res.json();
  $('live').checked = state.live;
  $('r').value = state.rgb[0]; $('g').value = state.rgb[1]; $('b').value = state.rgb[2];
  updateSwatch();
  $('autocycle').checked = state.auto_cycle;
  $('cyclems').value = state.cycle_interval_ms;
  $('sseon').checked = state.sse_enabled;
  $('schedlive').checked = state.schedule_live;
  $('neuro').checked = state.streamers.includes('Neuro');
  $('evil').checked = state.streamers.includes('Evil');
  $('title').value = state.title;
  $('offset').value = state.timestamp_offset_hours;
  updateOffsetLabel();
  $('clients').textContent = state.sse_clients;
}

function updateSwatch(){
  const rgb = [+$('r').value, +$('g').value, +$('b').value];
  $('swatch').style.background = rgbCss(rgb);
  $('rgbtext').textContent = `rgb(${rgb.join(', ')})`;
  $('rv').textContent = rgb[0]; $('gv').textContent = rgb[1]; $('bv').textContent = rgb[2];
}

function updateOffsetLabel(){
  const h = +$('offset').value;
  const d = new Date(Date.now() + h*3600*1000);
  $('offv').textContent = `${h >= 0 ? '+' : ''}${h}h -> ${d.toISOString().replace('.000','')}`;
}

async function push(patch){
  const res = await fetch('/api/state', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(patch)});
  state = await res.json();
  log(`broadcast -> ${JSON.stringify({live: state.live, rgb: state.rgb})}`);
}

function currentRgbPatch(){ return {rgb: [+$('r').value, +$('g').value, +$('b').value]}; }

async function sendCurrent(){ await push({...currentRgbPatch(), live: $('live').checked}); refresh(); }
async function sendBlack(){ $('live').checked = true; $('r').value=0; $('g').value=0; $('b').value=0; updateSwatch(); await push({rgb:[0,0,0], live:true}); refresh(); }
async function sendRandom(){ const rgb=[...Array(3)].map(()=>Math.floor(Math.random()*256)); ['r','g','b'].forEach((k,i)=>$(k).value=rgb[i]); $('live').checked = true; updateSwatch(); await push({rgb, live:true}); refresh(); }

function setOffset(h){ $('offset').value = h; updateOffsetLabel(); pushSchedule(); }
async function pushSchedule(){
  const streamers=[]; if($('neuro').checked) streamers.push('Neuro'); if($('evil').checked) streamers.push('Evil');
  await push({schedule_live:$('schedlive').checked, streamers, title:$('title').value, timestamp_offset_hours:+$('offset').value});
}

['r','g','b'].forEach(k => { $(k).addEventListener('input', updateSwatch); $(k).addEventListener('change', () => push(currentRgbPatch())); });
$('live').addEventListener('change', async ()=>{ await push({live:$('live').checked}); refresh(); });
$('autocycle').addEventListener('change', async ()=>{ await push({auto_cycle:$('autocycle').checked, cycle_interval_ms:+$('cyclems').value}); });
$('cyclems').addEventListener('change', async ()=>{ await push({auto_cycle:$('autocycle').checked, cycle_interval_ms:+$('cyclems').value}); });
$('sseon').addEventListener('change', async ()=>{ await push({sse_enabled:$('sseon').checked}); refresh(); });
['schedlive','neuro','evil'].forEach(id => $(id).addEventListener('change', pushSchedule));
$('title').addEventListener('change', pushSchedule);
$('offset').addEventListener('change', pushSchedule);

function log(msg){ const el=$('log'); el.textContent = `[${new Date().toLocaleTimeString()}] ${msg}\\n` + el.textContent; }

refresh();
setInterval(()=>{ fetch('/api/state').then(r=>r.json()).then(s=>$('clients').textContent=s.sse_clients).catch(()=>{}); }, 3000);
</script>
</body>
</html>
"""


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "NeuroLavaTestServer/1.0"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        print("[%s] %s" % (self.log_date_time_string(), fmt % args))

    # -- helpers -----------------------------------------------------------
    def _send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _send_html(self, html):
        body = html.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json_body(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        return json.loads(raw.decode("utf-8")) if raw else {}

    # -- routes ------------------------------------------------------------
    def do_GET(self):
        path = self.path.split("?", 1)[0]
        try:
            if path == "/":
                self._send_html(INDEX_HTML.replace("__PORT__", str(PORT)))
            elif path == "/v1/rgb":
                self._send_json(_payload())
            elif path == "/api/state":
                self._send_json(_api_state())
            elif path == "/schedule":
                self._send_json(schedule_payload())
            elif path == "/v1/events":
                with _lock:
                    sse_enabled = bool(_state["sse_enabled"])
                if not sse_enabled:
                    # Force the plugin onto its HTTP-polling fallback path.
                    self._send_json({"error": "SSE disabled for testing"}, 503)
                else:
                    self._handle_sse()
            else:
                self._send_json({"error": "not found"}, 404)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path != "/api/state":
            self._send_json({"error": "not found"}, 404)
            return
        try:
            patch = self._read_json_body()
        except Exception:
            self._send_json({"error": "invalid JSON body"}, 400)
            return

        with _lock:
            if isinstance(patch.get("live"), bool):
                _state["live"] = patch["live"]
            rgb = patch.get("rgb")
            if isinstance(rgb, (list, tuple)) and len(rgb) == 3:
                try:
                    _state["rgb"] = [max(0, min(255, int(v))) for v in rgb]
                except (TypeError, ValueError):
                    pass
            if isinstance(patch.get("auto_cycle"), bool):
                _state["auto_cycle"] = patch["auto_cycle"]
            if "cycle_interval_ms" in patch:
                try:
                    _state["cycle_interval_ms"] = max(100, min(60000, int(patch["cycle_interval_ms"])))
                except (TypeError, ValueError):
                    pass
            if isinstance(patch.get("sse_enabled"), bool):
                _state["sse_enabled"] = patch["sse_enabled"]
            if isinstance(patch.get("schedule_live"), bool):
                _state["schedule_live"] = patch["schedule_live"]
            if isinstance(patch.get("streamers"), list):
                _state["streamers"] = [s for s in patch["streamers"] if s in ("Neuro", "Evil")]
            if isinstance(patch.get("title"), str):
                _state["title"] = patch["title"][:120]
            if "timestamp_offset_hours" in patch:
                try:
                    _state["timestamp_offset_hours"] = max(-48.0, min(48.0, float(patch["timestamp_offset_hours"])))
                except (TypeError, ValueError):
                    pass

        broadcast()  # push the new state to any connected plugin via SSE
        self._send_json(_api_state())

    # -- SSE ---------------------------------------------------------------
    def _handle_sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        with _sse_lock:
            _sse_clients.append(self.wfile)
        try:
            # Late-joining clients get the current state immediately.
            first = ("data: %s\n\n" % json.dumps(_payload())).encode("utf-8")
            self.wfile.write(first)
            self.wfile.flush()
            # Hold the connection open; periodic comment keeps proxies happy.
            while True:
                time.sleep(15)
                try:
                    self.wfile.write(b": ping\n\n")
                    self.wfile.flush()
                except Exception:
                    break
        finally:
            with _sse_lock:
                if self.wfile in _sse_clients:
                    _sse_clients.remove(self.wfile)


def main():
    global PORT
    try:
        PORT = int(os.environ.get("PORT") or (sys.argv[1] if len(sys.argv) > 1 else 8765))
    except ValueError:
        print("Invalid port, using default 8765")
        PORT = 8765

    server = ThreadingHTTPServer((HOST, PORT), Handler)
    threading.Thread(target=_auto_cycle_loop, daemon=True).start()

    print("Neuro Lava Lamp test server running:")
    print("  Web UI:            http://localhost:%d/" % PORT)
    print("  SSE Stream URL:    http://localhost:%d/v1/events" % PORT)
    print("  HTTP Polling URL:  http://localhost:%d/v1/rgb" % PORT)
    print("  Schedule API URL:  http://localhost:%d/schedule" % PORT)
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
