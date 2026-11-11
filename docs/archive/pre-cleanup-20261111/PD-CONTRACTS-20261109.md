# 双口供电选择：排除55误用，新增实际PD合同读取

Goal继续active。充电和基础外设尚未全部完成；本轮没有刷机、重启、GPIO/充电控制写入。
设备仍stock8b8e72da…Android，起始只读81%、4246mV、-469mA、Charging。
换线问题未收到回复，仍保持Android。

## 原厂55属性的真正已知调用路径

新增只读提取PHY、EUD和PWRKEY原厂模块，并核对设备sha256sum；资产在research/charge-routing-20261109。

- dwc3-msm只通知内置usb_phy_set_charger_current。
- SNPS/M31 eUSB2 PHY的set_power工作队列写的是INPUT_CURRENT_LIMIT，单位mA×1000。
  当前原生ICL500mA已覆盖这条常规通知用途。
- 可见原厂模块中，ONLINE写由pm8941-pwrkey触发：KPDPWR_N_SET和RESIN_N_SET同时成立，
  保持5000ms后写ONLINE=1；释放后写0。qti_battery_charger将ONLINE setter路由到Lenovo property55。
- 源码及原厂pwrkey反汇编已同时确认参数1/0和属性编号4(ONLINE)。
  因此55用于强制关机相关的SC7603_LOWPOWER处理，**没有证据把它当PD输入选择器**。
  没有为了“修PD”盲发SET55；正常运行读到55=0不能证明少了初始化。

## 原厂直接GET实机结果

capture_stock_pd_contract.py使用内核UCSI命令锁，经独立debugfs挂载发出固定GET：
GET_CAPABILITY、两口GET_CONNECTOR_STATUS、第二口GET_PDOS、再次GET_CONNECTOR_STATUS。
无SET角色/电压/PDO/sink、无复位。操作约0.34秒，挂载/目录已清理。
证据在edl_work/charge-routing-20261109/stock-pd-get和research/charge-routing-20261109/stock-pd-proof.json。

| 项 | 实读值 |
|---|---|
| 源PDO1 | 0x0a01912c：5V/3A |
| 源PDO2 | 0x0002d0de：9V/2.22A |
| 当前RDO | 0x1204b12c：位置1，5V/3A合同 |
| 第二口状态前/后 | 相同；PD sink，RDO未改变 |
| GET_CAPABILITY features | **0** |

重要兼容点：固件没声明PDO_DETAILS能力位，但实际GET_PDOS正常。
显式只读探测不能依赖该位来跳过本机；不支持的PPM用UNSUPPORTED应答+ACK隔离处理。
较早v19按能力位跳过的构建保留为中间件，未刷；推荐是本轮v20。

debugfs response固定打印128bit，短回答末尾可残留上次数据，不能把高位尾巴当第三/第四PDO。
原生实现按CCI的实际长度解析；原厂sysfs也独立确认只有5V/3A和9V/2.22A两项。
**合同不等于实际取电，更不等于净充电。** 此时系统仍优先SDP500mA输入，物理换线/选路还需验证。

## 原生实现

UcsiQuery可选elden-ucsi-pdos=1，默认保留原五个GET/ACK：

- 对已连接PD sink读取partner source PDO，分页最多4+3个SPR PDO。
- 错误ACK后将可选“不支持”查询隔离，不破坏正常状态读取；超时仍需新链，不重试不明事务。
- PDO后重新读取两口状态，RDO/关键状态变化或期间连接通知导致合同不一致，不能发布成确定合同。
- PdContract解析固定、电池、可变与PPS；超范围/未知AVS拒绝。可变电压只报告范围，不伪造单一电压。
- 日志区分ELDEN_PD、ELDEN_PD_SOURCE、ELDEN_PD_CONTRACT，明确contract-only/not metering。
- 没有SET_PDOS、角色/电压切换或新充电控制；assess_power_session.py新增PD摘要。

796项新PD/分页/稳定性/缺能力位检查，1845项原UCSI回归，ASan/UBSan通过。
原厂正常查询证明报文可用；新XNU实现尚未上机。

## 候选

basic v20 / power v20 / backlight v5，126份源码/构建输入冻结source-compiled-v20。
kernel SHA：f93e72e2e0cd4deb1d1b851fd946ebb1e665e5fee7b037e0a07bf48c6765289d。

`edl_work/basic-bundle-20261108/v20/final/pd-survey-focused-gestures/boot-signed.img`

- SHA256：ea2697260f04062ce3b16dcb57218111c7098c623dd4ba2fa44a6ec0dfb57456
- session：487f3d06f440093d
- 96MiB，1738713600@footer0/父链3；原30模块/HID/全资产/6GiB/RAM Data/8秒/USB回程/AVB通过。
- 未刷。温控GPIO、ADC、背光、UI电池和无线在本档不启动；已有充电模式/ICL与合格原始数据保留。
- pd-qualification.json候选内冻结，防止下一轮更新全局证明后旧件无法复验。

接线确认后，先保持已知接线跑PD只读候选，对比原厂PDO/RDO、PMIC实际输入和电池电流。
若换长边PD/短边PC，先在Android确认ADB和原厂选择，再动实验镜像。
新写reference仍是stock8b8e72da…，fresh>=3500mV/native battery-soc-ok=yes不变。

## 定时参考同步完成

本轮UTC21:48按6小时节奏检查40个research克隆：25已最新，3快进，9固定版仅fetch，2有本地修改仅fetch。
UniXNU-piano远端仍不存在，旧副本保留。报告edl_work/reference-sync/20261108-214845。

- ShengUfs：af14068c→a901555e，新增最多8命令并发、Linux队列/DMA/完成路径与计数器。
- UniXNU-sheng：9109d4ca→a68ee4ee，新增WN8030键盘盖、触摸板修正、任意长度GENI I2C及UFS基准。
- mu-silicium：b72d892a→20b82927，主要其他设备支持/ACPI更新。
- Armada/h0指定内核保持最新。新的上游变化没有自动替换本机已冻结组件，后续先审硬件/ABI再迁移。

所有本轮编译、同步、采集进程已结束。主线仍是双口实际输入与基本外设，不把PD查询完成当快充已修。
