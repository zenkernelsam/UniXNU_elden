# CPU功耗调查与双口充电下一步（2026-11-09）

接续HANDOVER-CHARGE-MODES-GESTURES-20261109。上一轮属实质progress；完整外设Goal仍active。
本轮保持stock Android，未刷设备、未改CPU频率或背光；用户已收到可选接线调查：长边PD、短边PC，保持Android。
该物理接线问题尚未得到回复，避免此时重启/刷机；先继续离线准备。

## 真实现场与来源

- Android启动完成，KernelSU root正常；起始58%、4.000V、约-474mA净充电。
- 当前接线下Android背光brightness/actual_brightness=0，bl_power=4，屏幕已关。
  XNU调试种子明确no_auto_dim且平台禁用系统睡眠，所以不能把两者功耗差简单归为充电IC故障。
- 原厂CPU策略使用scaling_driver=scmi：policy0为CPU0..5、policy6为CPU6..7。
  原厂最高频率3532800/4320000kHz；其当前下限556800/1017600kHz。
  不能因此给本机套qcom-cpufreq-hw裸寄存器控制。
- 实机原DT证明CPUCP v2：TX17830000+300、RX16430000+4c08，tx邮箱0，rx邮箱2；
  SCMI发送SRAM17b4e000+200，接收17b4e200+200。与Pagani已实机运行的SCMI客户端匹配。

本轮固定来源和原始现场分别在research/charge-route-20261109与edl_work/charging-next-20261109。
Armada885fd1fd里的本机DTS/patch、h0cheung bf96c3d4的SCMI相关SoC DTS/驱动、原厂recovered-reference.dts各自记录，不混为同版本。

## 已完成的CPU候选

采用已实机运行的Pagani SCMI PERF代码，固定来源commit c888efd3ce1b817d13f28783a77cf689cb2255c9。
原始头和仅加“发包前读命令白名单”回调的构建头在research/cpu-power-20261109，source.json记录双SHA。
新CpuPerfSurvey.cpp使用本机地址，只查询版本、域属性、档位表、当前档位和限制。
PERF_LEVEL_SET与PERF_LIMITS_SET在任何传输I/O之前被拒绝；不改档位、不清忙邮箱、不强抢占。
异常未完成事务保留映射/lease，结果仍不冒充硬件成功。没有替换现有ArmCpu实现。

2177项ASan/UBSan检查通过，包含真实核心查询、两个域、拒绝设置时零I/O和超时后不强制释放。
原生编译/重链/预链接及独立30原模块、HID207/52与220/37、RAM Data/6GiB/AVB检查完成。

新候选（尚未刷）：

- edl_work/basic-bundle-20261108/v12/final/cpu-survey-focused-gestures/boot-signed.img
- SHA256 aece7477ec27b6571d16c6d35d8c5676f388dace979a5df38f8a5f6ee6dc5fbc
- session c74d6219c6d7c4b0；96MiB，boot1738713600@footer0/父链3
- kernel212810877cab111caa10b861f1f8c555bad1695c3d6e7e2b9d028fa6ddd6f2dc，52641896B
- 冻结83源在research/basic-bundle-20261108/source-compiled-v12；power runtime v14
- focused首项CPU performance levels - SURVEY，8秒倒数，保留已验M3/ICL500/控制RPC/手势ADT及自动回程。

设备当前boot_a仍stock8b8e72da…，后续增量reference用stock。
待接线状态明确且可保持回程时，先跑本CPU只读候选，日志看ELDEN_CPU_PERF/LEVEL/CURRENT/LIMITS。
只有得到本机具体表项与当前档位后才选择节能策略；不会照搬Pagani最高档测试模式。

## 背光调查

Armada本机DTS指定I2C10@888000、AW99706@76、GPIO88、4095级、默认511。
原厂实际Linux总线号为4；4-0011和4-0076都存在并绑定i2c-bl，因此仍需确认实际响应的芯片，不能仅凭设备节点名认定。
原厂panel0-backlight是显示前端；DT出现DCS背光字段不能据此忽略实际I2C背光芯片。

AW99706驱动提供的寄存器：chip-id11应为07，brightness MSB04低4位/LSB05；
1e可解锁MTP、1f可执行MTP，后续读写接口必须排除这两个地址。
Armada0171的寄存器重放描述的是TB323FU/五代，不能照抄到四代；HWEN复位会丢失引导配置。
后续先确认时钟/总线空闲、芯片ID及现存配置，再移植sheng背光服务接口和本机亮度操作。

## 待验证

1. 交换长边PD/短边数据后的原厂供电选择与ADB枚举；这能直接检验双口优先级，不需先改充电寄存器。
2. CPU只读调查，再选择固件已描述的安全档位/调速策略。
3. 本机背光身份与运行时控制；原厂温控GPIO逻辑仍需移植，不能当充电选路开关滥用。
4. 原生净充电、完整80%及40–60%充停周期、UI持久设置；触控/Wi-Fi/蓝牙与其他基础外设仍未完成。
