"""Download selected online artwork for personal console use; do not redistribute."""
from pathlib import Path
import argparse,hashlib,json,time,urllib.request

ART = [{'file': 'miku-spring-smile.jpg',
  'page': 'https://wall.alphacoders.com/big.php?i=736713',
  'url': 'https://images.alphacoders.com/736/thumb-1920-736713.jpg',
  'title': 'Miku smiling among cherry blossoms',
  'series': 'Hatsune Miku'},
 {'file': 'fate-rin-saber.png',
  'page': 'https://wall.alphacoders.com/big.php?i=933827',
  'url': 'https://images5.alphacoders.com/933/thumb-1920-933827.png',
  'title': 'Rin and Saber in white dresses',
  'series': 'Fate/stay night'},
 {'file': 'one-piece-nami-robin.jpg',
  'page': 'https://wall.alphacoders.com/big.php?i=1274739',
  'url': 'https://images6.alphacoders.com/127/1274739.jpg',
  'title': 'Nami and Robin by the sea',
  'series': 'One Piece',
  'artist': 'Rachael'},
 {'file': 'dxd-rias-akeno.jpg',
  'series': 'High School DxD',
  'title': 'Rias and Akeno in school uniforms',
  'page': 'https://www.pinterest.com/pin/675258537869613029/',
  'url': 'https://i.pinimg.com/736x/13/e5/56/13e5567d7a3da8424ac2a0f304071a0e.jpg'}]

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
        if item["file"].endswith(".png"):
            valid=data.startswith(b"\x89PNG\r\n\x1a\n") and data[-8:-4]==b'IEND'
        else:
            valid=data.startswith(b"\xff\xd8") and data.endswith(b"\xff\xd9")
        if not valid:raise ValueError("Unexpected image format: " + item["file"])
        (output/item["file"]).write_bytes(data)
        receipt.append({**item, "bytes":len(data), "sha256":hashlib.sha256(data).hexdigest()})
    (output/"credits.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    return receipt

if __name__ == "__main__":
    parser=argparse.ArgumentParser();parser.add_argument("output",type=Path);args=parser.parse_args()
    print(json.dumps(fetch(args.output),indent=2))
