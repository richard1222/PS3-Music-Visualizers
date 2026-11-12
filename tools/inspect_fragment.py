"""Read-only metadata for revision-6 Cg RSX fragment programs.

Inspects instruction framing and texture opcodes, not control-flow reachability,
rendering, uniforms or runtime bindings. Never modifies executable resources.
"""
import struct

MAX_FRAGMENT=1024*1024
TEXTURE_OPS={0x17,0x18,0x19,0x2f,0x31,0x33,0x34,0x37}


def inspect_fragment(data):
    if not 32<=len(data)<=MAX_FRAGMENT:raise ValueError('Fragment size bound')
    profile,revision,total,parameters,parameter_offset,program_offset,code_size,code_offset=struct.unpack_from('>8I',data)
    if profile!=7004 or revision!=6:raise ValueError('Expected revision-6 RSX fragment profile')
    if total!=len(data):raise ValueError('Declared fragment size differs')
    if parameters>1024 or parameter_offset<32 or parameter_offset>len(data):raise ValueError('Parameter bounds')
    if not 32<=program_offset<code_offset:raise ValueError('Program header bounds')
    if code_offset%16 or code_size%16 or not 16<=code_size<=len(data)-code_offset:
        raise ValueError('Microcode bounds')
    cursor=0;counts={};samplers=set();reads=0;instructions=0
    while cursor<code_size:
        words=struct.unpack_from('>4I',data,code_offset+cursor)
        # RSX microcode stores the two 16-bit halves in opposite order.
        dest,source0,source1,source2=[((w<<16)&0xffffffff)|(w>>16) for w in words]
        opcode=((dest>>24)&0x3f)|((source1>>31)<<6)
        counts[opcode]=counts.get(opcode,0)+1;instructions+=1
        if opcode in TEXTURE_OPS:
            reads+=1;samplers.add((dest>>17)&15)
        step=32 if any((word&3)==2 for word in (source0,source1,source2)) else 16
        if cursor+step>code_size:raise ValueError('Inline constant outside microcode')
        cursor+=step
        if dest&1:
            if cursor!=code_size:raise ValueError('Uninspected bytes after end instruction')
            return {'profile':profile,'revision':revision,'microcode_bytes':code_size,
                    'instructions':instructions,'texture_read_instructions':reads,
                    'sampler_indices':sorted(samplers),
                    'opcode_counts':{hex(k):v for k,v in sorted(counts.items())},
                    'runtime_binding_verified':False,'control_flow_evaluated':False}
    raise ValueError('No end instruction')
