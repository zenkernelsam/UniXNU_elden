# Y700双口ADC原生实现与温控前置

完整Goal active，充电/基础外设优先，GPU最后。上一轮电池发布器修复属于实质进展。
本轮完成真实ADC协议取证、生产C实现、原生编译/重链及三个签名候选；新驱动未刷入。
当前仍stock8b8e72da…Android，最后只读74%、4159mV、-480mA、Charging。
换线问题未答复，继续保持Android；没有设备重启/刷机/GPT/root/userdata/校准修改。

## 本轮原厂实机证据

1. 从设备/vendor/lib/modules只读提取并以设备sha256sum交叉核验：
   - qcom-spmi-adc5-gen3.ko：3d713936763524a82d66580771056a069c584c036390e4651863005d8afea2e3
   - qcom-vadc-common.ko：dcaee8b0e413618dd04a6792fe60ad022c99bd80976cd0c6700449c744be94a6
   - qti-glink-adc.ko：2d3073bd15298efd047e49efdd9ecc854737178864abe4a6a5717205e2f0cb63
   - dwc3-msm.ko：7fbebdd776bfb3300d21dd3099af414591a831a700347f29ce2e8ebfdfb75426
   资产在research/adc-elden-20261109/stock-modules。
2. capture_stock_adc.py创建独立trace instance，只记录SID0/9000..91ff。
   读取原厂usb1/usb2连接器热区各两次，没有自编SPMI事务，没有改变屏幕或接线。
   112/112事件、overrun0，实例已移除；全部操作约2.94秒。
   原始证据edl_work/adc-bringup-20261109/oem-trace，机器可读证明在research/adc-elden-20261109/oem-trace-proof.json。

| 热区 | SDAM904f写入7字节 | ADC原始码 | 原厂温度m°C |
|---|---|---|---|
| USB1 第一次 | 07 80 01 14 80 47 00 | 7aaf | 27415 |
| USB1 第二次 | 同上 | 7ab4 | 27409 |
| USB2 两次 | 01 80 01 18 80 47 02 | 1ea1 | 27473 |

USB1使用SID7/AMUX47/ADC Gen4：默认decimation1、settle0、满量程65535。
USB2使用SID1/AMUX47/Gen3：decimation2、settle2(200us)、满量程16384。
两路NTC表与Armada指定内核bf96c3d4逐字相同；差异在满量程/采样配置。
换算为R=raw×100000/(fullscale−raw)，查表后线性插值，输出直接为m°C。
已逐值复现原厂三个不同读数；不要因表间隔1024而额外除以1024。
Gen4的8000是合法码，不能沿用Gen3对该值的拒绝规则。

## 已实现

设备仓drivers/EldenUsbThermal与soc/sm8750/include/EldenAdcResources.h：

- Spmi：复用本机已验证v7.2控制器ABI，精确PPID0090，动态找APID并核EE0写归属。
  observer=0xc440000+EE×8000+APID×20，writer=0xc500000+APID×1000；不重写归属表。
  1..8字节，低字节先行；外露接口仅允许ADC状态/结果读和4F配置、4B清本EOC、50清请求、E5触发。
  命令超时/未知写结果保留lease和映射；绝不触及重启SDAM7148或其它PMIC外设。
- Adc：只借immediate slot0；先验HS=1/REQ=0且无旧EOC/全局fault，保存配置。
  配置读回→触发→等EOC→读取原始值→只清本EOC→等待PBS消费清除→归还原配置。
  不清热监控48/49/6e状态、不改其它TM通道、阈值、GPIO、电源轨或校准数据。
- ThermScale：原始Qualcomm/Armada GPL表、明确两种满量程，防止近开路时s64电阻缩窄溢出。
- Policy：复现原厂两口45°C恢复、75/70°C绝对保护与电池温差迟滞；这里只计算期望电平。
- UsbThermalRuntime：实际IOMemoryMap/LDR/STR后端，独占ADC lease，CPU计数器门检。
  经既有root/Data/PID1/M3流程启动，记录全部状态、原始码、温度及归还结果。
  当前温控输出为shadow，battery-known=0；只评估不依赖电池的分支，没有GPIO写入。

