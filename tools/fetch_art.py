"""Download selected online artwork for personal console use; do not redistribute."""
from pathlib import Path
import argparse,hashlib,json,time,urllib.request

ART = [
    {"file":"one-piece-heroines.jpg", "page":"https://j-books.shueisha.co.jp/books/onepiece_novelheroines.html", "url":"https://j-books.shueisha.co.jp/icon/onepiece_novelheroines_news.jpg", "title":"One Piece HEROINES: Nami, Robin, Vivi and Perona", "series":"One Piece", "source_credit":"Shueisha publisher illustration"},
    {"file":"dxd-xenovia-irina-rossweisse.jpg", "page":"https://www.pinterest.com/pin/high-school-dxd-new-born-hero--796714990302842223/", "url":"https://i.pinimg.com/736x/1d/2e/37/1d2e37bbb959857772a33117d6e2b8b4.jpg", "title":"Xenovia, Irina and Rossweisse in uniforms", "series":"High School DxD"},
    {"file":"miku-spring-smile.jpg", "page":"https://wall.alphacoders.com/big.php?i=736713", "url":"https://images.alphacoders.com/736/thumb-1920-736713.jpg", "title":"Miku smiling among cherry blossoms", "series":"Hatsune Miku"},
    {"file":"miku-city.jpg", "page":"https://wall.alphacoders.com/big.php?i=578968", "url":"https://images6.alphacoders.com/578/578968.jpg", "title":"Moonlit Dreamscape with Hatsune Miku"},
    {"file":"miku-cosmic.jpg", "page":"https://wall.alphacoders.com/big.php?i=651169", "url":"https://images3.alphacoders.com/651/thumb-1920-651169.jpg", "title":"Cosmic Vocaloid Dreamscape"},
    {"file":"miku-piano.jpg", "page":"https://wall.alphacoders.com/big.php?i=736787", "url":"https://images2.alphacoders.com/736/736787.jpg", "title":"Enchanting Vocaloid Piano Night", "artist":"DMiTH"},
    {"file":"fate-saber.jpg", "page":"https://suwalls.com/anime/saber-fate-stay-night-8", "url":"https://cdn.suwalls.com/wallpapers/anime/saber-fate-stay-night-32735-3840x2160.jpg", "title":"Saber in purple twilight", "series":"Fate/stay night"},
    {"file":"fate-rin-saber.png", "page":"https://wall.alphacoders.com/big.php?i=933827", "url":"https://images5.alphacoders.com/933/thumb-1920-933827.png", "title":"Rin and Saber in white dresses", "series":"Fate/stay night"},
    {"file":"one-piece-nami-robin.jpg", "page":"https://wall.alphacoders.com/big.php?i=1274739", "url":"https://images6.alphacoders.com/127/1274739.jpg", "title":"Nami and Robin by the sea", "series":"One Piece", "artist":"Rachael"},
    {"file":"dxd-heroines.jpg", "page":"https://wallpapers.com/wallpapers/koneko-toujou-the-silent-powerhouse-of-high-school-dxd-6fr4ethcqw3pobcp.html", "url":"https://wallpapers.com/images/high/koneko-toujou-the-silent-powerhouse-of-high-school-dxd-6fr4ethcqw3pobcp.jpg", "title":"High School DxD heroines", "series":"High School DxD", "source_credit":"rolierols / Wallpapers.com"}
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
