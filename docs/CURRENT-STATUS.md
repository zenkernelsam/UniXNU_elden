# 当前状态 · 2026-11-12

最近已验基线为 **v91**：iPadOS 17.7.11 (21H461) 在 Y700 Gen4 / TB322FC 上进入
SpringBoard，通过一次软件上滑、一次短按 Settings，收到实际渲染的 Settings/General 页面。
这证明该软件输入路径可用，尚不能宣布真实手指操作或日用性能验收。

| 项目 | 已有证据 | 尚未完成 |
| --- | --- | --- |
| 根卷与用户态 | UFS 上的 iPadOS 根卷、RAM Data、launchd、SpringBoard、Settings | 持久 Data、完整应用场景 |
| USB 调试与回程 | 内核日志、帧回读、会话绑定 RPC、panic 后定时定向 fastboot | 断线及供电变化下的完整长期稳定性 |
| CPU | CPU8 配置及保护、调度/IRQ 调查已进入冻结基线 | 不能用配置核数代替持续在线/负载实测；性能仍待定位 |
| 显示与输入 | GOP CPU 显示、软件上滑/短按、触点/HID 链已推进 | 真实手指准确操作、流畅性和鬼触排除 |
| 电源 | 电池状态、背光、原厂 ADSP/GLINK、UCSI 事件及 PD 流程已有实测 | 双口共享 500mA 约束、完整快充/回退/保护工况 |
| Wi-Fi / 蓝牙 | 已有板级资源、固件与部分传输组件 | 联网、配对和持续使用验收 |
| GPU / 音频 | 参考实现和板级调查保留 | GPU 最后；扬声器先讨论衰减与保护 |

v91 的完整日志有 **24 字节 dropped**；对应 Settings 帧 CRC 和所统计的光栅事件保留。
不得把这轮写成全程无丢字节。详细边界与 SHA 在
[`profiles/v91/hardware-baseline.json`](../profiles/v91/hardware-baseline.json)。

v92 仅为离线 framebuffer 映射实验，尚未完成签名、资格与实机验证。
当前设备已恢复 stock Android。本轮整理、源码重建和编译均未访问平板。

## 冻结指纹

- v91 boot：`879ef26b2f438bc63856769deee0d1b0790d51a55b30bcc57f5fb92d34ca746f`
- v91 kernel：`2d72dea2e1b69adf063a9db2b91b06cb16e1bc880425a868f62f6611989efe69`
- v91 owned dylib：`f748e9cf601db43167eaf7736b0a161771b8c01eeb46860820604ccf892ac034`
- stock boot：`8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890`

Boot rollback floor 始终为 **1738713600 / footer location 0**，父链 location 3。
写入前核对目标、fresh 电量准入和完整镜像；写后完整 SHA 读回。
本文不是其他设备的刷写授权或通用刷写指南。

## 源码重建候选

2026-11-12 已完成从固定源码重建内核、FD、加载器与 Data 种子，并打包签名。
候选 SHA `4866c181ffb9c4d927e3d4c4cc3833110d80853288947109e03c0566c646a9b5`，会话 `3cf1627c47aa4365`，
通过完整独立静态核验，尚未刷入。已有硬件结论仍属于原 v91；本次没有新增外设验收。
复现结果见 `profiles/v91/validation/source-closed-candidate.json`。
