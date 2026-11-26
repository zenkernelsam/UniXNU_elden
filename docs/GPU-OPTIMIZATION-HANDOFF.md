# Y700 Elden A830 离线优化交接

2026-11-22 · 接手分支 `codex/elden-gpu-offline`。A830 是 Elden 的首项离线外设交付。当前任务是继续离线优化。主线完成 Wi-Fi 后，使用当时的 `main` 复现并逐项验证，通过的外设与 GPU 源码按依赖顺序合入 `main`。

## 已验证优化基线（gpu-opt-001..011,HEAD `e121465`)

2026-11-27 更新。十一轮离线优化全部以 fresh solo 20 阶段链验收（每轮 `returncode=0`、`resumed_userspace: false`),GPU 专项 ABI 逐轮逐字节相同（136B / 168 slots / `[0,1,7,39,40,41,140]` / C2+D2),30 个原 Apple 执行区保持，legacy prelinker 全局标志全 `false`,`hardware_qualified`/`device_access`/`boot_image_packaged`/`live_policy_selected` 全 `false`。各轮证据在 `lab/records/gpu-opt-001..011`,交接在 `_relay/gpu-opt-*/handover-to-codex.md`。

已验证并合入的基线内容：

- **原生 `worktree_file` 兼容覆盖**(opt-006):`prepare_gpu_tree.py` 支持 worktree 文件形式的 compatibility inputs,`config/inputs/gpu-metal-vulkan-inputs-779eb522.json` 原生固定翻译器 pin,准备树零手工 overlay。`config/inputs/build-adreno-metal-bridge.py` 为官方 `8542980` 构建器仅一行补丁名差异。合并的 AdrenoMetal(`AdrenoMetal-official@cc0a4e8` 合到 A830 fork `8f90243`）经系统性三方审计，验证提交 `cc01fa331982983169b019f7cb0218e2f25143b5`；持久参考 bundle 在工作台 `lab/records/gpu-opt-006/AdrenoMetal-a830-opt6-cc01fa33.bundle`。
- **ArmadaOS Mesa/Turnip A830 补丁**(opt-007):`0002`(freedreno A830 chip-id 变体)、`0003`(ir3 bindless UBO const 门控)、`0015`(ir3 array-to-SSA phi 链）逐字进入 Mesa 补丁序列（索引 24-26)，经 `mesa_patch_identity` 原生记录与 `test_gpu_turnip_patches.py` host 验证（strict apply、series tail/overlap、chip-id 注册、`lookup_value` 与 UBO 门控单例）。
- **KMD 诊断对等**(opt-008):pin `9c34ea46` 已携带 nbNote 内核笔记（method 18、512 行惰性 ring)、GPU priority 归因（newest-32 ring)、有界 frame trace(NativeGpuTrace v1,16384+4096）的完整等价实现；`tools/test_gpu_kmd_diag.py` 以精确提取+内核 shim 在 ASan/UBSan 下验证全部行为，抢占谱系（e971110/ccabc7e/b895643/7b6245f）确认排除。
- **Linux SM8750 DTS 对等**(opt-009):`test_gpu_board_inputs.py --linux` 阶段验证 14 GPU OPP、3 GMU OPP、4 个 speed-bin 熔丝（190/221/232/242)、寄存器孔径、IRQ/SMMU 与 pin `6f97250e` DTS 逐一相同，含 11 项负面/边界拒绝用例；cx_dbgc 合法子窗口包含关系已记录。`live_policy_selected` 保持 `false`,DCVS/IFPC/ACD 严格推迟。
- **统一离线验证门**(opt-009):`tools/verify_gpu_offline_suite.py` 顺序运行 board-inputs、submit-timing、turnip-patches、kmd-diag 四个 host 套件并验证 17 项护栏，输出单一机器可读 `offline-verification.json`(`passed: true`)。后续轮次以该门为验收标准。
- **呈现/帧时序诊断与关机排空对等**(opt-011): 独立 host 套件验证 FrameBridge 16 槽诊断（逐项拒绝归因、像素格式转发、KMD fence 关联、fallback/copy/validation 计数）、12 项真实排空用例（超时终止、时钟回退/停滞、悬挂 fence、活调用、零超时拒绝）与 GPU 排空前置于 UFS 停机的关机次序；并入验证门为第五阶段 `recovery-frame-diag`。
- **Gen80000 固件结构与原生告警卫生**(opt-012):`tools/test_gpu_firmware_and_warnings.py` 独立套件验证固件锁完整性、4 个 pin 固件字节+SHA256 同一性、Qualcomm 再分发声明在位、ZAP ELF32/MBN 结构（独立重实现与 pin 解析器输出逐字节互证）、DT 保留上限，含 14 项变异负向用例（截断/坏 magic/程序头越界/重叠段/缺 hash 段/超 span/翻转字节等）;`gpu-native-warnings.json` 全部条目与树文件哈希核对（含 `.inc` 嵌套站点语义）,native 构建 27 单元 14 告警全部已评审，评审匹配逻辑 fail-closed 三模式实证。并入验证门为第六阶段 `firmware-warnings`。

