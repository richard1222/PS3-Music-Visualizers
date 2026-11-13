"""Keep the installer and XMB version in sync with the in-app version."""
import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("header", type=Path)
parser.add_argument("template", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
match = re.search(r'^#define\s+NL_VERSION\s+"(\d{1,2})\.(\d{2})"\s*$', args.header.read_text(), re.M)
if not match:
    parser.error("NL_VERSION must have the form major.minor, for example 1.05")
version = f"{int(match[1]):02d}.{int(match[2]):02d}"
tree = ET.parse(args.template)
for key in ("APP_VER", "VERSION"):
    node = tree.getroot().find(f"value[@name='{key}']")
    if node is None:
        parser.error(f"SFO template has no {key}")
    node.text = version
args.output.parent.mkdir(parents=True, exist_ok=True)
tree.write(args.output, encoding="utf-8", xml_declaration=True)
