from pathlib import Path
import json,hashlib,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/'research/cpu-guard-20261111';t=p/'tree';out=r/'out/startup-cpu-guard-v81-20261111'
report=json.loads((p/'native-audit/result.json').read_text())
for group,name in [('armcpu','ArmCpu'),('ufs','ShengUfs')]:
 folder=t/('build/armcpu' if group=='armcpu' else 'build/sheng-ufs')
 assert hashlib.sha256((folder/(name+'.o')).read_bytes()).hexdigest()==report['objects'][group]['object_sha256']
links=t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.link'
files={'kernel.macho':out/'kernel.macho','kernel.json':out/'kernel.json','result.json':p/'result.json','native-audit.json':p/'native-audit/result.json',
       'layout.json':p/'audit/layout.json','layout.s':p/'audit/layout.s','linkarchives.txt':links/'mach.development.bcm2837.linkarchives',
       'ArmCpu.cpp':p/'ArmCpu.cpp','EldenSdamRecovery.inc':t/'build/sheng-ufs/src/EldenSdamRecovery.inc'}
with zipfile.ZipFile(p/'audit-export.zip','w',zipfile.ZIP_DEFLATED) as z:
 manifest={}
 for n,f in files.items():z.write(f,n);manifest[n]=hashlib.sha256(f.read_bytes()).hexdigest()
 z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'bytes':(p/'audit-export.zip').stat().st_size,'sha256':hashlib.sha256((p/'audit-export.zip').read_bytes()).hexdigest()}))
