"""Prepare the creator's 14-planet ZIP for webMAN's HDD slots."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile
from qrc import inspect_qrc

ARCHIVE_SHA256 = "6982dcd9b2280a7c0b5a8d0ee7c1912264efd3191691297cd3dfe1f7bd9807b8"
PLANETS = ["earth_day", "earth_night", "moon", "mars", "jupiter", "mercury",
           "venus", "saturn", "neptune", "sun", "ceres", "eris", "haumea", "makemake"]
SOURCE = "https://www.psx-place.com/threads/research-modifying-the-gaia-visualization-custom_render_plugin-earth-qrc.27756/page-33#post-646"

def prepare(archive, output, original=None):
    archive, output = Path(archive), Path(output)
    if output.exists():
        raise ValueError("Output already exists")
    if archive.stat().st_size > 128 * 1024 * 1024:
        raise ValueError("Archive size bound")
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("Archive differs from inspected creator download")
    resources = []
    if original:
        data = Path(original).read_bytes()
        resources.append(("original_earth", data, inspect_qrc(data)))
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        expected = {p + suffix for p in PLANETS for suffix in (".qrc", ".jpg")}
        if len(names) != 28 or set(names) != expected:
            raise ValueError("Unexpected archive members")
        if sum(i.file_size for i in z.infolist()) > 100 * 1024 * 1024:
            raise ValueError("Expanded archive bound")
        for name in PLANETS:
            info = z.getinfo(name + ".qrc")
            if info.file_size > 16 * 1024 * 1024:
                raise ValueError("Resource size bound")
            data = z.read(info)
            resources.append((name, data, inspect_qrc(data)))
    output.mkdir(parents=True)
    manifest = {"source": SOURCE, "creator": "DeViL303", "archive_sha256": ARCHIVE_SHA256,
                "native_visualizer": "Gaia/Earth", "slots": []}
    for slot, (name, data, detail) in enumerate(resources, 1):
        leaf = str(slot) + ".qrc"
        (output / leaf).write_bytes(data)
        manifest["slots"].append({"slot": slot, "name": name, "file": leaf, **detail})
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("archive", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--original", type=Path)
    a = p.parse_args()
    result = prepare(a.archive, a.output, a.original)
    print(json.dumps({"slots": len(result["slots"]), "output": str(a.output)}, indent=2))
