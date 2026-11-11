# 充电模式与无 Home 键布局续档（2026-11-09）

完整 Goal active：先充电/基础外设，GPU最后。用户新增旁路、80%上限、40–60%养护，以及底部导航条/上滑解锁。
驱动接口优先已实机成功的sheng，SM8750他人移植只采用已验证功能；Y700寄存器/板级行为优先ArmadaOS指定内核，次为mainline。
**Y700双口充电策略以本机原厂协议、HAL和实机证据为准。** Armada最新README也列充电/双口尚未完善，不当现成完成品。

## 现场与完成情况

最近已恢复stock boot8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890，Android/root/充电已确认。
所有boot仍1738713600@footer0/父链3；没有GPT、根卷、userdata或身份区写入。
前续档research/HANDOVER-POWER-UCSI-20261108.md的实机链已完成：

- UCSI v1.1初始化解决原ffffffff错误，普通PMIC通知注册通过。
- USB配置声明/原厂ICL500mA均实测；由约+0.90A净放电降至约+0.42A，**仍未净充电**。
- PMIC_LOGS_ADSP_APPS独立端点实机通过，14轮内部票据；i_sdp500、fcc_1=13000、fv4500。
- 实机GPIO归属：global515=TLMM3，541=TLMM29数据mux，728=PM8550 SID1 GPIO1；未写这些GPIO。
- v8测试镜像86b39fe5…/session1cf636b1034b6967，Root/Data/PID1正常、dropped0、RPC回程通过。
- v7仅延时版发现路由根因后没有刷；不能把它当已试变体。

## 原厂控制对照：已做并恢复

真实设备/vendor/bin/hw/vendor.lenovo.hardware.battery-service，SHA9f0ea0a6…，原件/mini-debug符号及反汇编在research/charge-features-20261108。
字段映射经构造器与方法核对：

| HAL | sysfs | Lenovo GLINK property |
|---|---|---|
| setBatteryChargeDisabled(bool) | charging_enable，写1 & ~disabled | 28 |
| setBatteryProtectedLevel(int) | protection_setting | 29 |
| setMaxBatteryChargingLevel(int) | protection_setting_eu | 30 |
| setBatteryRechargingPercent(int) | recharge_setting | 31 |
| setBatteryMaintenanceEnabled(bool) | battery_maintenance | 32 |
| setBatteryMaintenanceEnabledV2(bool) | battery_maintenance20 | 33 |

本机没有HAL通用bypass_setting节点，不套其他机型的虚拟旁路接口。
GET0x60/SET0x61、owner32778/type1/request24/reply24沿用原厂协议；当前新增控制只写28/29。

1. edl_work/charge-disable-oem-20261109：charging_enable从1短暂变0后，聚合与两FG电流均0，USB仍online1且有约59mA输入。
   之后精确恢复1，重新开始充电。对照不是按字段名字推断旁路。
2. edl_work/charge-window-oem-20261109：真实SOC约50%，135040保护窗让固件停止充电、charging_enable变0；恢复095100成功。
3. edl_work/charge-window-resume-oem-20261109：135040停止后，175080让固件自动恢复约-481.5mA充电；末尾095100/enable1恢复通过。

保护窗编码为启用标志×100000＋下限×1000＋上限；已验证的保守上下阈值变更支持该协议。
80%候选使用176080（76–80），40–60使用140060。尚未等待真实电量跑完整个40–60或80%周期，不能夸称长期验收完成。

## 新内核代码与即将测试项

drivers/EldenPmicGlink/ChargePolicy实现原始窗口捕获、恢复、旁路、80、40–60。
只新增GET29/30和SET28/29；**不再次GET28**，使其与既有BMS轮询不存在同请求迟到响应歧义。
SET28用之后两轮完整BMS观测确认；窗口需独立GET读回，保留EU更严格限制，失败不盲重试。
146项ASan/UBSan通过；额外主机序列检查拒绝“ACK就算应用”及旧generation造成的假恢复。

USB命令为charge SESSION stock|bypass|80|40-60，只排队到PMIC线程；status含charge_control、charge_mode、charge_generation。
原fastboot命令保持。elden_usb_console.py新增显式--charge-sequence，必须以stock结束；每项确认新的应用generation后再计保持时间。
失败会尝试一次stock恢复，再走已验证回程；遇恢复未确认，必须核对/恢复stock，不能宣传成功。

当前待测构建为power runtime v12、basic bundle v10；v11/v9已构建但未刷，随后对齐原厂SET应答规则，目录日期仍沿用20261108以保持原工单路径。
当前正在native重链；候选准备好后仍需fresh电压>=3500mV且原生battery-soc-ok=yes，再自动刷/测。
固件温控、FV、校准、fake SOC及身份命令不在新控制面内。电池发布器目前仍默认off。

## 手势配置

