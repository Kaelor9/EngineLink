"""Helpers for the source checks: the whole firmware as one text, and the body
of one function or class found by its signature (brace matching that skips
strings and comments), so a check does not depend on which file or which
neighbouring function it sits next to."""
from pathlib import Path

FW = Path(__file__).resolve().parent.parent / "firmware" / "EngineLink"


def firmware_source() -> str:
    """All firmware sources: EngineLink.ino first, then the other .ino tabs,
    headers and .cpp files, the way Arduino joins the .ino files."""
    files = [FW / "EngineLink.ino"]
    files += sorted(p for p in FW.glob("*.ino") if p.name != "EngineLink.ino")
    files += sorted(p for p in FW.glob("*.h") if p.name not in ("ui.h", "icons.h"))
    files += sorted(FW.glob("*.cpp"))
    return "\n".join(p.read_text(encoding="utf-8") for p in files)


def body(src: str, signature: str) -> str:
    """Signature plus its {...} block. Fails if the signature is missing or
    appears more than once."""
    assert src.count(signature) == 1, f"missing or duplicate: {signature}"
    start = src.index(signature)
    i = src.index("{", start)
    depth = 0
    while i < len(src):
        c = src[i]
        if src.startswith("//", i):
            i = src.index("\n", i)
            continue
        if src.startswith("/*", i):
            i = src.index("*/", i) + 2
            continue
        if c in "\"'":
            i += 1
            while src[i] != c:
                i += 2 if src[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return src[start:i + 1]
        i += 1
    raise AssertionError(f"unbalanced braces after {signature}")
