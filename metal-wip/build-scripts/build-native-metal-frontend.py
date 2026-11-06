"""Compile the existing METAL=vk Objective-C frontend using the iOS SDK on Linux."""
from pathlib import Path
import argparse,subprocess,json,hashlib,re
parser=argparse.ArgumentParser();parser.add_argument('--diagnostic',action='store_true');parser.add_argument('--variant');args=parser.parse_args()
assert args.variant is None or re.fullmatch('[a-z0-9-]+',args.variant)
repo=Path('/home/scion/scion-star2lte');workspace=Path('/path/to/development-workspace')
core=workspace/'sources/HakiOS-Scion';area=repo/'xnu-deps/build/panvk-native';label=args.variant or ('diagnostic' if args.diagnostic else '')
out=area/('metal-frontend'+('-'+label if label else ''));out.mkdir(exist_ok=True)
sdk='/home/scion/theos/sdks/iPhoneOS16.5.sdk';linker=str(repo/'xnu-deps/host/bin/ld64')
sources=[core/'userland/src/metalvk/HakiosMetalVK.mm',core/'userland/src/metalvk/NativeBridgeMetalFrontend.mm',core/'userland/src/hakios_metal_shown.c']
if args.diagnostic:
 source=sources[1];text=source.read_text()
 marker='NmbFunction *handle=name?nmb_function_new(_handle,name.UTF8String):nullptr;gTimeFunction.add(functionBegan);\n if(!handle)return nil;'
 assert text.count(marker)==1
 text=text.replace(marker,'''NmbFunction *handle=name?nmb_function_new(_handle,name.UTF8String):nullptr;gTimeFunction.add(functionBegan);
 fprintf(stderr,"METAL_FUNCTION name=%s ok=%d reason=%s\\n",name.UTF8String?:"?",handle?1:0,handle?"":nmb_last_error());
 if(!handle)return nil;''')
 marker='if(error)*error=nbError(@"unqualified function constant values");return nil;'
 assert text.count(marker)==1
 text=text.replace(marker,'''fprintf(stderr,"METAL_FUNCTION_CONSTANT_ABI name=%s values=%p class=%s\\n",name.UTF8String?:"?",(__bridge void*)values,object_getClassName(values));
  '''+marker)
 marker='if(error)*error=nbError(@"unqualified constant value ABI");return nil;'
 assert text.count(marker)==1
 text=text.replace(marker,'''fprintf(stderr,"METAL_CONSTANT_VALUE_ABI class=%s\\n",object_getClassName(value));
   '''+marker)
 diagnostic=out/'NativeBridgeMetalFrontend.mm';diagnostic.write_text(text);sources[1]=diagnostic
common=['-target','arm64-apple-ios16.5','--ld-path='+linker,'-isysroot',sdk,'-O1','-Wall','-Wextra',
 '-I'+str(core/'tools/metal'),'-I'+str(core/'userland/src/metalvk')]
commands=[];objects=[]
for source in sources:
 obj=out/(source.stem+'.o');objects.append(obj)
 command=['/usr/bin/clang++' if source.suffix=='.mm' else '/usr/bin/clang',*common]
 if source.suffix=='.mm':command+=['-std=c++17','-fobjc-arc']
 command+=['-c',str(source),'-o',str(obj)];commands.append(command)
runtime=workspace/'artifacts/star2lte-native-vulkan/libmetal2vulkan-public-examples.dylib'
commands.append(['/usr/bin/clang++',*common,'-bundle','-o',str(out/'HakiosMetal'),*map(str,objects),str(runtime),
 '-framework','Foundation','-framework','Metal','-framework','IOSurface','-framework','IOKit','-framework','CoreFoundation','-lobjc'])
log=out/'build.log';result=None
with log.open('w') as output:
 for command in commands:
  result=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT)
  if result.returncode:break
print('\n'.join(log.read_text().splitlines()[-60:]))
report={'exit_code':result.returncode,'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
 'build_log':str(log),'hardware_executed':False,'objc_metal_discovery_verified':False}
if result.returncode==0:
 binary=out/'HakiosMetal';report.update(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),binary_size=binary.stat().st_size)
(workspace/('artifacts/star2lte-native-vulkan/metal-frontend-build'+('-'+label if label else '')+'.json')).write_text(json.dumps(report,indent=2)+'\n')
raise SystemExit(result.returncode)
