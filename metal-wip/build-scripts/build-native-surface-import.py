from pathlib import Path
import json,os,subprocess

repo=Path('/home/scion/scion-star2lte')
workspace=Path('/path/to/development-workspace')
area=repo/'xnu-deps/build/panvk-native'
assert json.loads((workspace/'artifacts/star2lte-native-vulkan/native-surface-fd-host-validation.json').read_text())['asan_ubsan_passed']
env=dict(os.environ)
env['PATH']=str(repo/'xnu-deps/build/mesa/venv/bin')+':'+env['PATH']
log=area/'surface-import-build.log'
with log.open('w') as output:
    result=subprocess.run(['ninja','-C',str(area/'build-ios'),'-j4'],env=env,stdout=output,stderr=subprocess.STDOUT)
print('\n'.join(log.read_text().splitlines()[-25:]))
assert result.returncode==0
subprocess.run(['python3',str(workspace/'tools/wsl/audit-native-vulkan.py'),'--suffix=-r11'],check=True)
exports=subprocess.check_output(['llvm-nm','--defined-only','--extern-only',str(area/'build-ios/src/panfrost/vulkan/libvulkan_panfrost.dylib')],text=True)
assert all(' _hakios_native_surface_fd_'+name in exports for name in ['create','release','lookup'])
print('Native IOSurface FD entry points exported; R11 audit passed; hardware not yet tested.')
