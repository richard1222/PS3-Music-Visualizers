"""Read and append-replace data resources in an owner's local QRC.

No embedded executable/shader replacement, firmware upload or console I/O.
"""
import struct,zlib
from qrc import inspect_qrc,MAX_RAW

def unpack(data):
    inspect_qrc(data)
    return zlib.decompress(data[8:])

def index_files(raw):
    if len(raw)<64 or raw[:4]!=b'QRCF':raise ValueError('QRCF required')
    tables=[struct.unpack_from('>II',raw,8+8*i) for i in range(6)]
    for offset,size in tables:
        if offset<64 or offset>len(raw) or size>len(raw)-offset:raise ValueError('Table bounds')
    tree_start,tree_size=tables[0];ids_start,ids_size=tables[1];strings_start,strings_size=tables[2];files_start,files_size=tables[5]
    def string(start,size,offset):
        if not 0<=offset<size:raise ValueError('String offset')
        end=raw.find(b'\0',start+offset,min(start+size,start+offset+2048))
        if end<0:raise ValueError('String terminator')
        return raw[start+offset:end].decode('utf-8')
    entries={};position=tree_start;nodes=0
    while position<tree_start+tree_size:
        nodes+=1
        if nodes>2048 or position+28>tree_start+tree_size:raise ValueError('Node bound')
        tag,count=struct.unpack_from('>ii',raw,position);position+=28
        if not 0<=count<=64 or position+16*count>tree_start+tree_size:raise ValueError('Attribute bound')
        name=None;source=None
        for _ in range(count):
            key,kind,a,b=struct.unpack_from('>4I',raw,position)
            label=string(strings_start,strings_size,key)
            if label=='id' and kind in (7,8):name=string(ids_start,ids_size,a+4)
            if label=='src' and kind==6:
                if a>files_size or b>files_size-a:raise ValueError('File bounds')
                source=(a,b,position+8)
            position+=16
        if string(strings_start,strings_size,tag)=='file':
            if name is None or source is None or name in entries:raise ValueError('Missing or duplicate file entry')
            offset,size,reference=source
            entries[name]={'data':raw[files_start+offset:files_start+offset+size],'reference':reference}
    if position!=tree_start+tree_size:raise ValueError('Tree end')
    return entries,tables

def replace_data(data,replacements):
    if not replacements:raise ValueError('No replacements')
    raw=unpack(data);entries,tables=index_files(raw);file_start,file_size=tables[5]
    if any(offset+size>file_start for offset,size in tables[:5]):raise ValueError('Files must be last table')
    tail=raw[file_start+file_size:]
    if len(tail)>15 or any(tail):raise ValueError('Unrecognized trailing data')
    allowed=('.jpg','.dds','.mnu','.path','.ini','.txt')
    for name,value in replacements.items():
        if name not in entries or not name.endswith(allowed):raise ValueError('Only existing data resources may be replaced')
        if not isinstance(value,bytes) or not 0<len(value)<=8*1024*1024:raise ValueError('Replacement bounds')
    prefix=bytearray(raw[:file_start]);payload=bytearray(raw[file_start:file_start+file_size])
    for name,value in sorted(replacements.items()):
        payload.extend(bytes((-len(payload))%16));offset=len(payload);payload.extend(value)
        struct.pack_into('>II',prefix,entries[name]['reference'],offset,len(value))
    struct.pack_into('>I',prefix,52,len(payload))
    rebuilt=bytes(prefix)+bytes(payload);rebuilt+=bytes((-len(rebuilt))%16)
    if len(rebuilt)>MAX_RAW:raise ValueError('Expanded QRC bound')
    checked,_=index_files(rebuilt)
    if entries.keys()!=checked.keys():raise ValueError('File set changed')
    for name,entry in checked.items():
        if entry['data']!=replacements.get(name,entries[name]['data']):raise ValueError('Resource verification failed')
    result=b'QRCC'+struct.pack('>I',len(rebuilt))+zlib.compress(rebuilt,9)
    inspect_qrc(result)
    return result
