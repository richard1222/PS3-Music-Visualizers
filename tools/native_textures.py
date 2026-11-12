"""Offline JPEG-header inspection and fixed-size QRC texture experiments.

Header compatibility is a constraint, not proof of decoder or PS3 compatibility.
No network access, embedded code modification or console installation.
"""
import hashlib, struct, zlib
from native_qrc import unpack, index_files
from qrc import inspect_qrc


def jpeg_info(data):
    if not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
        raise ValueError('JPEG boundaries required')
    cursor=2; frame=None
    while cursor<len(data)-2:
        if data[cursor]!=255:raise ValueError('JPEG marker required')
        while cursor<len(data) and data[cursor]==255:cursor+=1
        if cursor>=len(data):raise ValueError('Truncated JPEG marker')
        marker=data[cursor];cursor+=1
        if marker in (0,0xd8,0xd9) or 0xd0<=marker<=0xd7:
            raise ValueError('Unexpected JPEG header marker')
        if cursor+2>len(data):raise ValueError('Truncated JPEG length')
        length=int.from_bytes(data[cursor:cursor+2],'big')
        if length<2 or cursor+length>len(data)-2:raise ValueError('JPEG segment bound')
        segment=data[cursor+2:cursor+length];cursor+=length
        if marker in (0xc0,0xc1,0xc2):
            if frame is not None or len(segment)<6:raise ValueError('Invalid JPEG frame')
            precision,height,width,channels=struct.unpack('>BHHB',segment[:6])
            if not width or not height or channels not in (1,3) or len(segment)!=6+3*channels:
                raise ValueError('JPEG frame shape')
            components=[list(segment[6+3*i:9+3*i]) for i in range(channels)]
            frame={'frame_marker':marker,'precision':precision,'width':width,
                   'height':height,'components':components}
        elif marker==0xda:
            if frame is None:raise ValueError('JPEG scan before frame')
            return frame
    raise ValueError('JPEG scan required')


def replace_jpeg_fixed(data,name,image):
    """Keep decompressed size, table locations and every other file unchanged.

    Shorter images leave unused bytes in their old allocation. The file's
    declared length is adjusted; the decoder is never asked to read the tail.
    Shared/overlapping allocations are refused instead of editing aliases.
    """
    raw=unpack(data);files,tables=index_files(raw)
    occupied=sorted((start,start+size) for start,size in tables if size)
    if any(left[1]>right[0] for left,right in zip(occupied,occupied[1:])):
        raise ValueError('Overlapping QRC tables')
    if name not in files or not name.endswith('.jpg'):raise ValueError('Existing JPEG required')
    old=files[name]['data']
    if len(image)>len(old):raise ValueError('Replacement exceeds original slot')
    if jpeg_info(old)!=jpeg_info(image):raise ValueError('JPEG frame profile differs')
    reference=files[name]['reference'];offset,size=struct.unpack_from('>II',raw,reference)
    for other,item in files.items():
        if other==name:continue
        begin,length=struct.unpack_from('>II',raw,item['reference'])
        if length and max(begin,offset)<min(begin+length,offset+size):
            raise ValueError('Shared or overlapping texture allocation')
    edited=bytearray(raw);start=tables[5][0]+offset
    edited[start:start+len(image)]=image
    struct.pack_into('>I',edited,reference+4,len(image))
    checked,after_tables=index_files(edited)
    if len(edited)!=len(raw) or after_tables!=tables:raise ValueError('Container layout changed')
    for other,item in checked.items():
        if item['data']!=(image if other==name else files[other]['data']):
            raise ValueError('Unexpected resource change')
    result=b'QRCC'+struct.pack('>I',len(edited))+zlib.compress(edited,9)
    inspect_qrc(result)
    return result


def compare(base,candidate):
    """Return metadata only; do not export embedded copyrighted resources."""
    old_raw=unpack(base);new_raw=unpack(candidate)
    old,old_tables=index_files(old_raw);new,new_tables=index_files(new_raw)
    common=old.keys()&new.keys();changed=sorted(n for n in common if old[n]['data']!=new[n]['data'])
    images=[]
    for name in changed:
        if not name.endswith('.jpg'):continue
        before=jpeg_info(old[name]['data']);after=jpeg_info(new[name]['data'])
        images.append({'path':name,'before_bytes':len(old[name]['data']),
                       'after_bytes':len(new[name]['data']),'before':before,'after':after,
                       'same_frame_profile':before==after})
    return {'base_sha256':hashlib.sha256(base).hexdigest(),
            'candidate_sha256':hashlib.sha256(candidate).hexdigest(),
            'base_raw_bytes':len(old_raw),'candidate_raw_bytes':len(new_raw),
            'raw_growth_bytes':len(new_raw)-len(old_raw),'same_tables':old_tables==new_tables,
            'added':sorted(new.keys()-old.keys()),'removed':sorted(old.keys()-new.keys()),
            'changed':changed,'changed_jpegs':images,
            'changed_camera_paths':[n for n in changed if n.endswith('.path')],
            'changed_menus':[n for n in changed if n.endswith('.mnu')],
            'changed_embedded_code':[n for n in changed if n.endswith(('.elf','.bin','.fpo','.vpo'))],
            'hardware_compatibility_verified':False}
