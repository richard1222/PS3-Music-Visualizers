"""Stage curl's Mozilla CA bundle, checksum and MPL notice without overwriting."""
from pathlib import Path
from urllib.request import urlopen
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('output',type=Path);args=p.parse_args()
if args.output.exists():raise FileExistsError('Existing certificate directory preserved')
def fetch(url,limit):
    with urlopen(url,timeout=20) as response:
        assert response.url.startswith('https://')
        data=response.read(limit+1)
    assert len(data)<=limit,'Download size limit'
    return data
url='https://curl.se/ca/cacert.pem'
bundle=fetch(url,1048576)
checksum=fetch(url+'.sha256',4096).decode().split()[0].lower()
assert len(checksum)==64 and hashlib.sha256(bundle).hexdigest()==checksum
assert b'-----BEGIN CERTIFICATE-----' in bundle
license=fetch('https://www.mozilla.org/media/MPL/2.0/index.txt',131072)
assert b'Mozilla Public License' in license
args.output.mkdir(parents=True,exist_ok=False)
(args.output/'cacert.pem').write_bytes(bundle)
(args.output/'MPL-2.0.txt').write_bytes(license)
(args.output/'SOURCE.json').write_text(json.dumps({'source':url,'sha256':checksum,'bytes':len(bundle),'license':'MPL-2.0','upstream':'https://curl.se/docs/caextract.html'},indent=2))
print(json.dumps({'bundle_sha256':checksum,'bytes':len(bundle),'certificates':bundle.count(b'-----BEGIN CERTIFICATE-----')}))
