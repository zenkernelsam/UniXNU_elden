# PMIH0104输入路径实测

本机原厂GLINK寄存器调试协议已由源码、实际模块及DT核对：owner32783/read0x37，请求28B、回复284B，bus0/SID7。直接SPMI与GLINK代理的PMIC身份一致：TYPE51/SUBTYPE56。host私有debugfs只设置软件读取游标并恢复，未写data寄存器。

实际ADSP固件代码读取270b bit4，按内部USB/WLS路径选择2952或2a52限流descriptor，两者50mA步进。新PowerPathRead只发固定270b→2952→2a52→270b四次单字节GET，核tuple/length/epoch，错误/超时停止，无write0x38或任意地址接口。932协议检查通过。

v49基线22组e2/USB500mA/WLS100mA，与原厂一致。协商期周期观察那轮未建立9V，关闭观察的同核对照成功；未凭一对对照认定唯一原因。v50增加1809调度检查，PD成功后10秒仅读一次，协商期/停止后不发请求。

v50在PD完成10.093秒后取得pathdb/db、USB限流0、WLS/DC限流500mA，9V保持，173043B/drop0/无协议错误。原亮度/PD恢复停止、RPC回fastboot通过。内部路径确有变化，上层“USB500mA”不可当成独立物理USB输入读数。

这不是倒灌保护或更高电流的许可。两个外部Type-C和内部路径的绑定、反向阻断/互锁、热插拔失效回退仍需依据原厂设计核查；不直接提高通用USB ICL、不使用debug override绕过温控。现有500mA预算、每启动一次短脉冲保持，没有新OTG/FET策略。

已恢复精确stock全SHA、Android/root/green、熄屏补电。COM7开口时出现一次枚举竞态，写前失败，核对GPT和当前测试像完整SHA后完成恢复；后续工具只在协议开始前有限等待FileNotFound2，拒绝占用和异设备。详细证据及完整候选SHA见工作仓research/HANDOVER-INPUT-PATH-20261110.md。

下一步可取原固件charger ULog（源码正确名charger-ulog-glink.c），但8204B回复超出现有2048B日志intent，必须先设计有界接收，不能直接开启。系统亮度/真实电量/状态栏成果保留；完整充电和外围目标未完成。音频先讨论衰减及上限，GPU最后。
