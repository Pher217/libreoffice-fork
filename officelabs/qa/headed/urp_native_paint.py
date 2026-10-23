"""Headed regression check: URP calls that paint native controls must not deadlock (macOS).

project#377 / officelabs-agent#652. A UNO call arriving over URP runs on a cppu
request thread. When it ends in an immediate repaint, AquaSalGraphics::drawNativeControl
used to draw AppKit cells on that thread; AppKit renders through SwiftUI, which waits
for the main thread, while the main thread waits for the SolarMutex: deadlock.

Triggers covered:
  writer  insert a chart OLE object, then two doVerb(0)/doVerb(-3) cycles, as the
          agent's handle_insert_chart does. Unfixed: wedges on the first doVerb.
  calc    select a cell + calculateAll() in a loop. Timing-dependent: unfixed, it
          wedged at iteration 1325 of 1500.

Needs a headed macOS build. Run it with the bundle's python (no -I, or `import uno` fails):

  APP=instdir/OfficeLabs.app
  $APP/Contents/Resources/python officelabs/qa/headed/urp_native_paint.py \
      $APP writer 10 2652 /tmp/urp-paint

Arguments: <app bundle> writer|calc <iterations> <port> <outdir>. It starts its own
soffice with a throwaway profile and kills it by pid at the end.
Exit 0 = all iterations finished and no call raised; 2 = a call blocked for 45 s
(a `sample` of soffice is written to <outdir>); 3 = a call raised, so a trigger may
not have run; 1 = error.
"""

import os
import subprocess
import sys
import threading
import time

import uno
from com.sun.star.beans import PropertyValue

APP, MODE, N, PORT, OUT = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
os.makedirs(OUT, exist_ok=True)
PROFILE = os.path.join(OUT, f"profile-{MODE}")
office = subprocess.Popen(
    [os.path.join(APP, "Contents/MacOS/soffice"), "--norestore", "--nologo", "--nodefault",
     f"-env:UserInstallation=file://{PROFILE}",
     f"--accept=socket,host=127.0.0.1,port={PORT};urp;"],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
PID = str(office.pid)


def stop_office():
    office.terminate()
    try:
        office.wait(5)
    except subprocess.TimeoutExpired:
        office.kill()
WEDGE_S = 45


ERRORS = []


def call(label, fn):
    box = {}

    def run():
        try:
            box["v"] = fn()
        except Exception as e:  # report, keep going
            box["e"] = e

    t = threading.Thread(target=run, daemon=True)
    t0 = time.time()
    t.start()
    t.join(WEDGE_S)
    if t.is_alive():
        path = os.path.join(OUT, f"sample-{MODE}-{label}.txt")
        subprocess.run(["sample", PID, "3", "-file", path], capture_output=True)
        print(f"WEDGE at {label} after {WEDGE_S}s; sample -> {path}", flush=True)
        office.kill()
        os._exit(2)
    if "e" in box:
        print(f"  {label}: exception {box['e']!r} ({time.time() - t0:.2f}s)", flush=True)
        ERRORS.append(label)
    return box.get("v")


def connect():
    local = uno.getComponentContext()
    resolver = local.ServiceManager.createInstanceWithContext(
        "com.sun.star.bridge.UnoUrlResolver", local)
    for _ in range(60):
        try:
            return resolver.resolve(
                f"uno:socket,host=127.0.0.1,port={PORT};urp;StarOffice.ComponentContext")
        except Exception:
            time.sleep(1)
    stop_office()
    raise SystemExit("no office on port " + PORT)


ctx = connect()
smgr = ctx.ServiceManager
desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
dispatcher = smgr.createInstanceWithContext("com.sun.star.frame.DispatchHelper", ctx)
hidden = PropertyValue()
hidden.Name, hidden.Value = "Hidden", False

if MODE == "writer":
    doc = call("load", lambda: desktop.loadComponentFromURL(
        "private:factory/swriter", "_blank", 0, (hidden,)))
    for i in range(N):
        t0 = time.time()

        def insert():
            text = doc.getText()
            cur = text.createTextCursor()
            cur.gotoEnd(False)
            ole = doc.createInstance("com.sun.star.text.TextEmbeddedObject")
            ole.CLSID = "12DCAE26-281F-416F-a234-c3086127382e"
            ole.Name = f"OLChart_{i}_{int(time.time() * 1000)}"
            size = uno.createUnoStruct("com.sun.star.awt.Size")
            size.Width, size.Height = 12000, 7000
            ole.setSize(size)
            text.insertTextContent(cur, ole, False)
            return ole

        ole = call(f"i{i}-insert", insert)
        comp = None
        for _ in range(15):
            comp = call(f"i{i}-getEmbedded", ole.getEmbeddedObject)
            if comp is not None:
                break
            time.sleep(0.3)
        if comp is not None:
            call(f"i{i}-defaultChart", comp.createDefaultChart)
            call(f"i{i}-modified", lambda: comp.setModified(True))
        for cycle in (1, 2):
            ext = call(f"i{i}-ext{cycle}", ole.getExtendedControlOverEmbeddedObject)
            call(f"i{i}-c{cycle}-doVerb0", lambda: ext.doVerb(0))
            time.sleep(0.3)
            call(f"i{i}-c{cycle}-doVerbHide", lambda: ext.doVerb(-3))
            time.sleep(0.2)
            frame = call(f"i{i}-c{cycle}-frame", lambda: doc.getCurrentController().getFrame())
            call(f"i{i}-c{cycle}-escape", lambda: dispatcher.executeDispatch(
                frame, ".uno:Escape", "", 0, ()))
            call(f"i{i}-c{cycle}-end", lambda: dispatcher.executeDispatch(
                frame, ".uno:GoToEndOfDoc", "", 0, ()))
        print(f"writer iteration {i + 1}/{N} ok ({time.time() - t0:.1f}s)", flush=True)
else:
    doc = call("load", lambda: desktop.loadComponentFromURL(
        "private:factory/scalc", "_blank", 0, (hidden,)))
    sheet = doc.Sheets.getByIndex(0)

    def fill():
        for r in range(1, 201):
            sheet.getCellByPosition(0, r - 1).setValue(r)
            sheet.getCellByPosition(1, r - 1).setFormula(f"=A{r}*2+RAND()")
            sheet.getCellByPosition(2, r - 1).setFormula(f"=SUM(B$1:B{r})")
    call("fill", fill)
    ctrl = doc.getCurrentController()
    for i in range(N):
        t0 = time.time()
        cell = sheet.getCellByPosition(2, i % 200)
        call(f"i{i}-select", lambda: ctrl.select(cell))
        call(f"i{i}-calculateAll", doc.calculateAll)
        call(f"i{i}-calculate", doc.calculate)
        print(f"calc iteration {i + 1}/{N} ok ({time.time() - t0:.2f}s)", flush=True)

call("close", lambda: doc.close(True))
stop_office()
if ERRORS:
    print(f"FAILED: {len(ERRORS)} call(s) raised: {ERRORS[:10]}", flush=True)
    sys.exit(3)
print("DONE", flush=True)
