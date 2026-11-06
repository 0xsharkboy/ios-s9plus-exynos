from pathlib import Path
import hashlib,json,subprocess

workspace=Path('/path/to/development-workspace')
repo=Path('/home/scion/scion-star2lte')
area=repo/'xnu-deps/build/panvk-native'
source=workspace/'tools/mesa/panvk-native/native-surface-import.m'
binary=area/'native-surface-import'
subprocess.run(['/usr/bin/clang','-target','arm64-apple-ios16.5','--ld-path='+str(repo/'xnu-deps/host/bin/ld64'),
    '-isysroot','/home/scion/theos/sdks/iPhoneOS16.5.sdk','-O2','-Wall','-Wextra','-Werror','-fobjc-arc',
    '-I'+str(area/'native-src/include'),'-I'+str(source.parent),str(source),'-framework','Foundation','-framework','IOSurface','-o',str(binary)],check=True)
dest=workspace/'artifacts/star2lte-dev-ssh/surface-import-probe'
dest.write_bytes(binary.read_bytes())
report=dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),bytes=binary.stat().st_size,hardware_executed=False)
(workspace/'artifacts/star2lte-native-vulkan/native-surface-probe-build.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
