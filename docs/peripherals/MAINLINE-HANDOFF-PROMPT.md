请继续你当前的 Y700 Elden 主线 Goal 和 Wi-Fi 修复。另一个线程已从 main@f82da007b624e32f2090d232896c3b9f4bd0a56e 建立独立分支 offline-peripheral-fixes，负责离线推进：AP501U 笔输入、双 AW86937 马达与 iOS 触感效果适配、RTC/唤醒、加速度计/陀螺仪与自动旋转、前后摄像头。

衔接仓库为 https://github.com/zenkernelsam/UniXNU_elden，分支 offline-peripheral-fixes。固定入口是 docs/peripherals/STATUS.md 和各组件文档。本地独立目录为 C:\Users\Cisco He\.codex\worktrees\offline-peripheral-fixes\UniXNU_elden。GPU 已交付内容仍单独在 codex/elden-gpu-offline，入口 docs/GPU-GOAL-STATUS.md 与 docs/GPU-MERGE.md，不要混淆两条分支。

2026-11-19 新证据：AP501U 的 F1/原厂校验/同报告 polar tilt，RTC 的真实 PMK8550 观察通道/系统时钟，以及双 AW86937 的 SE6/队列/工作线程/管理员用户接口和触控共享 GCC 修补。三者的固定 v152 离线链接 kernel4889eee6、Apple30 和各组件 ABI/停止顺序已检查，没有刷机或实机验收。入口为 PEN-NATIVE.md、RTC-WAKE.md、HAPTICS-NATIVE.md 与各 driver 的 validation JSON。马达校准为空，输出仍拒绝；连续 RTP、官方 waveform 编译和系统 Actuator 路由仍待完成。请评审后按当前主线源码移植，保留你的触控/SMMU/Wi-Fi/回程修复。

随后新增 WAKE-PROTOCOL.md（原厂双击15/50:13/50:11命令与过滤）、MOTION-CORE.md（无假重力的SSC协议/单位/安装矩阵/旋转保持）、CAMERA-CORE.md（精确Gen4传感器原始配置/电源/控制/启停逻辑）。三者目前是离线核心与对象证据；GPIO/PDC唤醒、真实Gen4传感器传输/HID、CCI/CAMSS/出图和相机服务仍待接。不要把参考DTS或主机模拟回包当作设备实测，不要移入T970的SM8250寄存器或假传感器。工作台 lab/records 和 lab/reference-reviews 已有 compact 交接记录；完整大清单在已校验的私有归档，重复副本按用户要求进入Windows回收站。

HAPTICS-RTP.md 已补原厂RTP参数、GENI64B FIFO扩展、双通道最多60秒流和实际C SampleMap驱动的142条官方配方/17AHAP编译测试，离线核14f945b5与原有ABI/回程检查通过。测试模板/增益是明确标记的模拟peer，不是实测Taptic转换；生产拒绝该bank。原生RTP请求/目录/持久预算及系统Actuator仍待接，测量校准前输出关闭。AES只表示接近空，不是完成；双24k不能只凭I2C400k时钟当作带宽足够。请保留当前主线全部新修补。

HAPTICS-BROKER.md 为后续最新衔接：原生v2客户端增加effect-id请求，RAM/RTP同一worker串行调度，共享预算跨客户端保留，实际STOP未确认则保留所有资源，目录还会重新编译来源逐字节比对。kerneld64231c6/12objects/Apple30及原有ABI回程检查通过。默认目录仍0/增益0，正向目录schema测试为HOSTONLY模拟fixture，不能当作校准候选。真实系统Actuator、能力/调音资源发布、实测waveform/双马达增益/温度/带宽仍需继续完成；别提前打开系统haptics开关或绕过预算/回程。

主线仍独占平板、刷写和当前 Wi-Fi/PCIe/WCN/QRTR 的实机迭代；离线线程不操作设备、不自动合并。你完成当前一轮并恢复安全基线后，再 fetch 并只读核对该分支的状态、已验证提交、构建/ABI结果及硬件缺口。只有组件明确具备集成证据后，才按当前主线源码评审、挑选对应提交和重建，不能整份旧目录/旧内核覆盖主线，也不能把规划或组件编译视为实机成功。注意共享电源、总线、IRQ、DMA及停止/回程顺序，保留你的最新修复。

最新衔接 HAPTICS-ACTUATOR.md：iOS17签名HAL、真实type1客户端、397312B新会话ring和原有SE6/AW worker已连接源码并完整离线链接kernel6c644610/native4a674e31。13原生生命周期场景/140检查、135实际HAL接口、4117ring与285013实际live组合通过；16scalar对象、168service/186client及pen/RTC/Apple30/停止顺序复核通过。特别修复捕获头文件的IOBuffer getBytesNoCopy错误槽43，现在直接调用OS17原始导出；原生prepare槽27和所有调用有审计，旧错误件拒绝。默认校准/目录/Actuator profile仍空，未驱动实机、未打开MG/closed-loop/preset或VirtualAudio补丁。先按当前主线review/移植，保留全部Wi-Fi/SMMU/触控/GPU/回程；实测gain/双负载/温度/带宽及系统type1权限后才逐层开放。私有归档22802522B/663bad26、264members已核，工作台lab/records/haptics-actuator-20261119。原生wake/SSC/camera和完整五项Goal继续，不因这批离线链接清flag。

随后新增 WAKE-IRQ.md：精确Gen4 GPIO162→PDC109→GIC SPI620/INTID652，TLMM target位8，PDC新旧版启用语义已按原Git核准。真实原生IRQ和序列化pause/release/50:13/读包/请求唤醒/power-ready/restore流程已离线构建并链接kernel63486fdf/nativebbd93bc8；50route/12nativeIRQ场景及1233flow检查通过，Apple30/Actuator/pen/RTC/回程仍通过。它还没有hook当前touch worker或display/system PM；GOP的逻辑PM不是panel上电证据，terminal pen/touch stop也不能当可逆pause。主线后续接真实非terminal释放/排空/面板电源/原固件恢复，实测PDC版本/中断/手势后验收亮屏。原生<3.2缺统一bank provider时明确拒绝，不接管共享summary240或peer652。私有19273807B/08e9be2d、53members已核，workbench lab/records/wake-irq-20261119；五项Goal继续。

板级实现优先 ArmadaOS-Freedom 的 TB322FC/SM8750 文档、固定内核和适用补丁，其次 Linux mainline；iPadOS 接口参考成熟 UniXNU 与同版 OS17 实现。不要搬用三代/五代的寄存器或固件。真机前重新核准入、候选完整摘要、原有 floor1738713600@0/父链3 和自动回程。

马达尤其需要机械/电气校准：Apple 效果强度不是硬件 gain，不能直接回放或线性放大后认为安全。仅启用有已验证增益/boost/频率/时长/双路预算和停止保护的效果；未校准连续RTP保持关闭。传感器方向和相机数据必须来自真实硬件；前后摄像头设备树节点存在不等于 ISP/出图完成。把接入这些分支作为后续衔接事项，继续当前 Wi-Fi 进度，不提前宣称五项已完成。
