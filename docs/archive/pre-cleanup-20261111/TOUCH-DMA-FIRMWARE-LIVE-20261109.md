# 实机交接：SE-DMA、NT36536和原厂SRAM固件通过

执行入口仍 `EXECUTION-PLAN-20261109.md`。用户已明确原生长边PC/短边PD接好，授权继续无人值守。
本轮最终已恢复stock Android补电；后续重新读取电量后可继续，不重复问同一接线。

## 最新实机结论

- SIDa3/slot7接入成功，原slot0..6与所有bank配置保持。
- **CB0 SCTLR实际0a9f00e0，M=0**。只捕获和稳定复核完整原值，不写shared CB。
- NT36536 ID为 **15,0e,00,23,65,03**，cascade=1；原厂SRAM上传后PID **6097**、FW **1a**、协议stage8。
- mode6完整 **72次DMA**全部返回0，TX/RX LEN_IN与该次实际方向/长度吻合，包含大块固件传输。
- 退出时只清除slot7 SMR；safe S2CR0留在无效项后，不恢复固件opaque reset读值。
  detach=0/stage8/writes3/poison0，memory-release0、platform released0/quarantine0。
- mode5和mode6均无panic，Root/Data/PID1健康、USB dropped0、RPC及PC fastboot确认通过。

**还没有读取并验收实际点帧、没有启用物理触控HID，也没有手指上滑验收。**
T1的芯片/PID/总线与清理证据已齐；T2的SRAM就绪已过，连续有效点帧仍待补。

## 版本与证据

| 版本 | 结果 | signed SHA / session |
|---|---|---|
| v26 | 编译后发现新C++对象含NEON，未刷 | 保留旧件，不当候选 |
| v27 mode5 | Native缓冲分配成功；SCTLR全0判据错误，写前拒绝，资源归还成功 | 635a4b0f… / 4abe4a231f0c4c5d |
| v28 mode5 | 6笔DMA/芯片ID通过；opaque S2CR恢复写导致panic；180秒自动回fastboot通过 | 15addbab… / 5ff87b2539ca17a2 |
| v29 mode5 | 芯片ID稳定、清理修复通过；未装SRAM时固件信息不合格属于本轮可见状态 | ff5381a6… / 42c86c42ca39813b |
| v29 mode6 | 72笔DMA、原厂SRAM、PID/FW、清理及回程全部通过 | 2bd2fd46… / 290cc9d932bdf6ec |

最终kernel：52707432B，SHA256
`9a8b327803ecb347edf743da0201c8a55ab3c463e0083f81d5251d1681d7b4d2`。

最终mode5 image：
`ff5381a6fc06a617e612c88cef7ffa72d7000fd59bbdd6def98a75bdf302990e`。
最终mode6 image：
`2bd2fd46a78e1dd827157e9832985a93d22c6b64451cf874925bc8f9d347ffcd`。
目录分别为 `edl_work/basic-bundle-20261108/v29/final/touch-dma-{identify,firmware}-focused-gestures/`。
两件96MiB、**1738713600@footer0 / 父链3**，全像device SHA一致。
原30模块、HID207/220/37、52绑定、原资产与6GiB/512MiB RAM Data/8秒菜单/USB回程保持。

最终构建输入135份：`research/basic-bundle-20261108/source-compiled-v29/`。
实际archive中6个触控对象均检查无FP/SIMD操作：`edl_work/basic-bundle-20261108/v29/touch-codegen/`。
新原生C++ Runtime/NativeDmaBuffer必须带`-mgeneral-regs-only`，不能只依赖独立库的编译选项。

现场日志：

- `edl_work/live-touch-dma-identify-20261109/`：v27写前拒绝，78635B。
- `edl_work/live-touch-dma-identify-mbit-20261109/`：v28，24495B，真实ID和退出panic。
- `edl_work/live-touch-dma-cleanup-20261109/`：v29 mode5，78016B，cleanup通过。
- `edl_work/live-touch-dma-firmware-20261109/`：v29 mode6，116286B，72笔完整DMA与PID/FW。
- 各目录保存flash/result、USB raw/meta、assessment和PC fastboot-return。

## 本次panic不是待查黑盒

用户照片与PC日志一致，保存在 `research/touch-dma-route-20261109/v28/device-panic.jpg`。
PC `fffffff0079d9124` 是 `STR w2,[x8]`；LR `fffffff0079d6d50` 在route_detach。
当时x1=c1c、x2=0300006e，向S2CR7写回固件初始无效项读数触发external abort。
精确证据：同目录 `panic-diagnosis.json`。

修复已经实机验证：清SMR后保留S2CR0，不再次写opaque/reserved reset值。
这个inactive条目不是原始字节恢复，但没有有效SID能匹配它；所有其他项逐一核对不变。
当前只允许一次attach，正常启动复位后重新取固件基线；不能当通用动态SMMU分配器。

## 新实现与剩余边界

- `DmaRoute`：要求真实owner回调、完整v25基线、bank0 M0/完整SCTLR稳定、地址宽度与master idle；
  只写81c/c1c，不写CB/global/TLB。失败保留lease与内存到物理复位。
- `NativeDmaBuffer`：cached/wired32KiB连续内存分两路16KiB，另16KiB private RX staging；CPU物理与bus地址显式区分。
- `DmaTransfer`：cache、mapping、idle、配置身份/失败隔离；允许canonical首次word0→8，之后不允许配置漂移。
- `Platform`：survey后在已资格DMA路径接管GENI；保存/恢复IRQ enables与pointer/attr，不回写LEN/FSM触发寄存器。
- `Runtime` mode5读取ID/PID；mode6走固定Linux派生Protocol的原厂SRAM上传，不启HID。
- **mode6不是只读**，会软件复位触控并写volatile SRAM，未写触控flash或存储分区。
  sidecar复用了mode5资格文件，其中`footprint.no_firmware_upload`描述mode5；mode6的
  `packet.touch_sram_upload=true`和实际日志为准。后续资格文件应按模式分别标注，避免混淆。
- 当前profile在Root/USB启动后无其他SMMU写者，touch lease串行整个实验且持续复核全局。
  以后GPU/Wi-Fi并行启用必须统一全局owner，不能把当前局部lease当通用仲裁。

## 当前设备与下一步

已恢复stock `8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890`，
全像device SHA、Android boot_completed1/green、KernelSU root均通过。
证据 `edl_work/touch-dma-success-stock-20261109/`。
正常熄屏后2026-11-09T14:13左右：57% / 3984mV / **-329mA净充电**、Dozing。
开机高负载曾瞬时+3782mA，已明确与睡眠后的充电采样区分。历史读数不能代替下轮fresh准入。
没有运行中的编译、刷写或监听器。**下一次刷写reference是stock，不能误用mode6。**

下一步集中真实触点/HID：

1. 基于已验mode6，为DMA增加物理点帧/HID入口，保留校验失败不发布、抬指/错误释放规则。
2. 加真实有效帧计数和少量原始点帧/坐标日志；无手指时的空帧也应可见，不靠reports变化猜测总线工作。
3. 新连续worker必须响应停止，并在主动RPC回fastboot前停止SPI、释放触点、撤route、恢复GENI与释放内存。
   当前旧PIO mode3无限循环没有该stop hook，不能直接把它视为完整无人值守退出实现。
4. 原独立HID207/220/37与52绑定沿用；先PC日志确认有效帧，再请用户手指点击/拖动/上滑验收。
5. floor仍1738713600@footer0/父链3，fresh>=3500mV/native yes，boot_a-only和完整读回不变。

Goal保持active。真实触控、双口净输入、无线等尚未完成，不能清全部外设flag。
