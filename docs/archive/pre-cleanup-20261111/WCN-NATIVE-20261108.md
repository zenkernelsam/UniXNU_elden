# WCN7861 原生主机桥离线交档（2026-11-08）

接续 `HANDOVER-WCN-POWER-20261108.md`。本页结束上一轮未收尾的构建状态；没有设备操作。
用户重新指定电池/双口充电、触控、Wi-Fi、蓝牙优先，接下来先推进电池。

构建及独立审计已经结束，`edl_work/wcn-native-20261108/final/independent-audit.json` accepted=true。
原生 UART.SK 控制端、实际 HCI 桥、单一在途命令、连接代次、准确 bluetoothd 进程引用和停止路径
已编译/预链接；原始 30 个执行组件、HID 207 槽/52 绑定及五个固件完整副本保持。

最终核 SHA256 `276b03e402366e97fb12fb9675ae5485aec1427d95c20fd5af10531161b3ce33`。
两件均 96MiB，boot **1738713600@footer0 / 父链3**，未刷：

| 变体 | SHA256 | 含义 |
|---|---|---|
| off | 64987719f3ed72a8ace825480f061d3ff36f19b8d77f117d08fbb4af8e674d50 | mode0，无射频启动 |
| live | 1de6c1e3a09dff2e4b67758d10fdb8b5d6199749e6a7d0c474e5bd789e9c626f | mode4，实际 WCN 电源/UART/HCI→主机桥；并非只读 |

桥模型 6 场景/367 断言 ASan/UBSan通过，成功源码冻结为 `research/wcn-native-20261108/source-compiled/`。
没有启用原始 bluetoothd 文本适配，也没有开启 RAM Data 中的 launchd job，因此不能称系统蓝牙可用。
物理控制器、扫描、配对和睡眠都未验。

正确 Y700 21H461 bluetoothd 已从本项目 OS.raw 验 SHA 后只读提取并卸载，9893760B，
SHA256 `3f1e4493ea6a76976625f7af58a8f330c0c90d82d9020b11110b4f3f5b758df4`；
CDHASH `fe7bc182386a2766c7574f8b48412ff9df113564`。本机 Mac 的 `/Volumes/iOSRoot` 是 Sony 的 iOS16，不能用。

以后续接 daemon：已对版原偏移 0x2ef860 的 `ff8304d1e9230c6d` 和 0xddd20 的 `a0050034`。
参考 dada/Enc3l 的验证后页映射钩子，须绑定该原始 build/CDHASH/程序及原字，并同步恢复 launchd job。
不能把文件名匹配或未知 build 的修改作为通用适配。持久配对凭据需要真实安全 KeyStore/Data，RAM 状态层不满足。

设备最后确认仍 stock Android 补电；现有 GUI/USB/8 秒自启/180 秒及 RPC 回程基线保留。
完整日用 Goal active，本阶段是离线进展。
