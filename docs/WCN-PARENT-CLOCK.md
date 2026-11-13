# WCN父时钟准入修正 · 2026-11-13

v99第一次真机运行准备好了BlueTool文件和挂载（prepared1/error0/stage5），
但WCN survey返回−19，尚未执行WCN电源获取、UART打开或HCI下载。
随后同步RPC回fastboot、完整SHA恢复stock Android。不能把未启动时的stop成功当作
活动蓝牙的关机验收。原始结果见[记录](../profiles/v99-bt-runtime/hardware-result.json)。

## 四代来源链

优先来源是[ArmadaOS四代说明](https://github.com/ArmadaOS-Freedom/armadaos-freedom/blob/885fd1fdaa373f266b9a3207997b91c67aa22e6d/devices/lenovo-tb322fc/README.md)，
其中固定引用h0cheung的`6f97250ead155fc3b980856323e99a4623dae73d`。
该提交的`sm8750-lenovo-elden.dts`明确写着`Lenovo Legion Y700 Gen4`，
compatible为`lenovo,elden`和`qcom,sm8750`，与本机TB322FC一致。
`enij90/armada-tb321fu`则是三代TB321FU/SM8650，不作为本机寄存器或固件输入。

这次按四代提交的`gcc-sm8750.c`确认GPLL0采用Taycan寄存器及固定PLL操作，
even输出的post-divider位10..13为1时除2。
同一提交`clk-alpha-pll.h`把固定Taycan操作映射到lucid-evo；
`clk-alpha-pll.c`的`alpha_pll_lucid_evo_enable()`在USER_CTL bit25置位时
只走投票和LOCK检查，不要求手动OPMODE已经为RUN。
这一C文件与本地Linux7.0.14参考的Git blob逐字相同；板级选择仍取四代固定源，
没有以7.0.14的整份GCC或头文件替换它。
文件摘要及对象结果见[来源证据](evidence/wcn-parent-vote-20261113.json)。

## 原拒绝与修正

真机读取为MODE=`e0000005`、OPMODE=`0`、L=`1f`、ALPHA=`4000`、
USER_CTL=`02000403`。现有双读一致性检查已通过；LOCK、输出和vote-run均置位。
按19.2MHz参考计算main为600MHz，even为300MHz。
旧解码器同时要求手动OPMODE=1和三个输出全启，因而拒绝该投票模式快照。

修正只改变只读解码：vote-run允许已锁定输出的OPMODE0/1，手动模式仍要求RUN；
要求所用main/even输出，保留LOCK、总输出、精确600MHz和除2检查。
不写PLL、不创建新票、不重调频，也不改变rail/GPIO/UART控制。
频率由寄存器和已核参考时钟推导，不是独立频率计实测。

实际旧函数稳定复现拒绝；修复后12个既有电源场景及344项断言通过ASan/UBSan，
包括真实快照、未锁定/未启输出、手动standby、未知OPMODE、错误整数/小数频率、
错误分频以及失败不改输出。两个ARM64/iOS17共享提供者/适配对象编译通过，
无FP/SIMD、原导出保留、共享规划器仍只有一份。

后续已以独立[v101 profile](../profiles/v101-bt-clock/README.md)完整重链、签包并上机，
旧v99指纹保持。v101实测父时钟检查和六路rail操作通过，power stage6；
随后的UART/peripheral/HCI合并流程返回−110，尚不能指定其具体超时子阶段。
stop/close成功，同步RPC回程和完整stock恢复通过。下一步先补准确子阶段诊断，
不改频率或猜测固件/接线；HCI身份、配对、持久凭据和睡眠仍待验。
