"""Inspect a QRC container without executing its embedded code."""
import hashlib
import struct
import zlib

MAX_PACKED = 16 * 1024 * 1024
MAX_RAW = 32 * 1024 * 1024

def inspect_qrc(data):
    if not 8 <= len(data) <= MAX_PACKED or data[:4] != b"QRCC":
        raise ValueError("Not a bounded compressed QRC")
    size = struct.unpack_from(">I", data, 4)[0]
    if not 64 <= size <= MAX_RAW:
        raise ValueError("Uncompressed size bound")
    decoder = zlib.decompressobj()
    raw = decoder.decompress(data[8:], size + 1)
    if len(raw) != size or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("Incomplete, oversized or trailing compressed data")
    if raw[:4] != b"QRCF":
        raise ValueError("Missing QRCF header")
    header = struct.unpack_from(">4s13I", raw)
    tables = []
    for i in range(6):
        offset, length = header[2 + 2*i:4 + 2*i]
        if offset < 64 or offset > size or length > size - offset:
            raise ValueError("QRC table outside container")
        tables.append({"offset": offset, "bytes": length})
    return {"bytes": len(data), "uncompressed_bytes": size,
            "sha256": hashlib.sha256(data).hexdigest(), "tables": tables}
