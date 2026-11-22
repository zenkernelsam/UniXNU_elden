# 外设源码与文档入口

当前状态以 [STATUS](STATUS.md) 为准；给主线的短提示是
[MAINLINE-HANDOFF-PROMPT](MAINLINE-HANDOFF-PROMPT.md)。各组件说明保留独立
构建、ABI 和证据引用，旧逐轮状态已按原字节放入 archive。

| 要查什么 | 当前路径与顺序 |
| --- | --- |
| 相机连续中断／原执行器 | [CAMERA-IRQ](CAMERA-IRQ.md)，irq-validation.json（当前候选） |
| 最新相机帧与 OS17 服务 | [CAMERA-FRAMES](CAMERA-FRAMES.md)，bridge-validation.json 为接口父交付；frames-validation.json 为旧凭据 |
| 共享 early SMMU／内存 | [SHARED-SMMU](SHARED-SMMU.md)，SharedNative.h、Peripheral.h、shared-validation.json |
| 相机完整底层链 | [CAMERA-DMA](CAMERA-DMA.md) → [CAMERA-CLOCKS](CAMERA-CLOCKS.md) → [CAMERA-POWER](CAMERA-POWER.md) → [CAMERA-PIPELINE](CAMERA-PIPELINE.md) → [CAMERA-CCI](CAMERA-CCI.md) → [CAMERA-CORE](CAMERA-CORE.md) |
| AP501U／Pencil HID | [PEN-NATIVE](PEN-NATIVE.md)，EldenPen/NativeBridge.{c,h}、Owner.{c,h}、protocol-validation.json；原 Android 可行性 docs/AP501U-PENCIL-FEASIBILITY-20261114.md |
| 原 touch／pen 可逆暂停与亮屏 | [INPUT-SLEEP](INPUT-SLEEP.md) → [WAKE-IRQ](WAKE-IRQ.md) → [WAKE-PROTOCOL](WAKE-PROTOCOL.md) |
| RTC／时间持久化边界 | [RTC-WAKE](RTC-WAKE.md)，EldenRtc/Owner.{c,h}，EldenRtc/validation.json |
| 马达 iOS 系统路线 | [HAPTICS-ACTUATOR](HAPTICS-ACTUATOR.md) → [HAPTICS-BROKER](HAPTICS-BROKER.md) → [HAPTICS-RTP](HAPTICS-RTP.md) → [HAPTICS-NATIVE](HAPTICS-NATIVE.md) → [HAPTICS](HAPTICS.md)；tools/build_wave_manifest.py + test_wave_manifest.py（manifest 生成器） |
| 传感器当前运行与原 QRTR | [SENSOR-RUNTIME](SENSOR-RUNTIME.md) → [MOTION-QRTR](MOTION-QRTR.md) → [ADSP-CHANNELS](ADSP-CHANNELS.md) |
| 传感器 pack／RPC／内存 | [SENSOR-MEMORY](SENSOR-MEMORY.md) → [SENSOR-PACK](SENSOR-PACK.md) → [SENSOR-RPC](SENSOR-RPC.md) |
| 采样到 OS17／旋转缺口 | [MOTION-SSC](MOTION-SSC.md) → [MOTION-HID](MOTION-HID.md) → [MOTION-CORE](MOTION-CORE.md) |
| Mac 输出退役与找回 | [MAINTENANCE](MAINTENANCE.md)，workbench maintenance/peripheral-cleanup-20261121、camera-bridge-cleanup-20261121、camera-irq-cleanup-20261121 |
| 旧状态、原 AGENTS／handover | [归档索引](../archive/peripherals-20261121/INDEX.md) 与原字节 manifest |
| 主线／GPU | 主线自己的 docs/INDEX.md；独立 GPU 分支 docs/GPU-GOAL-STATUS.md、docs/GPU-MERGE.md |

当前文档说明“已离线链接”时，只对实际记录的来源/组件成立，不说明已实机。
相机／传感器的物理 provider、iOS 用户态可用性与系统效果校准需要各自功能性
验收。不能用旧候选整内核覆盖主线；按当前唯一 owner 接口移植源码并重建。
原系统／厂商固件、密钥和设备备份仍是外部输入，不进源码 Git。
