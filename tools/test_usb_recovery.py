"""Host tests: refuse unknown partitions, merged 8 MB images and bad headers."""
from pathlib import Path
from tempfile import TemporaryDirectory
import struct
import sys
sys.path.insert(0, str(Path(__file__).parent))
import recover_usb as usb

def table_for(parts):
    image = bytearray(b"\xff" * 0x1000)
    for i, (label, (offset, size)) in enumerate(parts.items()):
        struct.pack_into("<HBBII16sI", image, i * 32,
                         0x50AA, 1 if label in ("nvs", "otadata", "spiffs") else 0,
                         0, offset, size, label.encode("ascii"), 0)
    return bytes(image)

raw = table_for(usb.PARTS)
usb.verify_layout(raw)
found = usb.partition_entries(raw)
assert found == usb.PARTS
bad = dict(usb.PARTS)
bad["spiffs"] = (0x680000, 0x180000)
try:
    usb.verify_layout(table_for(bad))
except RuntimeError:
    pass
else:
    raise AssertionError("unsafe partition layout accepted")
with TemporaryDirectory() as path:
    p = Path(path) / "enginelink-update.bin"
    image = bytearray(100_001)
    image[0] = 0xE9
    image[0x20:0x24] = bytes([0x32, 0x54, 0xCD, 0xAB])
    p.write_bytes(image)
    usb.verify_app(p)
    image[0x21] = 0
    p.write_bytes(image)
    try:
        usb.verify_app(p)
    except RuntimeError:
        pass
    else:
        raise AssertionError("invalid image was accepted")
    p.write_bytes(b"\xff" * (8 * 1024 * 1024))
    try:
        usb.verify_app(p)
    except RuntimeError:
        pass
    else:
        raise AssertionError("merged image was accepted")
print("USB rescue: partition allowlist, app header and full-image refusal OK")
