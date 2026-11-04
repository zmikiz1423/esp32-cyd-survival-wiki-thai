"""Local-only authoring UI for complete province maps. No cloud tile scraping."""
from __future__ import annotations

import argparse
import copy
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import threading
import time
import traceback
from urllib.parse import parse_qs, urlsplit
import webbrowser

SCRIPT = Path(__file__).resolve().parent
ROOT = SCRIPT.parent
sys.path.insert(0, str(SCRIPT))
VERSION = "1.6.0-th4"
PROFILES = [
    {"id": "overview", "name": "ภาพรวม · ซูม 8–12", "minZoom": 8, "maxZoom": 12},
    {"id": "standard", "name": "มาตรฐาน · ซูม 8–14", "minZoom": 8, "maxZoom": 14},
    {"id": "detail", "name": "ละเอียด · ซูม 8–16", "minZoom": 8, "maxZoom": 16},
]


def atomic_json(path: Path, value: dict):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), "utf-8")
    os.replace(temporary, path)


class MapApplication:
    def __init__(self, output: Path, cache: Path, pbf: Path | None = None):
        self.output = output.resolve()
        self.cache = cache.resolve()
        self.cache.mkdir(parents=True, exist_ok=True)
        self.pbf = pbf
        self.token = secrets.token_hex(24)
        self.lock = threading.RLock()
        self.cancel = threading.Event()
        self.worker = None
        self.status = self.blank_status()
        self.plan_cache = {}
        self.server = None

    def blank_status(self):
        return {"state": "idle", "stage": "ready", "message": "พร้อมเตรียมแผนที่ทั้งจังหวัด",
                "job": None, "province": None, "count": 0, "completed": 0, "bytes": 0,
                "batches": 0, "batch": 0, "missing": 0, "output": str(self.output), "error": None}

    @property
    def active(self):
        return self.worker is not None and self.worker.is_alive()

    def snapshot(self):
        with self.lock:
            result = copy.deepcopy(self.status)
            result['running'] = self.active
            result['done'] = result['completed']
            result['total'] = result['count']
            result['completedBytes'] = result['bytes']
            return result

    @staticmethod
    def builder():
        from offline_maps import builder
        return builder

    def parameters(self, body):
        if not isinstance(body, dict):
            raise ValueError("รูปแบบคำขอไม่ถูกต้อง")
        province = str(body.get("province", ""))
        if not province or len(province) > 80:
            raise ValueError("เลือกจังหวัดก่อน")
        low, high = body.get("minZoom", 8), body.get("maxZoom", 14)
        if isinstance(low, bool) or isinstance(high, bool) or not isinstance(low, int) or not isinstance(high, int):
            raise ValueError("ระดับซูมต้องเป็นจำนวนเต็ม")
        if not 6 <= low <= high <= 16:
            raise ValueError("ระดับซูมต้องอยู่ระหว่าง 6 ถึง 16")
        return province, low, high

    def plan(self, body):
        province, low, high = self.parameters(body)
        key = (province, low, high)
        with self.lock:
            cached = self.plan_cache.get(key)
        if cached is None:
            cached = self.builder().plan(province, min_zoom=low, max_zoom=high)
            cached = dict(cached)
            cached["planId"] = hashlib.sha256(json.dumps(key).encode()).hexdigest()[:20]
            with self.lock:
                self.plan_cache[key] = cached
        return copy.deepcopy(cached)

    def start(self, body):
        parameters = self.parameters(body)
        plan = self.plan(body)
        with self.lock:
            if self.active:
                raise ValueError("มีงานกำลังทำอยู่ กรุณารอหรือหยุดงานก่อน")
            self.cancel.clear()
            self.status = self.blank_status()
            self.status.update(state="running", stage="starting", province=parameters[0],
                               job=secrets.token_hex(12), count=plan["count"], missing=plan["count"],
                               batches=plan.get("batches", 0), message="กำลังเริ่มเตรียมข้อมูลแผนที่")
            output = self.output
            self.worker = threading.Thread(target=self.run, args=(*parameters, output), daemon=False)
            self.worker.start()
        return self.snapshot()

    def progress(self, update):
        if not isinstance(update, dict):
            return
        with self.lock:
            for key in ("stage", "message", "batch", "batches", "lastTile"):
                if key in update:
                    self.status[key] = update[key]
            if "total" in update and self.status.get("stage") in ("render", "rendering", "verify", "complete", "completed"):
                self.status["count"] = update["total"]
            self.status['phaseDone'] = update.get('done', 0)
            self.status['phaseTotal'] = update.get('total', 0)
            image_stage = self.status.get('stage') in ('render', 'rendering', 'verify', 'complete', 'completed')
            if "done" in update and image_stage:
                self.status["completed"] = update["done"]
            for key in ("completedBytes", "bytes"):
                if key in update and image_stage:
                    self.status["bytes"] = update[key]
            self.status["missing"] = max(0, self.status["count"] - self.status["completed"])
            if self.cancel.is_set():
                self.status["state"] = "cancelling"

    def run(self, province, low, high, output):
        try:
            result = self.builder().build(province, output=output, cache_dir=self.cache,
                                          min_zoom=low, max_zoom=high, progress=self.progress,
                                          cancelled=self.cancel.is_set, pbf_path=self.pbf,
                                          font_path=ROOT / "sdcard/web/Sarabun-Regular.ttf")
            with self.lock:
                self.status.update(state="completed", stage="complete", message="เตรียมแผนที่ครบแล้ว พร้อมคัดลอกลง SD",
                                   completed=self.status["count"], missing=0, result=result)
                if isinstance(result, dict):
                    self.status["bytes"] = result.get("bytes", self.status["bytes"])
        except Exception as exc:
            cancelled = exc.__class__.__name__ == "BuildCancelled"
            with self.lock:
                self.status.update(state="cancelled" if cancelled else "error", stage="cancelled" if cancelled else "error",
                                   error=None if cancelled else str(exc),
                                   message="หยุดแล้ว กดเริ่มพื้นที่เดิมเพื่อทำต่อได้" if cancelled else str(exc))
            if not cancelled:
                with (self.cache / "last-error.log").open("a", encoding="utf-8") as log:
                    log.write(time.strftime("%Y-%m-%d %H:%M:%S\n"))
                    traceback.print_exc(file=log)

    def select_folder(self):
        if self.active:
            raise ValueError("หยุดงานก่อนเปลี่ยนปลายทาง")
        if sys.platform != "win32":
            raise ValueError("การเลือกโฟลเดอร์แบบหน้าต่างรองรับ Windows เท่านั้น")
        # No user-controlled content is interpolated into the PowerShell script.
        script = """Add-Type -AssemblyName System.Windows.Forms
$owner=New-Object Windows.Forms.Form
$owner.TopMost=$true;$owner.ShowInTaskbar=$false;$owner.Width=1;$owner.Height=1
$owner.StartPosition='CenterScreen';$owner.Opacity=0;$owner.Show()
$dialog=New-Object Windows.Forms.FolderBrowserDialog
$dialog.Description='เลือก SD Card หรือโฟลเดอร์ปลายทาง โปรแกรมจะสร้าง maps ให้'
$dialog.ShowNewFolderButton=$true
try {if($dialog.ShowDialog($owner) -eq 'OK'){[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false);[Console]::Write($dialog.SelectedPath)}} finally {$dialog.Dispose();$owner.Dispose()}
"""
        import base64
        encoded = base64.b64encode(script.encode("utf-16le")).decode("ascii")
        result = subprocess.run(["powershell.exe", "-NoProfile", "-STA", "-EncodedCommand", encoded],
                                capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise ValueError("เปิดหน้าต่างเลือกโฟลเดอร์ไม่สำเร็จ")
        selected = result.stdout.decode("utf-8-sig").strip()
        if selected:
            path = Path(selected)
            if path.name.lower() != "maps":
                path /= "maps"
            self.output = path.resolve()
            with self.lock:
                self.status["output"] = str(self.output)
        return {"output": str(self.output)}


class Handler(BaseHTTPRequestHandler):
    server_version = "SurvivalWikiOfflineMaps/1.0"

    @property
    def app(self):
        return self.server.app

    def log_message(self, fmt, *args):
        # Never log the index token/query string.
        pass

    def send(self, value, status=200, content_type="application/json; charset=utf-8"):
        payload = value if isinstance(value, bytes) else json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'self'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(payload)

    def fail(self, message, code=400):
        self.send({"error": message}, code)

    def check_access(self, api=False):
        host = f"127.0.0.1:{self.server.server_port}"
        if self.headers.get("Host") != host:
            self.fail("Invalid host", 403)
            return False
        origin = self.headers.get("Origin")
        if origin and origin != "http://" + host:
            self.fail("Invalid origin", 403)
            return False
        provided = self.headers.get("X-Map-Token", "")
        request_url = urlsplit(self.path)
        if request_url.path == '/api/preview' and self.command == 'GET':
            provided = provided or parse_qs(request_url.query).get('token', [''])[0]
        if api and not secrets.compare_digest(provided, self.app.token):
            self.fail("Unauthorized local request", 403)
            return False
        return True

    def body(self):
        if self.headers.get("Transfer-Encoding"):
            raise ValueError("Chunked requests are not supported")
        length = int(self.headers.get("Content-Length", "0"))
        if length < 0 or length > 8192:
            raise ValueError("คำขอใหญ่เกินขนาดที่อนุญาต")
        return json.loads(self.rfile.read(length).decode("utf-8")) if length else {}

    def do_GET(self):
        path = urlsplit(self.path)
        if not self.check_access(path.path.startswith("/api/")):
            return
        try:
            if path.path == "/api/config":
                self.send({"version": VERSION, "output": str(self.app.output),
                           "provinces": self.app.builder().list_provinces(), "profiles": PROFILES, "running": self.app.active,
                           "source": "geofabrik-local-osm", "batchTiles": 3000, "batchBytes": 50000000,
                           "batchNote": "แบ่งงานสร้างภาพในเครื่องเพื่อบันทึกความคืบหน้า ไม่ใช่การแบ่งคำขอเพื่อเลี่ยงลิมิตบริการแผนที่"})
            elif path.path == "/api/status":
                self.send(self.app.snapshot())
            elif path.path == '/api/preview':
                state = self.app.snapshot()
                tile = state.get('lastTile')
                if not isinstance(tile, dict) or not all(type(tile.get(k)) is int for k in ('z', 'x', 'y')):
                    self.fail('No rendered preview yet', 404)
                    return
                z, x, y = tile['z'], tile['x'], tile['y']
                if not 6 <= z <= 16 or not 0 <= x < 2 ** z or not 0 <= y < 2 ** z:
                    self.fail('Invalid preview tile', 404)
                    return
                target = self.app.output / str(z) / str(x) / (str(y) + '.jpg')
                self.send(target.read_bytes(), content_type='image/jpeg')
            elif path.path == "/api/coverage":
                province = parse_qs(path.query).get("province", [""])[0]
                data_dir = SCRIPT / "offline_maps/data"
                collection = None
                for source in data_dir.glob("*.geojson"):
                    data = json.loads(source.read_text("utf-8-sig"))
                    matching = [f for f in data.get("features", []) if province in (str(f.get("id", "")), str(f.get("properties", {}).get("id", "")), str(f.get("properties", {}).get("shapeISO", "")))]
                    if matching:
                        collection = {"type": "FeatureCollection", "features": matching}
                        break
                self.send(collection or {"type": "FeatureCollection", "features": []})
            elif path.path in ("/", "/index.html"):
                token = parse_qs(path.query).get("token", [""])[0]
                if not secrets.compare_digest(token, self.app.token):
                    self.fail("เปิดโปรแกรมจาก เตรียมแผนที่ทั้งจังหวัด.bat", 403)
                    return
                html = (SCRIPT / "offline-map-app/index.html").read_text("utf-8")
                html = html.replace("__MAP_TOKEN__", self.app.token)
                self.send(html.encode("utf-8"), content_type="text/html; charset=utf-8")
            elif path.path in ("/app.js", "/style.css"):
                target = SCRIPT / "offline-map-app" / path.path[1:]
                self.send(target.read_bytes(), content_type="text/javascript; charset=utf-8" if target.suffix == ".js" else "text/css; charset=utf-8")
            elif path.path == "/Sarabun-Regular.ttf":
                self.send((ROOT / "sdcard/web/Sarabun-Regular.ttf").read_bytes(), content_type="font/ttf")
            else:
                self.fail("Not found", 404)
        except (ValueError, FileNotFoundError) as exc:
            self.fail(str(exc))
        except Exception as exc:
            self.fail(str(exc), 500)

    def do_POST(self):
        path = urlsplit(self.path).path
        if not self.check_access(True):
            return
        try:
            body = self.body()
            if path == "/api/plan":
                self.send(self.app.plan(body))
            elif path == "/api/start":
                self.send(self.app.start(body))
            elif path == "/api/cancel":
                if self.app.active:
                    self.app.cancel.set()
                    with self.app.lock:
                        self.app.status.update(state="cancelling", message="กำลังหยุดหลังจบส่วนปัจจุบัน กรุณารอสักครู่")
                self.send(self.app.snapshot())
            elif path == "/api/select-folder":
                self.send(self.app.select_folder())
            elif path == "/api/open-folder":
                self.app.output.mkdir(parents=True, exist_ok=True)
                if sys.platform == "win32":
                    os.startfile(str(self.app.output))
                self.send({"output": str(self.app.output)})
            elif path == "/api/close":
                if self.app.active:
                    self.fail("กรุณาหยุดงานและรอจนหยุดก่อนปิดโปรแกรม", 409)
                else:
                    self.send({"ok": True})
                    threading.Thread(target=self.server.shutdown, daemon=True).start()
            else:
                self.fail("Not found", 404)
        except (ValueError, FileNotFoundError) as exc:
            self.fail(str(exc))
        except Exception as exc:
            self.fail(str(exc), 500)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "province-maps/maps")
    parser.add_argument("--cache", type=Path, default=Path(os.environ.get("LOCALAPPDATA", str(ROOT / ".thai-work"))) / "SurvivalWikiMaps/offline")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--state-file", type=Path)
    parser.add_argument("--pbf", type=Path, help="Developer/local source override; never exposed to browser")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    app = MapApplication(args.output, args.cache, args.pbf)
    # Fail before opening an empty UI when the runtime is incomplete.
    app.builder().list_provinces()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    server.app = app
    app.server = server
    url = f"http://127.0.0.1:{server.server_port}/?token={app.token}"
    if args.state_file:
        atomic_json(args.state_file, {"url": url, "token": app.token, "port": server.server_port, "pid": os.getpid(), "root": str(ROOT)})
    print("Survival Wiki Offline Maps started on loopback port", server.server_port, flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        app.cancel.set()
    finally:
        server.server_close()
        if app.worker:
            app.worker.join()


if __name__ == "__main__":
    main()
