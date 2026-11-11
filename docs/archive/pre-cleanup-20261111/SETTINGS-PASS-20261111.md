# v91里程碑：软件上滑与短按进入有内容的Settings

2026-11-11。完整基本外设Goal仍active，不能清旗。当前“短按Settings得到真实内容”这一子项已在一次实机操作中通过；实指、流畅度和其它外设未因此完成。

## 已通过的实际操作

`live-graphic-cfcolor-v91-20261111`由单watcher确认CRC锁屏、发一次软件上滑，确认Settings位置后等待45秒，发一次100ms短按。

- after-tap10的完整CRC画面显示Settings标题、彩色侧栏、选中的General及右侧实际列表内容，不再只是白页。
- 新独立标记：PID108一次`ELDEN_GLYPH stage=1`，8次stage2，尺寸均58×58，error字段0；7次data=d003a003a证实CFColor身份分支及着色，1次data=4003a003a没有指定前景色。
- 本轮没有完整ELDEN_FAULT行、没有记录到非零IconServices退出；标记PID保持108。全日志有24字节drop，不能将“没采到故障”扩成所有工况保证。真实位图、页面内容及本轮稳定进程是正面证据。
- HID down→up101065µs，UIKit两组99898µs，原事件tick差换算101067.19µs，窗口四条完整，自动分析和视觉验收分开保存。
- 240秒hold及PC ACK/sync/SDAM回实际fastboot正常；随后完整恢复Android。

冻结清单：[settings-baseline-20261111/baseline.json](settings-baseline-20261111/baseline.json)。截图为同目录`settings.png`；`settings-3x.png`仅对1:8原采样图做3倍最近邻放大，没有生成新画面内容。

## 最终修复及边界

真实故障路径是系统符号资源在ISCompositor的CI模式判断之前进入CoreUI GraphicVariant→sharedCIContext→GL。保留原mode0补丁，并为精确21H461目标方法提供真实CoreGraphics背景/符号合成。

目标方法、options、前景色getter和备用CUIColor.CGColor getter均核原UUID/IMP/机器码/type encoding，只有显式开启的iconservicesagent加载。返回真实拥有的CGImage；处理位置、source/output scale、alpha和前景色，输出有界。

v90已安装方法，但所有绘图都因颜色检查stage3/data16(hex)=22被拒绝，随后走原GL并崩。这里更正先前推断：**`id`编码不等于CUIColor对象。** v91用CGColorCreate创建真实参考值取得桥接Class，再用类关系和CFGetTypeID==CGColorGetTypeID验证输入，之后才转CGColorRef。保留已核的CUIColor getter分支，不猜类名或盲转。实际v91 stage2/bit35确认了CF分支；Mac真实CF值测试也验证接收CGColor，拒绝CFString/色彩空间/nil。

原CoreUI外部颜色调用的离线符号解析碰到共享stub范围，`color-imports/symbolication.json`没有给出函数名，**不当作那些BL目标已逐个符号确认**。运行时CFType身份和实际绘图已提供更直接的本机证据。

CoreGraphics核心已做实际Mac ASan/UBSan绘图/颜色/生命周期检查，并原生编译iOS17 arm64。额外CI content/shape阴影等效果没有完整实现，输出为8位兼容路径；这不是最终效果parity。GPU最后阶段需补齐效果并明确撤销/关闭临时CPU兼容选项，不能让它永久遮住完整GPU路径。

## 可复用的冻结构件

- v91内核：`2d72dea2e1b69adf063a9db2b91b06cb16e1bc880425a868f62f6611989efe69`。
- boot：`edl_work/basic-bundle-20261108/v91/final/icon-cpu-focused-gestures-ipad-pro-cpu8/boot-signed.img`。
- boot SHA：`879ef26b2f438bc63856769deee0d1b0790d51a55b30bcc57f5fb92d34ca746f`。
- 库SHA：`f748e9cf601db43167eaf7736b0a161771b8c01eeb46860820604ccf892ac034`；agent代码/entitlements不变。
- session：`6c0b187edabfa6cf`；原始日志5042424B，SHA `dce87b250805f331a541d470909a0812df78cbaa6be7258007eec8b3bfb53dd2`，无断段/重连。
- footer1738713600@0，父链保持；原30模块/52HID、210来源及硬件驱动归档不变，所有写入完整SHA核对。
- 新代码在`package/EldenGraphicVariantHook.c`、`EldenGraphicVariantCpu.c/h`和独立内核ELDEN_GLYPH日志分支；源码和资格均冻结到v91。

v90的失败像/结果保留，不当成功基线。v91只通过这一工况，不能外推多点触控、拖动、睡眠、音量键或其它应用。

## 下一步单主工单

1. 从v91基线继续**实际触控和无GPU显示流畅度**。实指需用户在场集中做上滑、Settings短按、滚动、抬指和返回；用户不在时继续离线/已有自动化可验的依赖，不伪造物理输入结论。
2. 已有整屏CPU copy约0.7秒的独立线索，先完成本机帧缓冲所有活动别名、内存属性和源代码审核，再决定成对映射候选；不能只改一个alias或盲设全局HAS_UCNORMAL_MEM。不要再重复CoreUI PC0、颜色类型或等待时长探针。
3. 基础交互稳定后按唯一计划推进持久Data、完整双口/保护策略、Wi-Fi/蓝牙等；这些未完成项不清除。扬声器仍先讨论衰减/保护，GPU最后。

## 当前设备

已用固定stock `8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890`完整SHA恢复。`edl_work/graphic-cfcolor-v91-stock-20261111`确认Android/root/green/boot_completed1，99%/4453mV/-424mA/27.1°C/Charging，熄屏Dozing。全部logger/watcher/构建终止；下一reference=stock/fresh电量。无人值守和完整Goal继续有效。
