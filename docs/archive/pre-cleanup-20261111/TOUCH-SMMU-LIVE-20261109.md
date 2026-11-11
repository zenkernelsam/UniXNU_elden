# T1实机：触控没有继承SMMU映射

用户回复长边PC/短边PD已接好、Android后，本轮完成两次无人值守：
boot_a-only刷写→全像摘要→8秒自启→USB日志→75秒后健康RPC→PC确认fastboot。
随后恢复stock Android补电；用户授权仍有效，下一次测试无需再询问同一接线。

## 两轮结果

| 项 | v24目标SID调查 | v25完整全局清单 |
|---|---|---|
| signed boot SHA | d285afd934c83a23b5a7e2df39d822d32230d5e19d2d6b99bad355b2a57d127e | 88083769901bbdcbe311598e1d2e411cedae7fca551555c31c3fbb22ee1852b5 |
| session | 219d57d3edfcf40c | b2da49234e530d4e |
| USB bytes | 81478 | 89146 |
| dropped | 0 | 0 |
| 原生准入电压 | 3998mV/yes | 3979mV/yes |
| 结果 | SIDa3、SIDb6均0匹配 | 132个槽位和83个bank全局信息完整 |
| 回程 | RPC+PC确认 | RPC+PC确认 |

两轮Root/Data/PID1均健康，无kernel panic；没有SPI/DMA命令或SMMU配置写。
每轮完整96MiB device SHA等于候选SHA。boot index **1738713600@footer0/父链3**保持。

GENI实际值：protocol1、FIFO-disabled1、width32/depth16、status0、GSI events=f、DMA mode1、
TX/RX FIFO与IRQ/length均0。不能据GSI enable位推断已运行GPI，也不能直接省略接管资格。

## SMMU实机清单

```text
CR0 00230406
ID0 4c017e84
ID1 60000053
ID2 00005111
132 groups, 83 context banks
```

全部有效映射：

| slot | SMR | S2CR | 可识别用途 |
|---|---|---|---|
| 0 | 80000060 | 00000000 | 已验UFS SID60→bank0 |
| 1 | 80000540 | 00000001 | 原固件，未改 |
| 2 | 80020800 | 00000002 | 原固件掩码项，未改 |
| 3 | 80000040 | 00000003 | 已验USB日志SID40→bank3 |
| 4 | 80000480 | 00000004 | 原固件，未改 |
| 5 | 80000540 | 00000005 | 原固件重复SID，未改 |
| 6 | 80000480 | 00000006 | 原固件重复SID，未改 |

slot7..131均SMR0 / S2CR0300006e。无有效SMR不等于已取得这些槽位的所有权；
S2CR残值不能直接当作有效bank110（本机只有83banks）。
bank0..6全局CBAR=0001f000、CBA2R=1；bank7..82均全局0。
本轮**没有读任何CB SCTLR/TTBR，没有读取页表**，不能称所有bank已资格化。

注意原厂重复/掩码条目不覆盖a3或b6，不要为了“整理”全局表去改它们。
sheng固定最后一个bank且截断128槽的做法不能照搬；本机最后bank82并没有同样的全局配置。

## 当前恢复状态

已恢复 `signing/phase5_resign/boot_signed.img`，SHA
`8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890`。
恢复全像摘要通过，Android boot_completed1、TB322FC、green、KernelSU uid0、boot块设备完整SHA通过。
2026-11-09T13:32:13Z读取57% / 3985mV / -408mA净充电。此为恢复时历史采样，不能替代下次fresh准入。
设备继续长边PC/短边PD。没有未结束编译、写入或USB监听进程。

## 下一步实现

当前T1阻塞已从“FIFO不可用”具体化为：SE-DMA SIDa3没有映射，需要有所有权约束的路由接入。

1. 根据完整现场实现仅目标SID/单个空槽的映射方案，不改现有7项。
   可研究共享已知bank0的禁翻译状态，但必须重新验证本机bank0 SCTLR、宽度、全局状态及所有权；
   UFS曾通过不能自动赋予触控DMA权限。不得写shared CB寄存器或盲用bank82。
2. 所有权/复验范围包含slot0与USB slot3及全部目标SID冲突，失败保留资源到复位。
   写S2CR→读回→SMR→读回的顺序参考成熟sheng；不实现全局reset/bypass或抢占masked slot。
3. 接 `NativeDmaBuffer` 与 `DmaTransfer` 独立库；核CTR cache/CPU物理与bus地址、DMA_IDLE/GSI处理。
   原16KiB staging覆盖15KiB固件块，但先只做ID/PID，不上传固件或启触控HID。
4. 将新的source manifest、Runtime/build接口和失败回程一起编译；独立核原30模块、HID、存储/USB与floor。
5. 新候选写入reference必须使用**当前stock8b8e72da…**；先开绑定session日志并读取fresh>=3500mV/native yes。

充电仍未解决双口净输入，Android是当前补电保障；完整Goal active。
之前提到的NativeDmaBuffer生命周期主机mock测试尚未动代码，因接线回复到达而先执行了实机。

## 文件

- `edl_work/live-touch-dma-survey-20261109/`：第一轮flash、USB、assessment、fastboot-return。
- `edl_work/live-touch-dma-inventory-20261109/`：第二轮及完整`inventory.json`。
- `edl_work/touch-inventory-stock-20261109/`：恢复flash和Android/root/电流证据。
- `research/touch-dma-inventory-20261109/`：v25来源许可/资格、v24摘要。
- `research/basic-bundle-20261108/source-compiled-v25/`：127个实际构建输入冻结。
- `edl_work/assess_touch_dma_survey.py`：现在支持显式`--version 24|25`，绑定各自image/session，仍不授予DMA权限。
