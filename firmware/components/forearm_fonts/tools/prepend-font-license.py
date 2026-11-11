"""Keep generated bitmap data's OFL attribution after font conversion."""
from pathlib import Path
import re
import sys

if len(sys.argv) != 3 or sys.argv[1] not in ("emoji", "text"):
    raise SystemExit("usage: prepend-font-license.py emoji|text OUTPUT.c")

target = Path(sys.argv[2])
match = re.fullmatch(rf"forearm_{sys.argv[1]}_(\d+)\.c", target.name)
if not match:
    raise SystemExit("output filename must identify its generated font size")
size = match.group(1)
template = (Path(__file__).parent / f"{sys.argv[1]}-license-header.txt").read_text()
header = template.replace(f"Forearm {sys.argv[1].title()} 24", f"Forearm {sys.argv[1].title()} {size}").replace("24 px", f"{size} px").encode()
data = target.read_bytes()
if not data.startswith(header):
    if data.startswith(template.encode()):
        data = data[len(template.encode()):]
    target.write_bytes(header + data)