参考本地UniXNU guide docs/zh/guide/mobilegestalt.md及sheng make-sheng-adt.py。
原ADT为iPad7,1/J120/pad，但没有buttons节点。新补home-button-type=2、hold/voldown/volup（无menu）及thin-bezel=1。
没有为了UI去更换硬件compatible、设备身份种子或内存描述；这不是物理Face ID认证。
签好的独立ADT候选aaac30ab…/sessionb5d5b060c8fb0e1a尚未刷；计划与新充电控制核同发，减少写入。
原512MiB RAM Data seed在Mac本项目内read-only挂载检查，没有MobileGestalt缓存，已卸载。
seed-cache-audit.json证明packed SHA483125a2…与当前候选相同；不需要改系统根卷plist或清空Android数据。
触摸硬件仍待验，出现导航条不能替代真实上滑验收。

## 参考仓同步

40个克隆已检查：12快进、16已最新（含HTTP/1.1重查后的tb322fc内核）、9固定检出仅fetch、2本地改动仅fetch。
UniXNU-piano原远端不存在，旧副本保留。完整与补查报告分别在edl_work/reference-sync/20261108-154118和20261108-160025。
同步脚本edl_work/sync_reference_repos.py，默认只列计划，--apply快进干净跟踪分支；不reset/stash/切换固定版本/推送。
Codex线程heartbeat“Y700参考仓同步”（id y700）每6小时运行；参考内容仍统一research。
当前Armada885fd1fd、sheng9109d4ca、tb322fc v7.2.8-elden bf96c3d4。新参考提交不自动替换已冻结候选源。

## 原生五阶段实机结果与联动修正

当前boot_a为2916ea662b7746e04889cfdfccf07db13ad6089b9cca939f3753e620ee51c594，session8931266f53ba2c20，现场已自动回fastboot。
旁路→stock→80→40–60→stock五阶段均有新的applied generation、窗口/充电许可观测，末尾原窗口95100/enable1恢复，Root/Data/PID1及回程通过。
日志edl_work/charge-modes-live-20261109。该轮暴露ICL联动错误：禁充把USB输入500mA退到100mA，导致旁路净放电更大。
已去掉UsbIcl对charging_enable1的依赖，继续保留数据口/实际USB预算/输入挂起等门检。54项sanitizer通过。
正在构建power v13/basic v11再验，原v12/v10保留。不要把第一轮控制写入成功等同于输入功率问题已解决。

## 最新复测完成，已恢复 Android 补电

最终推荐实验件：edl_work/basic-bundle-20261108/v11/final/power-control-focused-gestures/boot-signed.img。
SHA256 **68c545e05371a608add74e1f7026d1f3ffba10c957de69b3b1a9e1bbfdf0c500**，session **eced71442fde8dff**。
kernel=dfc6b8db1c59bb72bb710e89c164055fe778423353f057cf7fa01a2e1b4d724c，78源冻结source-compiled-v11。
146 ChargePolicy/54 UsbIcl主机检查及原模块/ABI/资产/AVB核验通过。

复测edl_work/charge-modes-input-fix-20261109：旁路→stock→80→40–60→stock全部得到新的应用generation，
原窗口95100/enable1恢复确认，所有稳定段USB ICL均500000，新增ICL下降问题已修。日志1048456B，回程已确认。
完整结果charge-assessment.json；CPU/GOP运行时电脑SDP输入约503mA，电池仍约+0.39..0.52A净放电。
因此当前实现的是控制与保护模式，不能说双口快充、任意负载下零电池电流或完整电量周期已验收。
旁路在原厂低负载的零电池电流已测；XNU负载需要更强输入，两个结论分开保存。

最后已把stock8b8e72da…写回boot_a，完整读回通过。恢复记录edl_work/charge-modes-final-stock-20261109。
下一发reference应使用stock控制件，不能误用仍以为在盘上的68c5实验件。没有剩余原生构建/监听/EDL写入。

复用命令：elden_usb_console.py --listen --expect-session SESSION --status-interval 3 --seconds 420
--wait-seconds 300 --reboot-after-ready --reboot-delay 240
--charge-sequence bypass,stock,80,40-60,stock --charge-delay 65 --charge-hold 10 --output NEW_DIRECTORY。
完整参数按一行运行；stdout可重定向listener.txt，raw另存。Windows分析脚本assess_charge_modes.py RUN_DIRECTORY。

后续重点：双口PD输入选择；依据原厂票据和本机Linux审核AP侧温控/背光/功耗；启用已做好的真实电池发布器；
再推进触控/Wi-Fi/蓝牙。无Home键ADT已随两轮模式测试启动，视觉导航条与真实上滑仍待反馈。
不要用GPIO541充电切换（它是数据mux），也不要仅凭PD协商状态或USB online宣布在充电。

最后Android复验：boot_completed/root均通过；capacity=57%，voltage=3992000uV，current=-480000uA，status=Charging，charging_enable=1。Goal仍active。
