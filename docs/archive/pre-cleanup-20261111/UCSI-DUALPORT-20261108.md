# Elden双口UCSI与供电原始诊断（2026-11-08，离线）

UcsiQuery读取两连接器capability/status，ACK命令完成；无PPM_RESET、角色或供电路径改动。
严格按48/528B版本布局、owner32779、length/return-code处理，在真实PMIC GLINK通道里复用，
不用另一个同名通道。UCSI1007项及45字段PMIC476项主机检查通过，iOS17编译/重链/签名/HID核验完成，未上机。

新开关`elden-ucsi-query=1`仅与`elden-adsp=2 elden-pmic=1`的原stock固件／通道资格配合。
UCSI GET/ACK本身不重置PPM，整个候选仍包含ADSP认证启动。保留原8秒自启和已验回程配置。
完整asset、session、阶段门槛在工作仓`research/HANDOVER-UCSI-DUALPORT-20261108.md`。

ROM原ucsi_qti_glink.ko已核read12B/write64B/read64B/buffer48；GPIO541是数据选择，不能用作充电FET。
原USB ONLINE写Lenovo55/SC7603_LOWPOWER，WLS setter无操作。USB/WLS对应实体口及55状态必须实际验证。
新PMIC原始轮询追加两路供电/限流/温度和55 GET；没有发送控制SET、v2 sink-path或GPIO写。

源snapshot分别冻结：原M3 first及新UCSI45。开发在设备源继续，引用manifest/schema固定snapshot，
防止后续更新让原签名候选的来源无法重建。现在仍缺实际PD/温控、UI电池发布、双口切换及睡眠等验收。
