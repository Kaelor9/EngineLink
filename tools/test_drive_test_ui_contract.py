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
assert "d.busy=true;d.left=st.s;render(true);" in record
assert "render(true);$('#main').scrollTop=0;" in record

# With reduced motion, only the first mooring setup remains visible.
assert "prefers-reduced-motion:reduce" in app
assert ".anim .mo-alongside{display:none!important}" in app
assert 'class="mo-alongside"' in app

print("Drive test animation continuity source contracts OK")
