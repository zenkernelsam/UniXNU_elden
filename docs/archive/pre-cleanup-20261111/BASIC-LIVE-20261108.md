# 基础功能合并核实机续档（2026-11-08）

用户已回家，明确授权恢复真机测试；随后要求按默认倒数→入核→PC日志→串口回fastboot无人值守迭代。
一公两母PD转接器已由用户要求搁置，暂只验证原生长边数据/短边PD。

## 当前现场

最后已刷boot_a为power-focused，SHA
`7becc8c22347680a9c76f9b45c433940c52723983ab9e4154922a77fc1d1c99d`，
session `426a5c7fdfe2656c`，stock **1738713600@footer0/父链3**。
整像设备SHA回读通过，现场GPT仅检查未修改；末轮已通过RPC自动回fastboot。
没有未结束的EDL写入。完整日用Goal active，充电尚未验收。

合并核 SHA `7bb8c9c5bf36e6e294cfa89d5b78f203d09ca59802ccc8f1076d5f8825e519d4`，52609128B。
已包含M3原生电池/ADSP/45字段/UCSI、NT36536/SRAM/HID，以及Wi-Fi只读原厂固件gateway。
源在 `research/basic-bundle-20261108/source-compiled-v2/`，61份实际成功输入；
原30执行模块、HID207/52与私有220/37槽、原FD/ADT/trust/Data/6GiB全部核对通过。
仍无charger SET、VBUS/FET切换、原生网络发布或Bluetooth daemon启用。

## 这轮新实机证据

- 起始fastboot原生battery-soc-ok=yes、4201mV，Androidboot_completed及KSU root通过。
- Android产品名实际为TB322FC，采集器白名单已修正；adb exec-out su -c sh的stdin EOF不可靠，
  已改为单个完整shell-quoted remote command，真实采集成功。
- 长边PC独供：81%，汇总current_now约-458..-467mA、statusCharging；两FG电流为正、电压约4.23V。
- 用户确认接回长边PC/短边PD后的双口采集：83%，负汇总电流约-456mA；
  USB SDP约4.83V/494mA、ICL500mA有效，wireless供电路径online0但测到约5.035V。
  这是原厂输入选择基线，不足以证明PD快充。两个数据目录 power-oem-both与power-oem-dualport；
  前者原标签是requested双口，用户事后确认实际仅长边PC。
- 合并all-off首轮ROOT/Data/PID1/HID/USB正常，RPC完整sync后回fastboot。
- 15项长菜单存在人工辨认/选择疑问，未将先前selected项当确定证据；改为focused三项菜单，
  第一项自动默认，避免依赖人工选项。驱动代码完整相同，未缩减Goal范围。
- firmware-focused（269c542c…/9a3d140db3ce8f71）自动入核实读ADSP23066968B、DTB167736B，
  两件SHA9edad6…/72b99f…全过，回程ACK/SDAM reset完整。
- survey-focused（9311479d…/35c24d210f1b8ecc）自动调查，stage2/error0，
  slaveffffffff、items30c、QMP1700、RSC30800，未启动DSP，自动回fastboot通过。
- power-focused自动原signed DSP启动成功，PMIC45项连续50周期、valid1fffffffffff。
  末周期aggregate4091mV、current+833..900mA，FG负电流，温度约35.3C；
  voltage初始4000mV/SOC50%等初始化值后逐步变为真实FG值，不能立即发布初始默认读数。
  USB约5.5..5.6V、ICL100mA、USB1/2online1、Lenovo55=0，短边另一路约5.07V但online0。
  与原厂基线/汇总及双FG符号须继续校验，未据此宣称在净充电。
- UCSI读出version110/buffer48，之后errorffffffff并停；PMIC仍持续。需要核对PPM初始化/命令兼容，
  不将v2 SET_SINK_PATH或未知Lenovo55 SET作为已可用控制。
- 末轮捕获923740B、dropped0，ACK fastboot并RESET_AFTER_SYNC route=sdam-v7；设备现fastboot。

原始日志分别在 basic-off-live、basic-auto-firmware、basic-auto-survey、basic-auto-power-20261108。
请读取各packet.json的当前session，不能套旧token。

## 下一步

先保留现在快充未验的判断，解决原signed ADSP启动后的充电状态和UCSI v1.1 query错误。
需要对照原厂PPM_RESET/notify流程、双口输入选择/Lenovo55和aggregate/FG电流方向；
不盲写温控/电压/保护或合并VBUS。电量不足时按精确stock恢复件回Android充电。
供电/周期快照资格后才启用IOPMPowerSource，再在同核按identify/SRAM/touch逐档启用触控。

packager支持 --default PROFILE --focused；保持第一项xnu、8秒默认入核，三项菜单。
elden_usb_console新增 --reboot-delay（只允许配合显式--reboot-after-ready），
仍要求同session、健康根/Data/PID1、fastboot=ready后只发一次RPC；延迟让诊断日志收齐。
本轮FW/survey用120秒、power用180秒，不手动点击也已复验回程。

Wi-Fi C只读文件代码已在实际modem_a备份上215项ASan/UBSan通过，七文件尺寸/SHA全对。
其固件检查入口已合并、默认off，真实Wi-Fi fw检查/MHI/PCIe/SID1401/MSI/IO80211/WPA尚未测试。
