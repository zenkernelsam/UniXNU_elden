"""Freeze the visually confirmed Settings milestone and its exact evidence."""
from pathlib import Path
import hashlib,json,re
from PIL import Image
from avb_guard import check_image
from decode_frame_preview import decode
from watch_icon_trial import rgb
r=Path(__file__).resolve().parents[1];run=r/'edl_work/live-graphic-cfcolor-v91-20261111';out=r/'research/settings-baseline-20261111';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=r/'edl_work/basic-bundle-20261108/v91/final/icon-cpu-focused-gestures-ipad-pro-cpu8/boot-signed.img';signed=check_image(image,'boot_a')
assert signed.sha256=='879ef26b2f438bc63856769deee0d1b0790d51a55b30bcc57f5fb92d34ca746f' and signed.index==1738713600 and signed.location==0
flash=json.loads((run/'flash/result.json').read_text());assert flash['success'] and flash['full_readback_sha256']==signed.sha256
packet_path=image.parent/'packet.json';packet=json.loads(packet_path.read_text());session=packet['session'];assert session=='6c0b187edabfa6cf'
log=next((run/'usb').glob('*.bin'));raw=log.read_bytes();meta=json.loads(log.with_suffix('.json').read_text());assert sha(log)==meta['raw_sha256']
assert not meta['discontinuities'] and not meta['reconnections']
frames=decode(raw,session);entry=next(x for x in frames if x[0]==10);verified=rgb(entry[2]);seen=Image.open(run/'trial-frames/after-tap-10.png').convert('RGB');assert seen.tobytes()==verified.tobytes()
verified.save(out/'settings.png');verified.resize((714,1140),Image.Resampling.NEAREST).save(out/'settings-3x.png')
text=raw.decode(errors='replace');glyph=[{'pid':int(x[1]),'stage':int(x[2]),'data':int(x[3],16)} for x in re.finditer(r'ELDEN_GLYPH pid=(\d+) stage=(\d+) data=([0-9a-f]+)',text)]
renders=[x for x in glyph if x['stage']==2];assert len(renders)==8 and {x['pid'] for x in glyph}=={108} and [x['stage'] for x in glyph].count(1)==1
assert any(x['data']&(1<<35) for x in renders) and all(x['data']>>40==0 for x in renders)
trial=json.loads((run/'trial.json').read_text());assert sum(x.get('command')=='tap-settings' and x['kind']=='request' for x in trial['events'])==1
assert 'ELDEN_UI_REPLAY settings down=0 up=0' in text and 'ELDEN_RPC RESET_AFTER_SYNC route=sdam-v7' in text
restore=r/'edl_work/graphic-cfcolor-v91-stock-20261111';rest=json.loads((restore/'flash/result.json').read_text());android=json.loads((restore/'android-final.json').read_text());assert rest['success'] and rest['full_readback_sha256']=='8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890' and android['root'] and android['boot_completed'] and android['verifiedboot']=='green'
paths=[image,packet_path,image.parent/'independent-audit.json',run/'flash/result.json',log,log.with_suffix('.json'),run/'trial.json',run/'trial-frames/after-tap-10.png',restore/'flash/result.json',restore/'android-final.json',r/'edl_work/icon-dependency-20261111/v91/EldenIconCpuMode.dylib',r/'edl_work/basic-bundle-20261108/v91/glyph-codegen.json']
report={'milestone':'One software swipe and short Settings tap reached populated Settings/General UI','visual_assessment':{'observer':'assistant via view_image','confirmed':True,'details':'Settings title, populated colored sidebar, General selection and populated right pane','frame_id':10,'frame_crc32':hex(entry[1]['end']),'sampled_dimensions':[238,380]},'image_sha256':signed.sha256,'kernel_sha256':packet['kernel_sha256'],'session':session,'glyph_pid':108,'successful_raster_events':len(renders),'cf_color_identity_observed':True,'complete_fault_lines':text.count('ELDEN_FAULT pid='),'nonzero_icon_exit_lines':[m[0] for m in re.finditer(r'ELDEN_ICON_EXIT pid=\d+ rv=([0-9a-f]+)[^\r\n]*',text) if int(m[1],16)!=0],'whole_capture_dropped':max(int(x['dropped']) for x in meta['statuses']),'software_settings_operation_passed':True,'physical_touch_verified':False,'full_goal_complete':False,'limits':['single boot/operation, not all workloads','some unrelated log bytes dropped; CRC frame and counted raster records retained','extra CI effects and 8-bit output limitations remain','display performance, physical gestures and remaining peripherals still pending'],'current_device':'restored stock Android, root/green/boot completed, sleep requested','evidence_sha256':{str(p.relative_to(r)).replace('\\','/'):sha(p) for p in paths}}
(out/'baseline.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('software_settings_operation_passed','successful_raster_events','complete_fault_lines','whole_capture_dropped','physical_touch_verified')},indent=2))
