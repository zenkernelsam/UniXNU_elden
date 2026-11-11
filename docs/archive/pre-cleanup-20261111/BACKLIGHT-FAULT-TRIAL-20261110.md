# AW99706启动事件与受控亮度试验

参考[Awinic正式搜索](https://www.awinic.com/cn/searchAll?keyword=AW99706)中的DS_AW99706_EN_V1.2（2025-10）及2024软件/硬件设计指南。工作仓research/backlight-fault-20261110保留三份PDF的URL/SHA和渲染核对。DS页34确认实际f8只启用LED1..4；open3c覆盖全部四路。0x10是OVL/OCP/OTP/UVLO汇总，之前未读，现已加入。

v44芯片只读实测：首次读取延后5秒仍open3c/FLAG08；之后8秒内五次全部清零，配置f8/亮度1597不变。569355B/drop0，0芯片写，控制器归还与150秒自主fastboot回程通过。资料未明确read-clear语义，后续零值不会重新启用失败的普通服务。

Backlight10新增独立mode4短试验。SettledTrial.h只接受实测精确启动事件，按100/1000/2000/4000/8000ms观察顺序检查全部零且配置/亮度未变；之后仍须新完整检查。当前试验仅把1597降至798保持500ms并还原。Aw99706每次实际写前及最终恢复后检查fault，未知传输结果/新fault停止、不重试。

231准入/241芯片/15678GENI sanitizer检查、实际4对象无FP/SIMD、原AppleARMBacklight169槽/0x370和3绑定通过。v45已收到trial-qualified0、roundtrip0/1597→798→1597/4写、release0/quarantine0/67事务；最终181790B/drop0/无断流，Root/Data/PID1健康，150秒本机回fastboot、stock完整SHA/Android/root/green恢复通过；工作仓HANDOVER-BACKLIGHT-TRIAL-20261110.md保存复现与恢复记录。

这里证明的是原生芯片寄存器控制，不是肉眼调光、CoreBrightness亮度滑杆、屏幕睡眠或功耗验收。下一步应补读CFG7的BRT_MODE（I2C选择），再在可停止且可靠恢复的较长窗口测量功耗；CFG0只表示DC/MIX调光方式。任何持续worker或service需补回程停止钩子与提前stop的竞争处理，不能直接把500ms延长就当完整电源驱动。

Power23也已补PD暂停中断：确认零后新GET非零立即恢复并记录Interrupted，318主机检查和原生机器码核验通过；不冒充实机故障注入或完整快充。
