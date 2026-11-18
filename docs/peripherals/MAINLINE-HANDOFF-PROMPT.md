请继续你当前的 Y700 Elden 主线 Goal 和 Wi-Fi 修复。另一个线程已从 main@f82da007b624e32f2090d232896c3b9f4bd0a56e 建立独立分支 offline-peripheral-fixes，负责离线推进：AP501U 笔输入、双 AW86937 马达与 iOS 触感效果适配、RTC/唤醒、加速度计/陀螺仪与自动旋转、前后摄像头。

衔接仓库为 https://github.com/zenkernelsam/UniXNU_elden，分支 offline-peripheral-fixes。固定入口是 docs/peripherals/STATUS.md 和各组件文档。本地独立目录为 C:\Users\Cisco He\.codex\worktrees\offline-peripheral-fixes\UniXNU_elden。GPU 已交付内容仍单独在 codex/elden-gpu-offline，入口 docs/GPU-GOAL-STATUS.md 与 docs/GPU-MERGE.md，不要混淆两条分支。

2026-11-19 新证据：AP501U 的 F1/原厂校验/同报告 polar tilt，RTC 的真实 PMK8550 观察通道/系统时钟，以及双 AW86937 的 SE6/队列/工作线程/管理员用户接口和触控共享 GCC 修补。三者的固定 v152 离线链接 kernel4889eee6、Apple30 和各组件 ABI/停止顺序已检查，没有刷机或实机验收。入口为 PEN-NATIVE.md、RTC-WAKE.md、HAPTICS-NATIVE.md 与各 driver 的 validation JSON。马达校准为空，输出仍拒绝；连续 RTP、官方 waveform 编译和系统 Actuator 路由仍待完成。请评审后按当前主线源码移植，保留你的触控/SMMU/Wi-Fi/回程修复。

主线仍独占平板、刷写和当前 Wi-Fi/PCIe/WCN/QRTR 的实机迭代；离线线程不操作设备、不自动合并。你完成当前一轮并恢复安全基线后，再 fetch 并只读核对该分支的状态、已验证提交、构建/ABI结果及硬件缺口。只有组件明确具备集成证据后，才按当前主线源码评审、挑选对应提交和重建，不能整份旧目录/旧内核覆盖主线，也不能把规划或组件编译视为实机成功。注意共享电源、总线、IRQ、DMA及停止/回程顺序，保留你的最新修复。

板级实现优先 ArmadaOS-Freedom 的 TB322FC/SM8750 文档、固定内核和适用补丁，其次 Linux mainline；iPadOS 接口参考成熟 UniXNU 与同版 OS17 实现。不要搬用三代/五代的寄存器或固件。真机前重新核准入、候选完整摘要、原有 floor1738713600@0/父链3 和自动回程。

马达尤其需要机械/电气校准：Apple 效果强度不是硬件 gain，不能直接回放或线性放大后认为安全。仅启用有已验证增益/boost/频率/时长/双路预算和停止保护的效果；未校准连续RTP保持关闭。传感器方向和相机数据必须来自真实硬件；前后摄像头设备树节点存在不等于 ISP/出图完成。把接入这些分支作为后续衔接事项，继续当前 Wi-Fi 进度，不提前宣称五项已完成。
