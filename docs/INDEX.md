# 文档入口 — offline-peripheral-fixes

| 目的 | 入口 |
| --- | --- |
| 本线程五项外设当前状态 | [peripherals/STATUS](peripherals/STATUS.md) |
| 组件源码／ABI／构建顺序 | [peripherals/INDEX](peripherals/INDEX.md) |
| 主线接入提示 | [peripherals/MAINLINE-HANDOFF-PROMPT](peripherals/MAINLINE-HANDOFF-PROMPT.md) |
| 最新相机 CMIO／真实帧 | [CAMERA-FRAMES](peripherals/CAMERA-FRAMES.md)、frames-validation.json |
| 共享 SMMU／early 原 owner | [SHARED-SMMU](peripherals/SHARED-SMMU.md)、shared-validation.json |
| 新克隆构建／外部输入 | [CLEAN-BUILD](CLEAN-BUILD.md)、[CLEAN-INPUTS](CLEAN-INPUTS.md) |
| 本周归档与 Mac 垃圾桶找回 | [peripherals/MAINTENANCE](peripherals/MAINTENANCE.md) |
| 旧完整 AGENTS／status／handover | [archive/peripherals-20261121](archive/peripherals-20261121/README.md) |

主线的当前状态、Wi-Fi、充电与设备回程，使用主线自己的 docs/INDEX.md。
本分支历史主线 docs/PLAN/WIFI-STATUS 只用于来源复现，不替代当前主线进度。
GPU 分支保持独立；本线程不自动 merge、访问平板或清除五项 Goal。
