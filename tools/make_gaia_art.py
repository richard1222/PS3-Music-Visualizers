"""OFFLINE ONLY: unverified Gaia texture variant. Earlier camera variant froze.

Preserve every camera path and menu byte. Do not install on a console until
the failed experiment's cause and renderer memory limits are understood.
"""
from pathlib import Path
import argparse,hashlib,json,re
from native_qrc import unpack,index_files,replace_data

CAMERA=(
 'time=0 eye 0 0 9.5 center 0 0 0 up 0 1 0 fovy=50\r\n'
 'time=8 eye 0.04 0.02 9.45 center 0 0 0 up 0 1 0 fovy=49\r\n'
 'time=16 eye 0 0 9.5 center 0 0 0 up 0 1 0 fovy=50\r\n'
 'time=24 eye -0.04 -0.02 9.45 center 0 0 0 up 0 1 0 fovy=49\r\n'
 'time=32 eye 0 0 9.5 center 0 0 0 up 0 1 0 fovy=50\r\n').encode('ascii')

def make(base,tiles):
    files,_=index_files(unpack(base));replacements={}
    targets=[name for name in files if re.fullmatch(r'earth/flashrom/clouds/[0-5]/1/00[01]_00[01]\.jpg',name)]
    if len(targets)!=24:raise ValueError('Expected all 24 cloud texture slots')
    for name in targets:
        data=(Path(tiles)/Path(name).name).read_bytes()
        if not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9') or len(data)>512*1024:
            raise ValueError('Bounded JPEG tile required')
        replacements[name]=data
    result=replace_data(base,replacements)
    proof={'scope':'OFFLINE ONLY: texture-only experiment; hardware compatibility unverified; earlier camera variant froze; no audio-response claim',
           'base_sha256':hashlib.sha256(base).hexdigest(),'output_sha256':hashlib.sha256(result).hexdigest(),
           'texture_slots':len(targets),'camera_paths_changed':0,'menus_changed':0,'changed_resources':sorted(replacements),
           'unchanged_resources':len(files)-len(replacements),'embedded_code_unchanged':True}
    return result,proof

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('tiles',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    if a.output.exists() or a.output.with_suffix('.json').exists():p.error('Output exists')
    result,proof=make(a.base.read_bytes(),a.tiles)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('xb') as f:f.write(result)
    with a.output.with_suffix('.json').open('x') as f:json.dump(proof,f,indent=2)
    print(json.dumps(proof,indent=2))
