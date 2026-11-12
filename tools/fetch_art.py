"""Download selected online artwork for personal console use; do not redistribute."""
from pathlib import Path
import argparse,hashlib,json,time,urllib.request

ART = [
    {"file":"miku-city.jpg", "page":"https://wall.alphacoders.com/big.php?i=578968", "url":"https://images6.alphacoders.com/578/578968.jpg", "title":"Moonlit Dreamscape with Hatsune Miku"},
    {"file":"miku-cosmic.jpg", "page":"https://wall.alphacoders.com/big.php?i=651169", "url":"https://images3.alphacoders.com/651/thumb-1920-651169.jpg", "title":"Cosmic Vocaloid Dreamscape"},
    {"file":"miku-piano.jpg", "page":"https://wall.alphacoders.com/big.php?i=736787", "url":"https://images2.alphacoders.com/736/736787.jpg", "title":"Enchanting Vocaloid Piano Night", "artist":"DMiTH"}
]

def fetch(output):
    output = Path(output)
    if output.exists(): raise ValueError("Output exists")
    output.mkdir(parents=True)
    receipt = []
    for item in ART:
        data = bytearray(); deadline = time.monotonic() + 30
        request = urllib.request.Request(item["url"], headers={"User-Agent":"SaberPS3MusicVisuals/0.1"})
        with urllib.request.urlopen(request, timeout=15) as response:
            while True:
                chunk = response.read(65536)
                if not chunk: break
                data.extend(chunk)
                if len(data) > 8*1024*1024 or time.monotonic() > deadline:
                    raise ValueError("Artwork download bound")
        if not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
            raise ValueError("Not a complete JPEG: " + item["file"])
        (output/item["file"]).write_bytes(data)
        receipt.append({**item, "bytes":len(data), "sha256":hashlib.sha256(data).hexdigest()})
    (output/"credits.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    return receipt

if __name__ == "__main__":
    parser=argparse.ArgumentParser();parser.add_argument("output",type=Path);args=parser.parse_args()
    print(json.dumps(fetch(args.output),indent=2))
