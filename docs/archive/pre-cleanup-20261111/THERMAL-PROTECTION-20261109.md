# 双口充电温控执行层

Goal active，完整基础外设尚未完成。本轮接续ADC候选，完成真实GPIO执行代码、
合格电池温度接口、闭环故障处理与原生构建；没有执行新驱动的硬件测试。

设备保持stock8b8e72da…Android，最后只读78%、4208mV、-474mA、Charging。
没有刷机、重启、充电属性/GPIO写、GPT/root/userdata操作；换线问题仍未收到回复。
唯一额外操作是在独立临时目录只读挂载debugfs，读取gpio状态后卸载并删除空目录，已成功清理。

## 原厂现场

`edl_work/thermal-protection-20261109/gpio.txt`与前次一致：

- global515 = TLMM GPIO3，输出低、func0、2mA、无上下拉。
  物理控制窗口采用mainline DT的f100000+3×1000；旧vendor节点名称/资源基址f000000不能直接当本组寄存器地址。
- global728 = PM8550 SID1 GPIO1，**基址8800**，输入低、normal、VIN0、10uA下拉、push-pull/低驱动。
  输入状态不能仅写输出电平；原厂高温路径会调用direction_output。
- 数据MUX仍是global541/TLMM29，与两温控脚分开。

参考文件/摘要在 `research/thermal-protection-20261109/`；使用Armada指定内核bf96c3d4的
pinctrl-spmi-gpio/pinctrl-msm与原厂qti_battery_charger代码、模块和实机状态交叉核验。

## 新执行逻辑

设备仓drivers/EldenUsbThermal：

- Spmi增加固定目标0188，仅允许GPIO身份/配置读取，以及MODE40、SOURCE44两个寄存器写入。
  原ADC目标0090保持自己的允许列表；不能改VIN、pull、drive、enable、其它GPIO或归属表。
- Pins检查GPIO类型、LV/MV模式、normal mux和继承配置。PMIC先预置电平，再按需要切换输出方向；
  TLMM只改OUT位，保留原配置。没有新指令时不把原输入低改成输出。
- 每次写均读回核对；未知完成不重试。两路失败独立：一边失败不能阻止另一边安全拉高。
- Control按原厂绝对/温差迟滞工作，完整数据连续两轮后正式进入监测；
  独立确认的绝对过热可在等待期间先拉高对应输出，不等待电池初始化。
- 正式监测后缺少有效ADC或电池数据会锁定两路为高。故障后的重新拉低要求连续两轮有效且达到原厂恢复条件。
  正常温控迟滞区保持当前状态。重复/旧序号不能充当恢复依据。
- ProtectionRuntime有实际ADC、SPMI、TLMM后端与持续循环。仅已完成且清除EOC的超范围转换允许后续重新测量；
  不确定SPMI/ADC事务不重试。退出时保留保护电平，**不会把热/故障状态恢复成旧低电平**。

BatteryRuntime新增不依赖UI开关的合格温度缓存：仍经过双FG/连续采样gate，
锁保护整个温度/epoch/cycle/期限元组，过期或断链返回未知。只读数据可用不等于已经发布电池UI。
之前的初始化默认值仍被挡住；旧v16候选的独立contract副本保持。

回fastboot前有停止请求：最多等待3秒，保持GPIO电平；超时有明确日志并继续既有恢复，
不会因为采样线程故障无限阻止恢复。持久Data的原VFS/fence逻辑没有放宽。

## 验证

- TestProtection：60项ASan/UBSan断言，包括输入低→输出高、预置顺序、参数保持、
  部分写、两路独立失败、迟滞、缺数据闭锁、两轮恢复、重复样本以及启动绝对高温。
- 扩展SPMI：42754项带范围断言检查，覆盖0090/0188分别归属和寄存器允许列表、超时/未知写。
- 原生编译全部通过，无新的早期静态初始化；温度锁和控制接口已真正链接。
- 原30模块/HID/全部资产/6GiB/RAM Data/原FD与8秒/USB回程及AVB独立检查通过。

这些是主机和原厂状态证据。**新XNU下的GPIO读取、保护执行、ADC恢复与本次停止钩子仍未实测。**

## 最终候选

basic v18 / power v18 / backlight v5；123份源码及构建输入冻结source-compiled-v18。
kernel52691048B，SHA256：3f3ec34d1a8d3852acf68c9e084385389f1fc967d81c3c842a281557bf564e13。
目录前缀 `edl_work/basic-bundle-20261108/v18/final/`，文件boot-signed.img：

| 子目录 | session | SHA256 |
|---|---|---|
| thermal-inspect-focused-gestures | a517f198a0a794e4 | bc10758ed1acdbff2f9bf4237f8a77d1ef7c3b5d3866045ab422d2e22fd5ca84 |
| thermal-guard-focused-gestures | ab3d452063e662b2 | 93d326fb8405988cd98049b2c14cb94193c669b65aa21a4a2cb15fe079daaf12 |

均96MiB、1738713600@footer0/父链3，未刷。
inspect用elden-thermal=1，只读GPIO，不转换ADC、不改GPIO。
guard用elden-thermal=2，包含真实GPIO控制；**不是只读候选**。
两档elden-adc均0，采样和保护使用同一共享lease，不允许竞争ADC。
UI电池/背光/无线/物理触控仍不启用；M3/UCSI/500mA/charge RPC、合格电池原始数据与恢复路径保留。

打包需要候选内thermal-qualification.json匹配冻结源码；guard要求真正链接温度接口和RPC停止钩子。
新构建使用prepare_basic_bundle.py的--thermal-protection参数；忘记它时不会提供新温控菜单。

## 接机后

继续保留Android直到换线确认；新写reference仍为stock8b8e72da…，电压需fresh>=3500mV且native battery-soc-ok=yes。
先测上轮ADC inspect/read，再测本轮GPIO inspect，对照实际type/subtype/mode/VIN/电平与原厂。
这些都通过后才试guard；先确认常温稳定、没有非预期写入、能停止并返回fastboot，再逐项验证保护路径。
温控闭环代码到位不代表物理保护或双口PD快充完成。供电优先级、实际PD净输入仍是下一条主线。
本轮所有编译/打包/采集进程已结束，Goal继续active。
