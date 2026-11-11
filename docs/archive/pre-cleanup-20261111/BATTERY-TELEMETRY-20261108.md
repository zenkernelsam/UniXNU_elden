# 电池优先续档（2026-11-08，离线候选已交付）

用户最新顺序：**电池与双口充电 → 触控 → Wi-Fi → 蓝牙**。
设备最后确认仍为原厂 Android 补电。本轮没有ADB、fastboot、9008、QEMU或设备写入。
日用iPad Goal仍active；快充、净充电和真实触控均不能因本页编译成功标完成。

## 新实现

设备仓 `drivers/EldenPmicGlink/` 新增 BatterySnapshot、BatteryRuntime。
已有原厂ADSP/GLINK/45项Lenovo GET和双口UCSI复用同一通道，无第二个PMIC owner。
原生IOPMPowerSource使用本次source XNU实际编译的公开类，没有拷贝猜测的私有vtable。
沿sheng的方法延后注册类，拿到完整合格读数后才公布；默认不开。

- Aggregate容量按原厂0.01%换算，电压/电流/电荷为uV/uA/uAh，温度保留0.01C。
  当前方向须原厂Android对照后选+1/-1；不把Linux的inverted标签直接当实测结论。
- 两块不等容量电芯不取SOC平均，使用固件汇总电量；FG1/FG2原始数据各自保留，单位不猜。
- 外部online不足以判净充电：状态charging、未挂起、使能以及合格方向下的正净电流才确认。
- 45项全轮日志每9项一行，保留valid、epoch、cycle、最早采样至完成的age；超过10秒不发布。
- 断链/超时/过期标Unavailable，保留最后有效电量，取消充电/电流确认和旧低电告警；不冒充0%或电池拔掉。
  这些只修改报告，未改变IC、输入FET、温控、限流或充电使能。
- ADSP新版共享AOSS QMP owner保护已在本核实际链接，旧冻结候选与来源完整保留。

原厂采集器补齐之前遗漏的FG1/FG2温度、两路ibus、时间/循环项、typec角色及GPIOchip base。
GPIO541只按真实base解析候选line，仍不作充电开关；两口物理映射/Lenovo55含义待现场。
支持多个带时间的样本和线缆标签，只读sysfs/dumpsys，默认离线，不发送控制指令。

## 实际构建、验证与两份候选

v3原生重链核 `8bf3527fc05d5f1ed225d5b832d622efa973f029fb0a000bd81be392f5ae9a69`。
保留已验入口/AMFI变换后，最终kernel 52330600B，SHA256
`6ecda04184f8ecb048b95b03128bbd0c262f1e0afc8375bd94d1d7f864d6e63d`。
原30个执行组件、207-slot HID/52绑定、FD/shim/FAT原资产独立审计通过。
ASan/UBSan快照57项通过，覆盖带电却净放电、两电芯、缺项/陈旧、INT_MIN/非法值等。
采集解析/双温度/角色/GPIO基址及shell语法检查通过，没有调用ADB。

`edl_work/battery-telemetry-20261108/final-v3/`，均96MiB、
**boot1738713600@footer0 / 父链location3**，独立RSA/data/stock Props通过，全部未刷：

| 档 | SHA256 | Session | 含义 |
|---|---|---|---|
| off | 6831a97eb171b45ac19f4c5f85e418ce2b0c8bec338e089792e86c0a7515c703 | dc9759f1aa4e6f19 | 新代码关闭，GUI/USB/Home/回程回归 |
| query | dc9ac415d59f9b7c2e26511d4460a028d4bda7e43adf56014bec27632e4139c0 | 6f9f378d4dad67ae | adsp2/pmic1/UCSI1，真实signed DSP启动及GET；无charger SET、暂不发布电池 |

query含DSP认证启动，不能把它整体叫无硬件写入。须先电源native yes、原FW0255…与survey现场资格。
6GiB、512MiB临时RAM Data、原根、8秒自启、180秒及RPC回fastboot配置保留。
v2仍保留作较早诊断代码，不作最终电池发布来源；v3补上失联时撤回充电确认。

完整证据 `final-v3/independent-audit.json`；源在
`research/battery-telemetry-20261108/source-compiled-v3/`，38份成功输入及脚本。
原M3复用对象的15份未改源/声明再次与ucsi45冻结manifest逐字节对上。
所有新内核构建、取回、签名和审计都已结束，无后台设备循环。

## 下次实际操作

先在Android记录短边独接PD、长边独接PC、双口的插入顺序及净电流/两FG趋势。
单次原厂采集例（从Y700工作仓运行）：

    python -X utf8 edl_work/collect_power_evidence.py --collect --label short-PD --samples 6 --interval 5 --output edl_work/power-oem-short-PD

线缆状态由所有者操作；脚本不切端口、不改电流、不重启。所有实验写入仍用现有电源准入/现场GPT/全像回读流程。
先off回归，按原FW/survey确定接活DSP还是原signed启动，再query，PC读ELDEN_PMIC_RAW和双口UCSI。
核清真实单位、符号及两路输入后，package_battery_telemetry.py的publish模式接受带本地证据SHA的qualified-contract。
没有合格记录不能生成启用发布的包；现阶段没有签好任何publish包。

**充电IC控制仍待现场证据**：不盲发v2 SET_SINK_PATH、USB ONLINE/Lenovo55或并联两路VBUS。
最后要测到短边持续输入、长边数据稳定、双FG/汇总净充电和SOC增长，才算这项验收通过。
电源通过后把NT36536合并进保留M3的同一内核，避免切换早期触控独立包丢掉充电能力。

触控五档继续见HANDOVER-NT36536-RUNTIME-20261108.md。
Wi-Fi已只读核77项原厂Peach资产和RID100/SID1401/MSI IOVA，见WIFI-INPUTS-20261108.md；原生网络未接。
蓝牙主机桥已收尾交档HANDOVER-WCN-NATIVE-20261108.md，daemon/配对未验。
后续按以上顺序推进；持久Data/GPU/睡眠/KShimC仍在完整Plan中。
