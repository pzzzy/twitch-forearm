"""Keep generated bitmap data's OFL attribution after font conversion."""
from pathlib import Path
import sys

if len(sys.argv) != 3 or sys.argv[1] not in ("emoji", "text"):
    raise SystemExit("usage: prepend-font-license.py emoji|text OUTPUT.c")

header = (Path(__file__).parent / f"{sys.argv[1]}-license-header.txt").read_bytes()
target = Path(sys.argv[2])
data = target.read_bytes()
if not data.startswith(header):
    target.write_bytes(header + data)
