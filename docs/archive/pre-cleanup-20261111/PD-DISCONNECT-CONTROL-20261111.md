# 单口／双口充电对照准备

目的是区分基础PD充电路径与双输入报告／限流策略，不把SDP500mA预算误当成独立DC接口。源码证据见charge-control-route-20261111/vote-retirement-review.json。完整快充、插拔兼容性和双口保护仍需验收。

已审核候选（**未刷**）：

- edl_work/basic-bundle-20261108/v72/final/pd-handoff-quiet-focused-gestures/boot-signed.img
- SHA256 e1edfb1c329732978dd1b91e4bd467b60656dc2a5aaad309e80e3b3048718d00
- session ce209214abfedb57，核c07c5fcce0269e74886a67a364f9260834856b3f115f947f71defec088238100
- 同一v72核/power35/backlight13，保留typed-SDP2、UCSI DA65、500mA预算、温控及原有PD流程；无新的DC/FET/电流控制。关闭ULog、frame-preview与wait-stack诊断；目标450秒回fastboot。
- 原30模块、HID/背光ABI、冻结源、原厂属性和1738713600@footer0/parent3均审核通过。复用v59中逐字相同的背光/PD等资格，未重编驱动。

前一次打包缺背光资格文件时输出未通过完整审核，文件保留为boot-before-complete-audit.img/signing-before-complete-audit.json，**不是可刷件**。最终仅以上e1ed…件有完整packet和independent-audit.json。后续打包命令必须在前一步成功后才继续签名。

执行顺序（等用户在场，不能凭超时当同意／就绪）：

1. 当前stock Android；重新读取HA2A1PT2电量/温度，fastboot必须>=3500mV且soc-ok=yes。启动绑定会话的USB监听后boot_a-only写入，全像SHA确认。
2. 自动首项启动；先在原生双口取得完整PD phase6/error0/restored1、实际合同/电压及限制票基线。若本轮未完成该阶段，保存原因并恢复Android，不让用户在进行中的控制阶段拔线。
3. 提示用户短边PD保持，拔长边电脑线约20秒再接回同一口。用户记录拔线前/期间电流表电压与电流；PC预先允许90秒重连。同核同boot nonce、USB掉线边界和目标dropped计数都要检查。
4. 核接回后的原始字段/票据变化和恢复状态；通过已验证RPC回fastboot并恢复stock，完整SHA、Android/root/green、熄屏。绝不为了更高读数增加电流值或修改ACL。

USB required=1检查的是控制器初始化状态2，不要求DTR；PowerStart的前提是SDAM/RAM Data/init exec，源码没有把主机连接作为充电启动条件。短时断线对照因此不需关闭USB资格门或更换内核。但DWC3长时间无主机冷启动和PD-only净功率尚未实测，不能由源码直接宣称通过。

高SOC限制：当前接近满电，适合辨认报告与限流票的撤销/恢复，不适合用低电流判定快充失败。完整正常UI负载下净充电仍需较低SOC的实测。缓存128KiB，短断线仍可能存在丢字节；若出现缺口，只采正向明确记录，不以缺失内容证明“无异常”。
