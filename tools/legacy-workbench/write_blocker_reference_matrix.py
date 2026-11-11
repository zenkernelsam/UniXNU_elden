"""Render the reviewed applicability decisions alongside captured Git identities."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1];out=root/'research/blocker-audit-20261111'
# These are review decisions, not generated claims of runtime success.
notes={
'Adreno740-a830':('NativeAdreno*.h; README.md','A740实机方法参考；Y700 A830不能直接用寄存器，GPU最后。'),
'Adreno740-ipados27':('README.md; 原分支与origin/main','保留两个版本；原跟踪分支已删除，新main不是本机已验候选。'),
'AdrenoMetal-a830':('README.md; Metal frontend','sheng图标正常方案依赖真实Metal后端；不是无GPU即用的软件修复。'),
'AdrenoPM4-night':('README.md; PM4 helpers','命令编码；与当前无GPU点按消费无直接因果证据，后置。'),
'armada-tb321fu':('README.md; packages/kernel','TB321FU是SM8650/第三代；不能照搬其电源或触控接线。部分blob缺失已记录。'),
'armadaos-freedom':('devices/lenovo-tb322fc/README.md; kernel patch索引','本机Linux入口；运行时RAM/保留区交接不能用静态DTS替代。部分blob缺失；精确本机驱动另从tb322fc仓读取。'),
'ArmSmmu4K-night':('README.md; translation helpers','DMA生命周期/cache/翻译方法；现有SID/CB隔离保持，不以全局bypass治UI卡顿。'),
'data-a-live':('README.md; tools/metal/adapt_ios16.py; wsl/pandora_touch.c','SM8850/iOS16；GPU合成约110fps不证明无GPU方案，仍记录偶发数秒卡顿。方法可用，ABI/接线不可直接用。'),
'droidspaces-kernel':('README.md; build config','原厂Android恢复/KMI参考；无XNU UIKit阻塞修复。'),
'edk2-gunyah':('README.md; firmware sources','虚拟平台UEFI，属于入核前；不解释已到SpringBoard的点按。'),
'hackios-xnu-kernel-sm8750':('README.md; xnu/files/bsd/miscfs/ramfs/ramfs.c','ramfs wired页刷新可修图标旧缓存，但本机是APFS RAM Data而非其ramfs；SM8750分支明确无实机，不能直接套。'),
'ltbox':('README.md; flashing source inventory','Android/EDL管理工具，不是用户态驱动；本轮不刷写。'),
'mu-silicium':('Status.md; UEFI/DT source inventory','固件入核前和资源交接参考；没有当前UIKit故障的直接修复。'),
'mwp':('README.md; Platforms/Silicon inventory','UEFI平台和遗留SoC资源；本轮仅范围筛查，不用作本机用户态/充电实现。'),
'NativeAdreno':('README.md; fence/queue helpers','GPU调度/内存与等待方法；当前未启GPU，不照搬其队列瓶颈解释。'),
'NativeAdreno-myron':('README.md; driver source inventory','A740 GPU fork，shader/scanout问题与当前无GPU路径不同。'),
'partclone':('README; filesystem utilities','备份/分区工具；当前无分区任务、无UIKit直接答案。'),
'QcaBluetooth':('README.md; origin/main 986d631','UART停钟后排队控制字节修复值得后续用；原生bluetoothd适配为24A446，不能直接换进21H461。'),
'QcomDwc3':('README.md; origin/main; XDCI/XHCI source','原Apple USB栈适配为iPadOS27；本机已有独立日志/RPC，不替换正在工作的回程通道。'),
'qdlrs':('README.md; AGENTS.md; transport inventory','Sahara/Firehose，与当前UI点按不直接相关；未改代码或提交。'),
'ShengHidInput':('NativeHidInput.cpp; 5f3a37c..1140614','最直接规范。v72 touchSend及descriptor已与旧pin对应；新提交主要PAC/macOS与参数改名，未找到新的legacy点按药方。'),
'ShengNovatekTouch':('NvtTouchCore.c; NvtProtocol.c; README.md','NT36532 40x60原始矩阵跟踪与抬指已具参考价值；Y700 NT36536点报告协议不同，不能替换其算法。'),
'ShengPreflight':('README.md; board preflight','固件准入检查；当前已入用户态，不当作UI修复。'),
'ShengUfs':('README.md; RootCommands/RootQueue; recent commit evidence','独立bounce/多槽/IRQ完成可借鉴；34ff541新等待未实机，本机8.7GiB读校验约24秒，不支持当前白窗=UFS慢。'),
'tb322fc-linux-kernel':('sm8750-lenovo-elden.dts; novatek-nvt36xxx-spi.c; input-mt.c','bf96c3d4的本机SPI4/IRQ162/NT36536/19040x30400与Linux slot/tracking分离为板级依据；不凭源码当Y700 XNU已验。'),
'testkey-signer':('README.md; signing scripts','签名工具；floor保持，不涉及用户态阻塞。'),
'twrp-device':('README.md; BoardConfig/fstab inventory','恢复/Android参考；当前不改启动链与分区。'),
'uefi-sheng':('README.md; HEAD与origin/main','SM8550固件和保留内存方法，不套Y700地址或换已验FD。'),
'UniXNU-dada':('README.md; package/BootWatchdogPolicy.c','SM8750同类板、共用sheng隔离策略；删服务仍不是驱动完成。'),
'UniXNU-erhai':('README.md; board/config inventory','SM8750P平台入口阶段资料；不足以作为已成功触控/用户态阻塞修复。'),
'UniXNU-hummer':('README.md; GSI-PORT-PLAN.md; NativeLegacyDisplay.cpp','同21H461，记录6核/触屏成功；S3910与Y700 NT36536不同，借鉴HID/显示分层和SMP方法。'),
'UniXNU-myron':('docs/drivers/native-novatek-touch.md; component docs','框架/Novatek集成规范补充，不能据相似名字复制本机硬件参数。'),
'unixnu-nativemobiledisplay':('NativeLegacyDisplay.cpp; NativeLegacyRequest.h; README.md','无IRQ时work-loop软件vsync是现成方案，v72已用60Hz；新SM7325 DPU直扫/GPU fence不是本机即用修复。'),
'UniXNU-Odin3':('README.md; porting.toml; OdinTouch.cpp','同21H461/SM8750但microSD与不同触控；作为分层适配参考，不导入其存储/针脚。'),
'UniXNU-Pagani-Drivers':('PaganiInput/{README.md,InputScript.h}; Adreno830/docs/ARCHITECTURE.md','现成非阻塞脚本输入、明确释放/真实输入抢占；GPU缺client导致CI回落空GL的实证。借方法，保留Y700报告和生命周期。'),
'UniXNU-Pagani_SM8750':('phone-keyboard-trace.patch; phone-stall-probe.patch; phone-gpu-client-memlimits.patch; porting.toml','同21H461 UIKit现有kdebug事件可查输入消费。键盘touchUp并非通用触屏up；GPU jetsam/statfs回归均需本机对应证据。'),
'UniXNU-piano':('README.md; two retained refs','SM8750平板配置参考；远端不可用，保留已有源码，不当新成功驱动。'),
'UniXNU-sheng':('BootWatchdogPolicy.c; reports/ipados27-touch-frame-gaps.md; porting.toml','直接命中CI/XPC。实测约4.4秒等待；完整图标方案实际用A740 Metal。lite=2开设置但图标全丢，不采作修复。'),
'UniXNU-sm8550-common':('README.md; CPU/GIC/UFS resources','方法与共享组件接口；MPIDR/IRQ和IPI workaround均需Y700版，不能直接smp=1。'),
'UniXNU-sm8750-common-pagani':('README.md; dtsi; kits source','19.2MHz/GIC/UFS资源重要；板级实际资源/所有权继续以TB322FC证据为准。'),
'UniXNU_Enc3l-night':('docs/porting/input.md; guide/mobilegestalt.md; drivers/sheng-hid-input.md; device records','官方规范：同OS ABI、coherent contacts与release、独立功能验收。Pro身份/vsync/报告修复已在机，不重复当新功能。'),
}
rows=json.loads((out/'repositories/index.json').read_text())
assert {Path(x['repo']).name for x in rows}==set(notes)
lines=['# 阻塞参考仓逐仓适用性，2026-11-11','',
'范围：41个Git克隆、47个检索树；HEAD和已有跟踪/默认分支对象逐仓索引，所有远端tip也记录。不是逐行审计所有Linux/UEFI代码。关键命中已续读源代码、官方记录和本机冻结实现。没有更新候选pin。',
'','两份Armada稀疏仓报告缺失blob，不能把git grep的退出码0当完整覆盖。相关本机驱动从已有tb322fc-linux-kernel对象读取；其余缺失项不宣称已审核。初次宽检索触发Git隐式补blob后超时；最终审查禁用lazy fetch并保留缺项。',
'','| 仓库 / 检索版本 | 重点文件或范围 | 结论 |','|---|---|---|']
machine=[]
for row in rows:
 name=Path(row['repo']).name;paths,decision=notes[name]
 shas=', '.join(x['sha'][:12] for x in row['reviews'])
 lines.append('| '+name+' / '+shas+' | '+paths+' | '+decision+' |')
 machine.append(dict(repo=row['repo'],revisions=[x['sha'] for x in row['reviews']],reviewed_scope=paths,decision=decision,
                     partial=any(x['search_errors'] for x in row['reviews'])))
lines+=['','补充非克隆参考：lilac-archive/AGENTS.md按HID/显示/日志相关段落检索；其447KB历史日志未逐行重读。保留“先核IOMFB/IOSurface及用户态退出，再怪触控IC”的方法，RMI4/MSM8998地址不复用。',
        '','复现索引：repositories/index.json、各仓matches/files/overview；所有判断以对应SHA为准。具体阻塞分级与后续实施见 ../BLOCKER-UPSTREAM-AUDIT-20261111.md。']
(out/'REFERENCE-MATRIX.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(out/'reference-decisions.json').write_text(json.dumps(machine,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Wrote 41 repository decisions; no source or candidate changes.')
