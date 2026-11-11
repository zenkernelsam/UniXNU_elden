# Y700 电源优先工单（2026-11-07）

用户将电池/充电提前到真实触控之前。当前最后已刷boot_a仍是HID主屏件7282b272…，
触控只读调查件499fac3b…未刷。stock boot1738713600@footer0保持。

## 实测与判定

- 单长边连接PC运行后，用户确认平板耗尽电量。
- 重回fastboot，只读确认HA2A1PT2、product elden_prc_wifi、slot a、battery-voltage3464mV、battery-soc-ok=no。
- 用户电流表：fastboot下仅短边接PD，8.70V、0.47A，约4W；再接长边PC，短边电流下降到0A/0W。
- 短边PD输入能拉载。问题与双口同时连接时的输入选择/挂起策略相关，不能凭这组数据直接判断充电IC失效。
- 4W为PD输入，包含系统耗电；PC一路实际电流和电池净电流尚未测得。
- 现象发生在fastboot，原厂Android/Recovery的同接法对照仍待完成。

## 原厂源码依据

固定h0cheung Linux 6f97250ead155fc3b980856323e99a4623dae73d，资料和SHA见
`research/power-elden-20261107/linux-sources.json`。

- 板DT两口均dual power/data role，长边数据路径优先；GPIO29为数据mux，不能作为VBUS选择开关解释。
- 原厂FDT包含`msm/adsp/charger_pd`、`qcom,battery-charger`、`qcom,charger-ulog-glink`。
- PMIC GLINK battery owner32778，UCSI owner32779；基本battery GET0x30、USB GET0x32；UCSI read0x11/write0x12。
- Y700 DT要求battery-current-inverted，必须按真实响应和电量趋势核对电流符号。
- 同机SM8750电池profile单独匹配。温度等属性不得直接使用sheng的厂商编号；本机Linux基本表温度12，
  sheng代码使用13并含Xiaomi的0x50扩展。需要真实报文资格确认。
- SMEM0x81d00000+2MiB、ADSP pid2、SMP2P443/429、GLINK478/479/480与sheng协议骨架一致。
- ADSP区域9ee00000+3a80000、DTB9ed80000+80000；sheng的9ea00000/9e980000不能复制。
- sheng的充电与PD由ADSP固件负责，ShengBattery提供IOPMPowerSource及input-current-limit接口。
  我们当前内核尚未整合这条完整ADSP/PMIC GLINK链。

sheng固定689400177f93219a38bd7cf5ae2e9f5782adc594；QcomSmem原始源码和许可证已存research下。
DSP备份dsp_a为64MiB ext4，/adsp是DSP用户态.so集合；不能把它当作adsp.mdt启动固件所在FAT卷。

## 双口保护要求

正常合规隔离PD电源通过USB GND形成共同参考地。驱动实现重点是VBUS选择、过压、
反灌与输入限流保护。沿原厂电源接口切换，保留这些保护；没有依据时不强制并联或同时导通两路VBUS。

目标：长边稳定USB数据，短边PD持续输入；验证两口插拔/先后顺序、电池净正电流和SOC上升。
能协商电压、能看到USB online或能发布电池图标均不足以完成此工单。

## 已交付保护与工具

- `flash_verified_boot.py`现于进入EDL前读取battery-voltage及battery-soc-ok，仅native yes准入；
  缺失/重复/非法读数和native no均拒绝。真实3464mV/no回放及三个边界组通过。
- `test_fastboot_power_guard.py`四项通过；签名和分区检查保持。
- `collect_power_evidence.py`默认离线；显式--collect在已授权Y700 Android/Recovery root shell下
  仅读power_supply和dumpsys battery，记录online/type/pd_active/current/limit/suspend/charge_enabled/temperature等。
  不修改sysfs、不重启、不访问用户数据。
- `elden-power-inspect=1`新探针先在原Root/Data/PID1/SDAM资格后只读SMEM到临时快照，
  以sheng读API分析apps-adsp/global partition和指定SMP2P/GLINK项。
  无SMEM分配/索引写、DSP reset、充电/电压/电流命令，不发布虚假电池值。
  候选正在独立构建；没有刷设备。

## 下一个判卷

先确认SMEM与ADSP slave-kernel ready及GLINK结构，决定接现存充电固件还是启动真实stock ADSP。
再接只读battery/USB/UCSI请求，记录两口状态、输入功率、电池电流及温度。
取得原厂Android同接法基线，核清source selection/input_suspend/限流接口，再实施单一路径切换和充电使能。
当前先用短边单独补电，native battery-soc-ok=yes后才准许新写入。

未完成：双口PD净充电、原厂IC/板级输入控制的资格、实际XNU电池发布与低电回程。
新的日用iPad Goal保持active；旧GUI/自动回程Goal已经complete。
