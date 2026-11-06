"""Package the verified BOOT prefix and installer; no phone or private inputs."""
from pathlib import Path
import argparse,hashlib,json,zipfile

def package(prefix,output):
    repo=Path(__file__).resolve().parents[1]
    profile=json.loads((repo/'profiles/installed-gles.json').read_text())
    payload=prefix.read_bytes()
    if len(payload)!=profile['bytes'] or hashlib.sha256(payload).hexdigest()!=profile['template_sha256']:
        raise ValueError('Template differs from the verified installed build')
    entries={'boot-prefix.template':payload,
             'META-INF/com/google/android/update-binary':(repo/'installer/update-binary').read_bytes(),
             'META-INF/com/google/android/updater-script':b'# Installed GLES BOOT: handled by update-binary.\n'}
    for name in ['README.md','NOTICE','SOURCES.md','INSTALL.md','TESTING.md','LICENSE',
                 'profiles/installed-gles.json','core/LICENSE.APSL','core/NOTICE','core/THIRD_PARTY_LICENSES.md']:
        entries[name]=(repo/name).read_bytes()
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,data in sorted(entries.items()):
            info=zipfile.ZipInfo(name,date_time=(1980,1,1,0,0,0))
            info.create_system=3
            info.external_attr=(0o100755 if name.endswith('/update-binary') else 0o100644)<<16
            info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,data,compresslevel=9)
    digest=hashlib.sha256(output.read_bytes()).hexdigest()
    print(digest+'  '+output.name)
    return digest

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix-template',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    package(args.prefix_template,args.output)