主机ASan/UBSan：ADC/换算/策略2610项断言；SPMI40896项断言。
覆盖实际字节/温度、保留其它TM标志、无额外转换的配置恢复、busy/故障/超时、
时间计数停止、错误读回、非法写地址、外部EE、重复APID、部分写与迟到完成的隔离。
这是主机与原厂观测证据，**不等于新XNU ADC已实测或充电温控已完成**。

## 最终构建与签名件

**basic bundle v17 / power v17 / backlight v5**；115份源码及构建输入冻结在
research/basic-bundle-20261108/source-compiled-v17。
kernel52658280B：77b8924ff826898c7d95bffe2c59b33fb5bdd7816e19e0119c5e36388d59bcf4。
新PMIC/ADC archive493840B：990be45c29ff0a6d5716991878c200d6c6ad52deaa7c1f8124cc83becc3edd61。

目录前缀edl_work/basic-bundle-20261108/v17/final，目标boot-signed.img：

| 子目录 | session | SHA256 |
|---|---|---|
| adc-inspect-focused-gestures | 182d1fc344a62ca6 | 1a20cbb55aae8e8f7f4dea6a7c471a3d26aa482393dd4c673618b9bfd2a0bf88 |
| adc-read-focused-gestures | 4a1fbb4829595575 | e59a8905cef1e9fbd2efd62555c0f405d92237db7da434e2abf66412e9131755 |
| adc-monitor-focused-gestures | 2b58d25582eab4c9 | 1f2bb00d11858d63bff1a6a0a1db43dced96b5883791080f08db712b2dcbd626 |

全部96MiB、1738713600@footer0/父链3，独立RSA/stockProps/原30模块/HID/全资产校验通过，均未刷。
保留6GiB/RAM Data、M3/UCSI/500mA/charge RPC、8秒倒数和USB/fastboot回程。
电池UI、背光、无线和物理触控在这三个诊断档均关闭；源码仍在同核，旧单项候选保留。
package/audit绑定候选内adc-qualification.json及实际冻结源码SHA，旧内核不会显示ADC菜单。

## 实测次序及剩余工作

接线确认后，仍需fresh>=3500mV且native battery-soc-ok=yes，当前写入reference用stock8b8e72da…。

1. inspect：只读SDAM状态，要求正确控制器/PPID/归属、snapshot result0；检查HS/REQ/EOC是否可借用。
2. read：实际两次转换，检查两口result0、原始码与合理温度，以及end release0/uncertain0/quarantine0。
3. monitor：每5秒一对，共12对，观察稳定性和shadow期望值，完成后归还配置。
   assess_power_session.py已加ADC和温控摘要；仍分别确认fastboot ACK、sync和PC实际枚举。
4. ADC实测通过后，再接合格电池温度、实际GPIO状态及原厂保护执行；不能把当前shadow当完整保护。
5. 双口优先级/PD实际输入仍要继续。新的sensor准备不等于充电IC控制或快充已经修复。

## 本轮排除的捷径

- 原厂qti-glink-adc只支持SMB1501的IIN/ICHG/die-temp；本机DT仅列总线106的68/69两实例。
  不可拿这个0x49接口查询USB NTC，也不能假定这些可选节点就是实装SC7603/PMIH0104。
- 原厂dwc3-msm.ko没有power_supply_set_property或Lenovo55直接调用，只调用内核内置
  usb_phy_set_charger_current；后续若追55/SC7603低功耗要沿PHY通知链继续，不在DWC3驱动盲加SET。
- 来源是Armada指定内核、社区原厂源码镜像、本机原厂模块和实机追踪的交叉证据，未把镜像仓当官方发布证明。

本轮无剩余编译/EDL/监听句柄，Goal继续active。
