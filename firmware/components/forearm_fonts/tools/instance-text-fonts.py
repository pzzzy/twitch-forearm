"""Make reproducible regular-weight sources before offline bitmap conversion."""
from pathlib import Path
import hashlib
import json

import fontTools
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

assert fontTools.__version__ == "4.60.1", "Use the pinned fonttools environment"
component = Path(__file__).resolve().parents[1]
provenance = json.loads((component / "assets/extended-text-provenance.json").read_text())
for record in provenance["files"]:
    data = (component / record["file"]).read_bytes()
    assert hashlib.sha256(data).hexdigest() == record["sha256"], record["file"]

for source, destination, axes in [
    ("Montserrat[wght].ttf", "Montserrat-Regular.ttf", {"wght": 400}),
    ("NotoSans[wdth,wght].ttf", "NotoSans-Regular.ttf", {"wght": 400, "wdth": 100}),
]:
    font = TTFont(component / "assets" / source, recalcTimestamp=False)
    instance = instantiateVariableFont(font, axes, inplace=True)
    instance.save(component / "tools/dump" / destination)
