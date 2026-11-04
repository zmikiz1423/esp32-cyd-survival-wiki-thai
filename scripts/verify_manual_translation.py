"""Check completeness and byte limits of the Thai FM21-76 SD-card text."""
from __future__ import annotations

import collections
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "sdcard/library/fm21-76"
ORIGINAL = ROOT / ".thai-work/original/library/fm21-76"
NUMBERS = re.compile(r"\d+(?:[.,]\d+)*")


def main():
    config = (ROOT / "firmware/src/config.h").read_text(encoding="utf-8")
    limit = int(re.search(r"#define\s+MAX_DOC_BYTES\s+(\d+)", config)[1])
    expected = {f"{i:04d}.txt" for i in range(1, 63)}
    actual = {p.name for p in FOLDER.glob("[0-9][0-9][0-9][0-9].txt")}
    assert actual == expected, ("Incorrect manual part set", sorted(expected ^ actual))
    records = []
    for name in sorted(expected):
        path = FOLDER / name
        raw = path.read_bytes()
        text = raw.decode("utf-8")
        assert not text.startswith("\ufeff"), f"BOM in {name}"
        assert "\ufffd" not in text, f"Damaged UTF-8 in {name}"
        assert not re.search(r"ZX(?:Q|TM)\d", text), f"Unresolved marker in {name}"
        assert re.search(r"[\u0e00-\u0e7f]", text), f"Missing Thai translation in {name}"
        assert len(raw) <= limit, f"{name} exceeds document limit: {len(raw)} > {limit}"
        backup = ORIGINAL / name
        assert backup.exists(), f"Original required for numeric comparison: {backup}"
        source = backup.read_text(encoding="utf-8-sig")
        missing_numbers = collections.Counter(NUMBERS.findall(source)) - collections.Counter(NUMBERS.findall(text))
        assert not missing_numbers, f"Source numeric tokens missing in {name}: {dict(missing_numbers)}"
        records.append({"file": name, "utf8_bytes": len(raw), "source_numeric_tokens_preserved": True})
    summary = {"parts": len(records), "total_utf8_bytes": sum(x["utf8_bytes"] for x in records), "largest_part": max(records, key=lambda x: x["utf8_bytes"]), "document_byte_limit": limit, "records": records}
    (ROOT / ".thai-work/manual-verification.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in summary.items() if k != "records"}, ensure_ascii=True))


if __name__ == "__main__":
    main()