明确的实机验收推迟项（均未做，勿在离线轮宣称）:ZAP/PAS 认证签名与装载、GMU/HFI 真实握手、基本 BO/map/submit/fence 硬件路径、正确像素与系统呈现、Metal/IOSurface 真实客户端、DCVS/IFPC/ACD 调频与省电、suspend/resume、双 ring 抢占与 GMEM restore、DPU 直扫、功耗与续航测量、GPU drain 在 UFS 停机前完成的实机次序。`metal2vulkan-rt` 迁移未定：上游已将 `metal2vulkan-runtime` 扁平重写，FFI 缺口由 `tools/audit_metal2vulkan_rt_gap.py` 产出的 `m2v-rt-gap.json` 逐项记录（见 opt-010 交接）。

## 接手入口与现有基线

- Windows GPU 工作树为 `C:\Users\Cisco He\.codex\worktrees\elden-gpu-offline\UniXNU_elden`，主线为 `D:\Desktop\Git\UniXNU_elden`，工作台为 `D:\Desktop\Git\Patch\Y700-Elden`。先读三处 `AGENTS.md`，再读本交接及分支的构建、合并和验收记录。`offline-peripheral-fixes` 单独交付 AP501U、双马达、RTC／唤醒、运动和相机。[构建](GPU-BUILD.md) · [GPU 合并](GPU-MERGE.md) · [离线验收](GPU-GOAL-STATUS.md) · [其他外设真机手册](https://github.com/zenkernelsam/UniXNU_elden/blob/offline-peripheral-fixes/docs/peripherals/REAL-HARDWARE-RUNBOOK.md)
- `71b537909ca538830aaa79b939d0767168ee0360` 记录验收，实际构建源码是 `9fca00698102d4631941a5a5d3743fbf2d212b66`，固定基础内核源码是 `a13e5087a8651b3185a0855eacf94923a21e97eb`。A830 C++、Elden runtime、GOP、内核 prelink 与 entry、Rust bridge、Turnip 和 Metal 插件均通过离线构建。GPU 专项 ABI 和 30 个原 Apple 执行区已核对。Y700 的 ZAP 认证、命令提交、正确像素、系统呈现和功耗仍待实机验证。[完整构建证据](evidence/gpu-complete-20261116.json)
- `edl_work/gpu-complete-20261116/elden-a830-offline.tar.gz` 在 Windows 工作台，SHA-256 为 `cef8999016da9aeb3ff69c0ca73043ad839814b58a3dfcaa352374e8069da1f1`。`lab/records/gpu-complete-20261116/` 有 `stages.json`、`result.json` 和 ABI、交付回执。Git 提交保留源码和精简证据，固件、系统资产及原始产物留在工作台和 CiscoMac。[交付回执](evidence/gpu-delivery-20261116.json)

## CiscoMac 的两个 GPU 工作树

| 路径 | 角色 |
| --- | --- |
| `/Users/ciscohe/Desktop/UniXNU_elden-gpu-offline` | GPU 源码工作树。保留 `lab/gpu-goal-20261115/complete-3` 验收输出、`fresh-base` 基础构建、`complete-inputs2` 固定输入和 `delivery-20261116`。新试验输出用新目录。 |
| `/Users/ciscohe/Desktop/UniXNU_elden-gpu-baseline` | 干净的 detached 工作树，固定在 `a13e5087`。直接供 `build_gpu_complete.py --baseline-source` 调用，该脚本核对完整提交和 tracked 源码状态。`complete-3/stages.json` 的基础构建命令记录了此绝对路径。保留原目录和提交。 |

两者共享 `/Users/ciscohe/Desktop/UniXNU_elden/.git`。验收构建的 `--base-build` 指向 `fresh-base`。整理时曾移走旧 `tree` 中历史命令引用的 metal2vulkan 缓存，现已从 `complete-3/tree/.cache/gpu-reference/metal2vulkan` 补成独立 bare 缓存，原路径仍为 `lab/gpu-goal-20261115/tree/.cache/gpu-reference/metal2vulkan`，对象为 `624bab3918de5b978e1101517abca2b4fa426565`。已重核 `fresh-base/build-state.json`、11 件 `complete-3` 产物的 SHA-256 和成功阶段的本机命令路径。

**GPU 的 C/C++、Rust、Mesa、着色器工具链、链接、签名、打包及编译型回归全部在 CiscoMac 上跑。** 通过 `ssh ciscomac` 使用现有 Xcode、LLVM、Rust `aarch64-apple-ios`、私有 shader prefix 和 Bison 3.8.2。Windows/WSL 只做 Git 和源码审查、既有 Python 输入准备、摘要核对、文件传输与 Devin 派发。Windows/WSL 禁止下载或安装构建工具。手册的 Windows 准备命令使用已经安装的 Python 3.12。按 `GPU-BUILD.md` 和 `stages.json` 提取实际参数，使用新的输出路径保留本轮日志。[构建流程](GPU-BUILD.md)

在 CiscoMac 的 GPU 工作树中运行基线对照。已核对 `complete-inputs2/prepared.json` 及其 2520 项输入 SHA-256，并确认现有 shader prefix 中的 `glslc`、`spirv-val` 和 `glslangValidator`。

```sh
cd /Users/ciscohe/Desktop/UniXNU_elden-gpu-offline
gpu_lab="$PWD/lab/gpu-goal-20261115"
/usr/local/bin/python3 tools/build_gpu_complete.py \
  --prepared "$gpu_lab/complete-inputs2" \
  --baseline-source /Users/ciscohe/Desktop/UniXNU_elden-gpu-baseline \
  --base-build "$gpu_lab/fresh-base" \
  --inputs /Users/ciscohe/Desktop/UniXNU_elden/lab/clean-build-20261114/isolated/inputs.json \
  --shader-prefix /Users/ciscohe/Library/Caches/elden-gpu-toolchain-20261115-2 \
  --bison /usr/local/opt/bison/bin/bison \
  --mesa-cache "$gpu_lab/tree-current/.cache/mesa-work" \
  --runtime-cache "$gpu_lab/tree/.cache/gpu-reference/metal2vulkan" \
  --output "$gpu_lab/baseline-replay-$(date +%Y%m%d-%H%M%S)"
```

对照用的是验收输入锁。实际 Turnip 构建的 Mesa 是 `85fda3b36405bcaea8bffc64aa4f2651dc90770d` 加 23 个补丁，取自 `scripts/data/adreno-compiler-inputs.json` 和验收构建的 dependency receipt。Mesa 与 metal2vulkan 的固定 Git 对象都已在上面指定的缓存中核对。升级轮按候选锁重新准备输入，并使用含候选提交的缓存。新旧构建分别保留 `stages.json`、`result.json` 与输出摘要。

## 核对官方移植指南与供体

官方 A740 路径为 `Metal → AdrenoMetal → metal2vulkan → Turnip → Darwin KMD → Adreno740 → A740`。Elden 的末端由 `a830-sm8750` fork 的 Gen8 KMD 接 A830。官方要求先打通平台 DMA 与基本显示，再匹配 GPU 后端、编译器支持、目标 OS GPU/IOSurface ABI。`components.toml` 的五组件列表出自 Component Catalog 的配置示例，解析器按依赖补齐。Elden 现有 OS17 构建由 `config/gpu-build.lock.json`、`config/gpu-firmware.lock.json` 和 Cargo 锁配套驱动，继续以锁定链路复现。[官方 GPU 指南](https://unixnu.com/porting/gpu) · [Component Catalog](https://unixnu.com/reference/components) · [设备配置与解析器](https://github.com/UniXNU/UniXNU/blob/8542980d02b4d2cc5c32ee3542de1760b2562aac/docs/porting/device-repository.md)

```toml
[components]
include = ["ArmDTPlatform", "ArmCpu", "Adreno740", "AdrenoMetal", "GopFramebuffer"]
```

| 参考 | Elden 可复用的工作 |
| --- | --- |
| Adreno740 A830 fork / SM8750 Pagani | 已固定 `9c34ea46` 和 Pagani `a84204ec`。沿现有 Gen8 寄存器、CP/ring、GMU/HFI、SMMU 和六缓冲区上下文继续。官方 A740 的提交、fence、故障隔离、诊断可借鉴。双 ring 抢占、GMEM restore、寄存器和 packet layout 要逐项核实 A830 行为。 |
| AdrenoFirmware / AdrenoPM4 | 已有 Gen8 固件装载、HFI 与 PM4 kit。按现有 A830 CP 操作验证 opcode 和布局，增量补齐实际缺的包。当前锁文件记录公开 `qcom/gen80000_gmu.bin`、`gen80000_sqe.fw`、`gen80000_aqe.fw` 和 `qcom/sm8750/gen80000_zap.mbn`。 |
| ArmCpu / ArmDTPlatform / ArmSmmu4K | 复用启动、GIC、计时、MMIO 及 SMMU 的接口。核对真实 stream ID、上下文、地址宽度、映射和缓存归属，并接当前主线已有 owner。TB322FC OPP、带宽、保留内存和 GMU 输入见本分支板级证据及 `EldenGpuVotes.c`。 |
| GopFramebuffer | 已固定 `b0d812d` 的 presenter，Elden 帧入口走 BGRX8888，有 CPU fallback。`qcom,sm8750-dpu` 对应 DPU 12.0。扩到 DPU 直扫或其他格式前，先核 Y700 面板时序、stride、像素格式、显示 DMA 和 fence 生命周期。 |
| AdrenoMetal / metal2vulkan / Turnip | 可借鉴 shader cache、purgeable、IOSurface、拷贝与运行时调度。把 OS17 前端协议、A830 能力声明、翻译器、Rust patch、Mesa 与 Darwin KMD 当作配套版本一起验证。`build-turnip-darwin.py --target ios` 已在当前分支使用。用锁文件和具体 A830 编译、设备 feature 证据评估版本。 |
| AirNir | 用于离线着色器编译，可对照同 OS 的 `.metallib` 和 shader 缓存预热方法。运行时由 AdrenoMetal 接 metal2vulkan 和 Turnip，经 AIR → SPIR-V → NIR → IR3。当前完整构建已有运行时链，独立 AirNir kit 作为离线研究项。 |

板级主参考是 ArmadaOS-Freedom 的 TB322FC/SM8750 及其引用的 Linux `6f97250e`，其次是 Linux mainline。已核的 Elden DTS 继承通用 `qcom/sm8750/gen80000_zap.mbn`，未在该板 DTS 确认 `qcom/sm8750/lenovo/elden/gen80000_zap.mbn`。若已有原厂固件能确认板级签名资产，再记录实际提取路径、SHA 和来源到固件锁及适用的 `proprietary-files.txt`。真实 PAS/ZAP 认证列入实机验收。当前 UEFI GOP 是既有基础显示来源，sheng UEFI 和设备仓用于参考接线与打包结构。工作台 `research/` 存独立 Git 克隆，先按复用审查找供体和提交，再核对源码。 [ArmadaOS 板级入口](https://github.com/ArmadaOS-Freedom/armadaos-freedom/blob/main/devices/lenovo-tb322fc/README.md) · [Linux DTS](https://github.com/h0cheung/tb322fc-linux-kernel/blob/6f97250ead155fc3b980856323e99a4623dae73d/arch/arm64/boot/dts/qcom/sm8750-lenovo-elden.dts) · [板级证据](evidence/gpu-board-inputs-20261115.json) · [全仓复用审查](https://github.com/zenkernelsam/Patch/blob/2c386e2739136b46ed6c494f24d8a5356c2ce386/Y700-Elden/lab/reference-reviews/20261122-unixnu-reuse/README.md)

## 离线优化顺序

1. **提交计时与缓存维护。** 对照 A740 的 `93adbc1` 分离诊断开销并记录长尾任务，再审 `5eb625f` 可跳过维护的映射类别。其 macOS run61 报告约 `351 µs / 579 µs` 用在缓存维护，为供体数据。先记录 Elden BO/map/unmap、submission、fence、缓存维护计数及时间，沿用 A830 已验证的缓存和 DMA 同步。确认映射所有权及 cache alias 后再缩减范围。后续看 `4799715` 的锁外清零与 `88241fa`、`7e8d248` 的故障隔离和 CP 快照。 [提交计时](https://github.com/UniXNU/Adreno740/commit/93adbc1) · [缓存维护](https://github.com/UniXNU/Adreno740/commit/5eb625f) · [故障隔离](https://github.com/UniXNU/Adreno740/commit/88241fa)
2. **并行评估整组 Metal 依赖。** Elden 已通过的是 steelbrain `development@624bab` 加 26 个 A830 前端补丁，桥接构建器来自 UniXNU `95c6788`。今天观察到官方 AdrenoMetal `cc0a4e8`，其中 `9d38fe6` 有分阶段纹理行拷贝，`6e80735` 集成 R2507。官方另一组构建清单指定 `46e7cc96` 与 Mesa `c823a83e`，观察到的 development tip 为 `a3786dd8`。用 `tools/review_gpu_update.py` 输出候选差异，再把 A830 前端、Rust/Cargo、patch、Turnip 功能集和 Mesa 一起在独立输出复建。对照像素、shader 编译/缓存、纹理上传和拒绝面。复制缓冲区近期有 macOS 专用修补，采纳前核对目标路径。 [AdrenoMetal](https://github.com/UniXNU/AdrenoMetal/tree/cc0a4e8e57d20f4a4698753c2530875f2b10c0ca) · [官方清单](https://github.com/UniXNU/UniXNU/blob/8542980d02b4d2cc5c32ee3542de1760b2562aac/scripts/data/gpu-metal-vulkan-inputs.json) · [development](https://github.com/steelbrain/metal2vulkan/tree/a3786dd88b6e55f16cfffbae48d14ad6c95d1e25)
3. **拆开 GPU 执行和显示等待。** 用 `b0d812d` presenter 统计 CPU fallback、拷贝和格式，用同一提交、fence 和帧记录对齐 CPU 编码、KMD 提交、GPU 执行、vsync 和显示 retire。分别测静态桌面、触控动画和应用打开的帧时间，记录中位数、P95、掉帧和冷／暖缓存。确认瓶颈后再推进 DPU 直扫、其他像素格式和 shader 预热。预热包以 OS、translator build 和 Turnip `LC_UUID` 定位。 [GopFramebuffer](https://github.com/UniXNU/GopFramebuffer/tree/b0d812d578efa2acb005b66f73168891719af09a) · [官方帧诊断](https://unixnu.com/porting/gpu-diagnostics)
4. **真机稳定出图后推进 GMEM、抢占和省电。** 当前先用 `sysmem,nocb` 建立 A830 基线。GMEM 先验颜色、mutable swap、row padding 及 load/store。抢占先核 Gen8 上下文和回收，调频、IFPC/ACD 及 suspend/resume 则依赖本机电源、温控和共享 RPMh owner 实测。每轮改变一项并保留可回退基线。 [当前合并约束](GPU-MERGE.md)

## Devin 首轮与主线合并

使用工作台 `_relay/dispatch.py` 向 `Devin-worker-agent-Y700` 派发一轮明确的离线任务。第一轮可对照 `93adbc1` 给 A830 提交路径补计时与慢任务记录，并列出 `5eb625f` 前置缓存资格。任务文件给出目标工作树、基线 SHA、参考提交、CiscoMac 构建路径、测试和交付格式。用当前 worker PID 派发，每三分钟检查 `handover-to-codex.md`，收尾标记为 `(handover to codex now)`。接手 agent 审核差异、命令退出码、源/产物 SHA 后在 GPU 分支提交推送。将可复用的证据留在 `lab/records/`，方便其他 agent 接手。 [派发流程](https://github.com/zenkernelsam/UniXNU_elden/blob/offline-peripheral-fixes/docs/peripherals/REAL-HARDWARE-RUNBOOK.md#handoff-and-worker-workflow)

Wi-Fi 稳定交付后，由主线使用当时 `main` 建测试集成分支。先沿外设验收矩阵测试其他离线外设，GPU 独立成轮。GPU 首次实机使用已验收离线源码重建，保留主线 Wi-Fi/UFS/QRTR 和共用 SMMU、RPMh、显示、recovery owner。按 `GPU-MERGE.md` 重做新内核和用户态配套交付，核对 floor `1738713600@0`、parent `3`、回程和现有主线启动配置。依次验证 GMU/HFI/ZAP、基本 BO/map/submit/fence 与可核正确像素、Metal/IOSurface 客户端及真实显示、GPU drain 在 UFS 停机前完成、回主线恢复流程。基线通过后再用相同场景 A/B 验证各优化。每批功能通过就尽早合入审核过的源码提交，保留分支顺序、依赖及实机证据。 [GPU 合并](GPU-MERGE.md) · [外设验收](https://github.com/zenkernelsam/UniXNU_elden/blob/offline-peripheral-fixes/docs/peripherals/GOAL-ACCEPTANCE-MATRIX.md)
