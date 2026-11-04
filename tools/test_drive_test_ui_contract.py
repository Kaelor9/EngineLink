#!/usr/bin/env python3
"""Drive test animation contract: keep SVGs mounted during live sampling."""
from pathlib import Path

app = Path("firmware/ui/app.html").read_text(encoding="utf-8")

# Live telemetry and recording progress are painted into existing elements.
assert "if(S.page==='dtest')dtPaint();" in app
assert "function dtPaint()" in app
for marker in ('data-dt-engine=', 'data-dt-progress', 'data-dt-now', 'data-dt-left'):
    assert marker in app, marker

record = app.split("async function dtRecord(){", 1)[1].split("// Four readings a second", 1)[0]
sampling = record.split("while(Date.now()<until", 1)[1].split("if(S.dt!==d", 1)[0]
assert "dtPaint();" in sampling
assert "render(" not in sampling, "Do not replace the scene during recording"
assert "d.busy=true;d.left=st.s;d.warnMsg='';d.miss={};render(true);" in record
# A safety guard (or Pause) stops the running step instead of recording on.
assert "const g=dtGuard(st,engines(),d.place,d);if(g&&!d.abortStep){d.abortStep=true;d.warnMsg=g;}" in record
assert "if(d.abortStep){d.abortStep=false;d.busy=false;render(true);return;}" in record
guard = app.split("function dtGuard(st,es,place,d){", 1)[1].split("\n}", 1)[0]
for m in ("over 1,000 rpm at the dock", "is above idle", "No connection to EngineLink", "The boat is moving", "No engine data from"):
    assert m in guard, m
assert "e.rpm<800" in app, "A step starts only at idle"
assert "d.phase='stopped'" in app and "Put both levers in NEUTRAL" in app
assert "render(true);$('#main').scrollTop=0;" in record

# With reduced motion: one still picture of the first mooring, no switch to
# the second, the ripples stand still.
assert "prefers-reduced-motion:reduce" in app
assert "const still=matchMedia('(prefers-reduced-motion: reduce)').matches;" in app
assert "if(!still&&host.dataset.dtscene==='moor'&&t>o.end)" in app
assert "dtFrame(o,still?99:t,still);" in app
assert "@media (prefers-reduced-motion:reduce){.dtw{animation:none" in app

# The owner's chosen scenes (2026-11-04) come from the boat studio page, and
# the lines are recomputed every frame from the boat's moved cleat.
assert "const DTS=/*DTS*/{" in app and '"boat":' in app and '"moorA":' in app and '"moorB":' in app and '"sea":' in app
frame = app.split("function dtFrame(o,t,still){", 1)[1].split("\n}", 1)[0]
assert "A=tf(l[0])" in frame, "The boat end of each line follows the cleat"

print("Drive test animation continuity source contracts OK")
