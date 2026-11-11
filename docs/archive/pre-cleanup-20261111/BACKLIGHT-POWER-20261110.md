# 背光功耗与PD协同实测

v46/power23/backlight11，147构建输入；CFG7=05确认I2C控制，现增加写前模式/亮度/配置/保护核对。停止生命周期拒绝stop之后的新worker，长时worker可中断并恢复原亮度；服务清理经workloop串行，普通CoreBrightness仍待实测。

1000竞态、394芯片、16289GENI、232准入检查与实际对象/回程机器码/169槽ABI核验通过。只读首发、调暗中RPC、合PD完整对照均已在本机完成，保持原模块/HID/6GiB/RAM Data/8秒默认及固定boot index1738713600@footer0/父链3。

非PD对照：原亮度1597稳定电流449533uA，半亮度798为285400uA，仍放电；305秒RPC在dim阶段触发，先恢复1597，4写/stop1/归还/fastboot通过，1099633B/drop0。

合PD：9V/2.22A合同、20秒输入暂停/500mA新鲜读回、133252us脉冲归还通过。60秒原亮度、60秒半亮度、60秒恢复窗口，分别扣除前6周期：13样本0uA、14样本平均-140143uA（-144000..-138000，双FG均正）、13样本0uA。支持约140mA条件净充电，不证明最大PD功率/全亮度快充/长期能量平衡。

1295521B，测量前913字节丢弃，三个窗口无新增丢弃；USB没有重连，Root/Data/PID1健康。恢复1597、完整stop/sync/SDAM和450秒目标回fastboot通过。已恢复精确stock全SHA、Android/root/green并熄屏补电。

后续backlight12单独修复signed32 brightness负值转0（原unsigned64会夹到最大），参照固定sheng a68ee4ee。archive736357ab…/4对象/169槽核过，未整核未刷。仍需接真实CoreBrightness请求、可靠电池UI并重复周期。详细原始日志、候选完整SHA与判读命令在工作仓research/HANDOVER-BACKLIGHT-POWER-20261110.md。

用户新增音频约束：扬声器/功放及增益工作先讨论衰减单位、上限与保护实现，不能无人值守开声试增益；GPU仍最后，完整基础外设Goal未完成。
