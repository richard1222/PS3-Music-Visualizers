"""Compare two local native QRC files without deploying or exporting assets."""
import argparse,json
from pathlib import Path
from native_textures import compare
from qrc import MAX_PACKED


def bounded_read(path):
    with path.open('rb') as stream:value=stream.read(MAX_PACKED+1)
    if len(value)>MAX_PACKED:raise ValueError('Packed QRC read bound')
    return value


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base',type=Path);parser.add_argument('candidate',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():parser.error('Existing report preserved')
    report=compare(bounded_read(args.base),bounded_read(args.candidate))
    with args.report.open('x',encoding='utf-8') as stream:json.dump(report,stream,indent=2)
    print(json.dumps({key:value for key,value in report.items() if key!='changed_jpegs'},indent=2))
