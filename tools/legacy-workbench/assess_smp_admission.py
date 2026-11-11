"""Record bounded SMP admission from the exact flash, CDC capture and fastboot return."""
from pathlib import Path
import argparse,datetime,hashlib,json,re,subprocess

ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,value):path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('run',type=Path);p.add_argument('--cpus',type=int,choices=[2,8],required=True)
 p.add_argument('--qualification',type=Path,required=True);a=p.parse_args()
 run=a.run.resolve();metas=list((run/'usb').glob('*.json'));assert len(metas)==1
 meta=json.loads(metas[0].read_text());raw=metas[0].with_suffix('.bin')
 flashpath=run/'flash/result.json';flash=json.loads(flashpath.read_text())
 image=Path(flash['image']);packetpath=image.parent/'packet.json';packet=json.loads(packetpath.read_text())
 assert flash['success'] and flash['gpt_identity_checked'] and flash['reset_sent']
 assert flash['serial']=='HA2A1PT2' and flash['index']==1738713600 and flash['footer_location']==0
 assert sha(image)==flash['image_sha256']==flash['full_readback_sha256']
 assert packet['cpu_guard_cpus']==a.cpus and packet['cpu_guard']['kernel_sha256']==packet['kernel_sha256']
 assert sha(image.parent/'kernel.macho')==packet['kernel_sha256']
 assert sha(raw)==meta['raw_sha256'] and raw.stat().st_size==meta['bytes']
 assert not meta['discontinuities'] and not meta['reconnections'] and not meta['control_events']
 assert 'reboot_requested_utc' not in meta and 'pre_reboot_log_bytes' not in meta
 assert not meta.get('oversized_console_lines_discarded')
 statuses=meta['status_samples'] if 'status_samples' in meta else meta['statuses']
 assert statuses and all(int(x['dropped'])==0 and x['session']==packet['session'] for x in statuses)
 events=meta['recovery_events'];lines=[x['line'] for x in events]
 text=raw.read_text(encoding='utf-8',errors='replace')
 assert 'panic(cpu' not in text and not any('FAILED' in x or 'REFUSED' in x for x in lines)
 assert any('ELDEN_VDOG SMP_ARM' in x and 'rc=0 state=1 ctl=1' in x for x in lines)
 mask=f'{(1<<a.cpus)-1:x}'
 end=f'ELDEN_SMP action=2 phase=3 reason=0 expected={mask} online={mask} work={mask} timers={mask} ipis={mask} error=0 vdog=1'
 assert end in lines and 'ELDEN_SMP QUALIFIED_RETURN stop-pet; await fastboot' in lines
 start=next(x['elapsed'] for x in events if f'action=0 phase=2 reason=0 expected={mask} online={mask} work={mask} timers={mask} ipis={mask} error=0' in x['line'])
 finish=next(x['elapsed'] for x in events if x['line']==end);assert finish-start>=4.9
 cores={};affinities=[0,0x100,0x200,0x300,0x400,0x500,0x10000,0x10100]
 for i in range(1,a.cpus):
  rows=[re.fullmatch(r'ELDEN_SMP CPU id=(\d+) step=(\d+) fail=(\d+) psci=(-?\d+) physical=([0-9a-f]+) timer=(\d+) sgi=(\d+) work-mpidr=([0-9a-f]+)',x) for x in lines]
  rows=[r for r in rows if r and int(r[1])==i];assert rows
  last=rows[-1];assert list(map(int,[last[2],last[3],last[4]]))==[7,0,0]
  assert int(last[5],16)==affinities[i] and int(last[8],16)&0xff00ffffff==affinities[i]
  assert int(last[6])>int(rows[0][6]) and int(last[7])>int(rows[0][7])
  cores[str(i)]={'affinity':last[5],'work_mpidr':last[8],'timer':int(last[6]),'sgi':int(last[7])}
 fastboot=Path('D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/fastboot.exe')
 observed=subprocess.run([str(fastboot),'devices'],capture_output=True,text=True,timeout=15,check=True)
 assert re.search(r'^HA2A1PT2\s+fastboot\s*$',observed.stdout,re.M)
 fb=run/'fastboot-observed.json';write(fb,{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'devices':observed.stdout,'y700_fastboot':True,'no_pc_reset_request':not meta.get('reboot_request') and not meta.get('reboot_requested')})
 assert json.loads(fb.read_text())['no_pc_reset_request']
 assessment={'qualified':True,'cpus':a.cpus,'mask':mask,'cores':cores,'stable_seconds':finish-start,'bytes':meta['bytes'],'dropped':0,'raw_sha256':sha(raw),'kernel_sha256':packet['kernel_sha256'],'candidate_sha256':sha(image),'session':packet['session'],'fastboot_observed':True,'pc_reset_requested':False,'limits':['bounded admission, not long-term or full-load stability','UI interaction not assessed','Android restoration pending']}
 assessmentpath=run/'assessment.json';write(assessmentpath,assessment)
 evidence=[raw,metas[0],flashpath,packetpath,fb,assessmentpath]
 proof=dict(assessment,evidence_sha256={str(x.relative_to(ROOT)).replace('\\','/'):sha(x) for x in evidence},cpu_guard=packet['cpu_guard'])
 a.qualification.parent.mkdir(parents=True,exist_ok=True);write(a.qualification,proof)
 print(json.dumps(assessment,indent=2))
if __name__=='__main__':main()
