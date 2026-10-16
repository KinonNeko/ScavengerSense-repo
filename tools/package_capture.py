"""Package the optional screenshot companion without the core mod or personal INI."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from check_binary import PE

ROOT=Path(__file__).resolve().parent.parent

def package(output):
    tree=ROOT/'fomod-build/04 Screenshot add-on'
    files={file.relative_to(tree).as_posix():file for file in sorted(tree.rglob('*')) if file.is_file()}
    dll=tree/'SKSE/Plugins/ScavengerSenseCapture.dll'
    compiled=next(ROOT/folder/'ScavengerSenseCapture.dll' for folder in ('plugin/build/Release','plugin/build')
                  if (ROOT/folder/'ScavengerSenseCapture.dll').is_file())
    assert dll.read_bytes()==compiled.read_bytes(), 'stale companion DLL'
    pe=PE(dll); assert pe.machine==0x8664 and {'SKSEPlugin_Load','ScavengerSenseCapture_Request'}<=pe.exports().keys()
    assert pe.unpack('<II',pe.offset(pe.exports()['SKSEPlugin_Version']))==(1,1<<16)
    assert 'SKSE/Plugins/ScavengerSenseCapture.ini' not in files
    assert not any(name.endswith(('.pdb','.log','.esp','.exe')) or name.startswith('Data/') for name in files)
    assert 'ScavengerSenseCapture-LICENSE.txt' in files
    output=output.resolve(); output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,file in files.items(): archive.write(file,name)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None and set(archive.namelist())==set(files)
        for name,file in files.items(): assert archive.read(name)==file.read_bytes(),name
    digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    return {'version':'0.1.0 beta','archive':str(output),'bytes':output.stat().st_size,'members':len(files),
            'sha256':digest(output),'dll_sha256':digest(dll),'payload':{name:digest(file) for name,file in files.items()}}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('output',type=Path); parser.add_argument('--report',type=Path)
    args=parser.parse_args(); result=package(args.output)
    if args.report: args.report.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:value for key,value in result.items() if key!='payload'},ensure_ascii=False,indent=2))
