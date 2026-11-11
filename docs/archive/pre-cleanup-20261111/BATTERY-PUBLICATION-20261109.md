# 电池发布器初始化修复与真实电量候选

Goal仍active，充电/基本外设优先，GPU最后。上一轮背光与iPad Pro候选是实质进展；
本轮继续充电主线，没有刷机、重启、改GPT/root/userdata、改接线或执行新的充电控制。
当前boot_a仍stock8b8e72da…，原厂Android充电。换线问题未回复，继续保持Android。

## 实际发现并修复的问题

不能只凭GLINK成功应答、完整valid mask或一次时间延迟就发布电池数据。
对已保存的v11实机USB日志，用生产BatterySnapshot.c重新执行，发现：

| 轮次 | 汇总电量/电流 | 独立电量计 | 结论 |
|---|---|---|---|
| 1 | 50%、4.000V、原始-4000mA、25°C | 58/57%、约3.97V、两颗均放电 | 汇总仍是启动默认值；旧发布器会错误声称充电4A |
| 2 | 50%、约3.973V、原始+577mA、25°C | 58/57%、约32°C | 部分字段更新，汇总电量/温度仍未准备好 |
| 3 | 58%、约3.973V、原始+577mA、32.5°C | 与汇总相符 | 第一轮合格采样，先等待下一轮确认 |
| 4起 | 真实汇总值 | 连续一致 | 开始允许发布 |

旧电池发布器此前一直默认关闭，因此上述错误尚未送入iPadOS界面。

新EldenBatteryGate要求：当前完整且新鲜的采样、两颗独立电量计的有效字段、
SOC/电压/温度处于允许的测量范围，电流方向不能与两颗电量计同时矛盾，且连续两轮一致。
这只是发布完整性检查，不是充电温控保护；没有平均两颗不同容量电芯，也没有用电量计替代汇总值。
重复/旧轮次不增加确认数；失联重新开始确认。拒绝时保留上次电量并标未知，不报告空电池。

另外修复：

- 旁路/80%等保护窗停止电池充电时，输入仍可能给系统供电；ExternalChargeCapable不再随charging_enable清零。
- input_suspend仍单独撤销输入可用状态；零值ETA不伪装成“剩余0分钟”。
- 已有IOPMPowerSource匹配对象正确释放引用；原来的延后类注册和IOService挂接保持。

## 验证证据

- `edl_work/qualify_battery_publisher.py` 将**实机原始45字段**喂给生产C解码器及新gate，未另写Python替代算法。
- `edl_work/battery-publication-20261109/contract.json`：71完整轮，前三轮不发布，后68轮合格；
  合格范围为-950…-383mA（Apple约定：正为充电），不存在错误“正在充电”记录。
- `decoded-native.json`同时保留旧直接解码与新的publication结果，原始4000mA不会被隐藏。
- 61项Snapshot和21项Gate的ASan/UBSan检查，覆盖初始默认值、重复轮次、缺字段、
  失联/过期、恢复确认、不同电芯电量与保护窗输入状态。
- 单位/方向依据原厂qti_battery_charger.ko 4aeb25c0…、实际Android读数及sheng发布器合同。
  汇总电压/电流/容量为uV/uA/uAh，SOC为0.01%，温度0.01°C；独立FG用于校验，不发布其猜测换算。

## 原厂现场与双口调查

本轮 `edl_work/charge-continuation-20261109/oem/` 只读：
电量69→70%，约4.109–4.111V，约-0.48A净充电，battery health Good。
USB输入约4.85V/494mA、SDP、ICL500mA；第二个UCSI端口存在PD合同，wireless逻辑输入仍offline。
**协商PDO/RDO或current_now的合同字段不能证明短边实际供电。** 当前原厂也优先电脑输入，
不能把XNU净放电全部归咎于充电IC；原厂已熄屏，XNU仍有持续图形/CPU负载。

原厂连接器热区：usb1-conn-therm约27.625°C、usb2约27.679°C；
两电池包约27.50/27.58°C。读数在connector-thermal.json，未改变温控或GPIO。

温控源码与原厂模块反汇编一致：

- USB1/globalGPIO515(TLMM3)：低于45°C或不高于49.9°C且比电池高不超过9.9°C时拉低；
  高于75°C或至少65.1°C且高出至少30.1°C时拉高；中间保持原值。
- USB2/globalGPIO728(PM8550 GPIO1)：恢复条件同上；高于70°C或至少60.1°C且高出至少20.1°C时拉高。
- 这些是过温保护脚，数据MUX GPIO541/TLMM29另有用途，不能当双口PD优先级控制。

真实DT：ADC5 gen3的SDAM资源偏移9000/9100；USB2的ADC通道147、ratiometric/200us settling，
USB1通道747还有`qcom,adc5-gen4`标记。来源固定在 `research/charge-continuation-20261109/`。
Armada指定内核bf96c3d4的adc5-gen3/adc5/vadc-common已提取并记录SHA。
本份主线实现没有直接消费同名gen4属性；下一步必须核原厂如何选择配置/换算，
不能从同一provider名就断言两路相同，也不能直接拿PMIC USB/WLS温度替代连接器ADC。
这部分尚未实现原生ADC读取或AP温控GPIO操作，双口PD净充电仍未解决。

## 新签名候选

**basic bundle v16 / power v16 / backlight v5**，kernel52658280B：
`a6caaecd52e481c8cc1b86b8614235801cab351b8519ee6fa82409023e2f3033`。
103份源码及构建输入冻结于 `research/basic-bundle-20261108/source-compiled-v16/`。
两件位于 `edl_work/basic-bundle-20261108/v16/final/`，文件名boot-signed.img：

| 目录 | session | SHA256 |
|---|---|---|
| power-control-focused-gestures | fe724b4d5e6f5d2e | 2977d7f9fdf6e3308bc59e2b6070b0a43d551416361518ff86f072ae6efd970f |
| power-battery-focused-gestures | f9cdfae04168e342 | d80dd93a1d49de33beaaff723b45d0ed55280ad04e706367db7e36bcbbfd1604 |

对照件不发布电池；power-battery才启用 `elden-battery=1 elden-battery-profile=1 elden-battery-current=-1`。
打包必须绑定本机OEM/原生C回放contract及每份编译源码SHA；旧无gate内核不能启用新菜单。
原30模块、HID207/220/37、全部资产、6GiB/RAM Data、原FD/shim、8秒倒数与USB/RPC回程及独立AVB通过。
96MiB、1738713600@footer0/父链3不变。背光代码保留但这两档均不启动；触控/无线仍不启用。
power v15保留为编译诊断记录；正式版本是v16，修了XNU已有struct gate导致的命名冲突。

**尚未刷入。** IOPMPowerSource服务实际注册、状态栏电量/充电图标仍待真机验证。
后续仍需fresh>=3500mV/native battery-soc-ok=yes，写入reference使用stock8b8e72da…。
监听器已新增battery_ready/held/published/refused摘要；先核前几轮held，再核真实published，
不能只看服务名就宣布电池或充电全部完成。确认接线后再进入自动实测。

上一轮背光三档及iPad Pro无Home键候选保留，详见对应11-09交接。Goal不清旗、不blocked。
