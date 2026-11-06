#!/usr/bin/env python3
"""Engine reading must not wait for the SD card (plan 13.6.1, criterion: engine
task waits < 200 ms). Source contracts for the card work the 2026-11-06 bench
reports caught holding engineMx: /api/sd/info 7.3 s, the raw clean-up walk
485 ms a pass, the Wi-Fi log ~300 ms. Plus the unmount guards that make
lock-free card work safe and the 503 causes counted apart.

These check the structure, not timing on the board; the board's own report
names the lock holder at the longest engine wait for that.
"""
import re
from fwsource import firmware_source, body

fw = firmware_source()

# Every card access in these functions sits between sdGapBegin() and sdGapEnd().
CARD = re.compile(r"openNextFile\(|SD\.(open|remove|rmdir|rename|usedBytes|totalBytes|exists)\(|wifiOpen\(|\bf\.print\(")
GAP_BEGIN = re.compile(r"const bool (\w+)=sdGapBegin\(\);")


def card_access_inside_gaps(src: str, name: str) -> int:
    seen = 0
    for m in CARD.finditer(src):
        before = src[:m.start()]
        opened = len(GAP_BEGIN.findall(before))
        closed = len(re.findall(r"sdGapEnd\(\w+\);", before))
        assert opened - closed == 1, f"{name}: card access '{m.group(0)}' outside an SD gap"
        seen += 1
    assert len(GAP_BEGIN.findall(src)) == len(re.findall(r"sdGapEnd\(\w+\);", src)), f"{name}: unbalanced SD gaps"
    for g in GAP_BEGIN.findall(src):
        assert f"sdGapEnd({g});" in src, f"{name}: {g} never ends"
    return seen


walk = body(fw, "void serviceSdWalk(){")
assert card_access_inside_gaps(walk, "serviceSdWalk") >= 5
assert "for(uint8_t k=0;k<4;k++)" in walk and "if(k==3)return;" in walk, "a few entries per loop pass"
assert walk.count("openNextFile(") == 1
assert "webXferBusy()" in walk, "no clean-up removal while a download may read the file"
assert "sessionName" in walk and "calName" in walk, "never removes the open session or motor test file"

tree = body(fw, "uint32_t removeTree(const String &path){")
assert card_access_inside_gaps(tree, "removeTree") >= 5
assert "vTaskDelay(" in tree, "the loop on the same core gets its turn during a wipe"

info = body(fw, "void handleSdInfo(){")
assert not CARD.search(info) and "sdCounts(" not in fw, "/api/sd/info never touches the card"
for key in ('\\"known\\"', '\\"counting\\"', '\\"usedMB\\"', '\\"rawMB\\"', '\\"exports\\"'):
    assert key in info, f"/api/sd/info lacks {key}"
assert "sdCountWanted=true" in info

# Wi-Fi log: callers only queue; one write per loop pass, without the lock.
log = body(fw, "void wifiLog(const String &what){")
assert not CARD.search(log) and "WIFI_PENDING_MAX" in log, "wifiLog only queues, bounded"
assert card_access_inside_gaps(body(fw, "static void wifiFlush(){"), "wifiFlush") >= 1
rep = body(fw, "void saveWifiReport(){")
assert card_access_inside_gaps(rep, "saveWifiReport") >= 1
assert rep.index("buildNetworkReport()") < rep.index("sdGapBegin()"), "the report reads shared data with the lock held"
assert "wifiFlush();" in body(fw, "void serviceWifiLog(){")

# The gap lets go only a lock the calling task holds (a foreign give asserts).
gb = body(fw, "bool sdGapBegin(){")
assert "xSemaphoreGetMutexHolder(engineMx)!=xTaskGetCurrentTaskHandle()" in gb and "return false" in gb
assert gb.index("sdOffLock=sdOffLock+1") < gb.index("xSemaphoreGive(engineMx)"), "counted while the lock is held"
ge = body(fw, "void sdGapEnd(bool gap){")
assert ge.index("xSemaphoreTake(engineMx") < ge.index("sdOffLock=sdOffLock-1")

# Nothing unmounts the card under lock-free card work or a reply still sending.
assert "bool sdUnmountBlocked(){return sdOffLock||webSendInFlight()||webXferBusy();}" in fw
sd = body(fw, "void serviceSD(){")
ends = [m.start() for m in re.finditer(r"SD\.end\(\)", sd)]
assert len(ends) == 2
first_guard = sd.index("if(sdUnmountBlocked()){webXferAbort();return;}")
assert first_guard < ends[0] and sd.index("sdWalkStop();") < ends[0]
assert sd.index("!sdUnmountBlocked()") < ends[1]
for route, sig in (("remount", 'web.on("/api/sd/remount"'), ("wipe", "void handleSdWipe(){"), ("factory reset", 'web.on("/api/factory-reset"')):
    start = fw.index(sig)
    nxt = fw.find("web.on(", start + 1) if sig.startswith("web.on(") else fw.index("\n}", start)
    chunk = fw[start:nxt]
    assert "if(sdOffLock){reply(false," in chunk, f"{route} refuses while the card is in use without the lock"
    assert "sdWalkStop();" in chunk, f"{route} closes the walk before touching the card"
io = body(fw, "static esp_err_t io(WebCtx &c, const char *p, size_t n, bool chunk) {")
assert io.index("sendsOffLock = sendsOffLock + 1") < io.index("ctxUnlock(c);") < io.index("sendsOffLock = sendsOffLock - 1")

# 503 by cause: every 503 path counts its own cause.
disp = body(fw, "static esp_err_t webDispatch(httpd_req_t *r) {")
assert disp.count("count503(httpdStats.busyXfer, r)") == 2 and "count503(httpdStats.busyLock, r)" in disp
assert "++httpdStats.busy503" not in disp
assert "count503(httpdStats.busyXferLock, j.r)" in body(fw, "static void xferWorker(void *) {")
for key in ("httpdBusyLock", "httpdBusyXfer", "httpdBusyXferLock", "httpdBusyPath", "engineMaxWaitBy"):
    assert f'\\"{key}\\"' in fw, f"report JSON lacks {key}"

# The engine task records who held the lock when its longest wait began.
et = body(fw, "void engineTask(void *){")
assert et.index("xSemaphoreGetMutexHolder(engineMx)") < et.index("xSemaphoreTake(engineMx,portMAX_DELAY)")
assert "engineMaxWaitBy" in et

print("SD card work and engine lock contracts passed")
