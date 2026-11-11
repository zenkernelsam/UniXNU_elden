# lilac GPU handoff 生产者设计（shim 阶段，草案）

> 依据：sagit `docs/gpu/hardware-submission.md` 的 40B `qcom,gpu-handoff`
> 契约 + vendor kgsl `a5xx_rb_start`。目标：由 shim（或未来 EFI 等价物）
> 完成 GPU 初始化并把 retained 状态交给 XNU；XNU 只校验与接管。

## 40B handoff（little-endian）
| off | 字段 | 值 |
|---|---|---|
| 0 | u32 version | 1 |
| 4 | u32 flags | 127（7 项承诺全满足） |
| 8 | u32 chip | 0x05040001 |
| 12 | u32 firmwareMode | 1（CP 固件已初始化并保留） |
| 16 | u64 pm4 | == CP_ME_INSTR_BASE |
| 24 | u64 pfp | == CP_PFP_INSTR_BASE |
| 32 | u64 reserved | 0 |

flags：clocks pinned / power pinned / NoC pinned / exclusive mapper /
firmware initialized / **non-secure** / firmware quiesced。

## shim 必须完成（按序）
1. 时钟/电源/带宽：GPUCC PLL、GX/CX GDSC、NoC vote；保持 pinned。
2. SMMU bank0：TTBR0/TCR/MAIR/CBAR/SCTLR + SMR/S2CR（SID0）；
   页表在普通 RAM，TLB 同步；保留 IOVA 窗口不与其他 DMA 重叠。
3. 认证 ZAP：PAS init_image/mem_setup/auth_and_reset（现有 0/0 已通）。
4. CP：INSTR_BASE=pm4/pfp（GPU VA），ME_INIT，`0x66` 切换；
   **trust 必须为 0**（vendor 由 ring 切换；不得直写寄存器）。
5. GPMU：经 CP ring 的 AGC 协议（不是 host IRAM 直写），ready=0xBABEFACE。
6. 停旧提交者：halt PFP/ME + VBIF 15，等 AXI drain，ring idle。
7. 生成 40B handoff 到 ADT `/chosen`（或 Apple DT provider）。

## lilac 已确认/未确认
- 已通：PAS（0/0）、zap auth、GPMU host 载入（但 ring 路径未验证）、
  SMMU 寄存器可写、CP 基址可设。
- 未通：**CP 取指**（ucode RAM 全 F）→ 由 trust=1 与安全域限制。
- 待测（今晚）：不写 TSB（保留 TZ 配置）是否让 CP 取指；
  shim 的 CurrentEL（EL2 才有更多权限）。

## 红线
- 不写 trust 寄存器（直写/TZ io_write 均复位）。
- 不写 SMR/S2CR bypass（曾导致充电界面）。
- 不做 RBBM reset。
- 任何新步骤前先 arm PON fastboot；失败必须 WDT 回 fastboot。

---

## 2026-10-30 源码级补充：sagit 生产者真实序列

`research/ios_sagit/boot/bridge/gpu_handoff.cpp` + `drivers/gpu/QcomGPU/
AdrenoStartup.h` 就是 saga 侧完整的 A540 冷启动/接管生产者。要点：

### reset()（我们**禁用**，仅记录）
前置：`MEControl&0x303==0x303` 且 `VBIFHalted&15==15`。随后
`RBBM_SW_RESET_CMD=1→读→0`，再 halt，并校验 RBRead/RBWrite/
ContextSwitch=0、trust≤1；再 `GPMU_CM3_SYSRESET=1` 清零 GPMU 状态。
—— 即便有完整前置，我们仍按红线不执行。

### start()（我们已基本照抄）
1. 校验 CP 已 halt、VBIF drain、ring 空。
2. `configure()`：~150 个 a5xx 寄存器（含 SECVID TSB 全零、
   CP_PROTECT、ADDR_MODE=1、HGCG 表）——**我们已有完整表**。
3. ME/PFP/RB 基址（64 位成对写）、ContextSwitch=0、
   `RBControl=12|(2<<8)|(1<<27)`（32KB ring；我们 16KB ring 用 11）。
4. 先 `VBIFHalt=0; MEControl=0` 释放，再下发 **ME_INIT 单独一包**并踢
   WPTR，等 RPTR==cursor 且 Status/CPInterrupt 干净才算 CP 活。

### gpmu()（关键差异）
ME_INIT 退役后才经 **CP ring** 发 AGC：`0x5f(0)` 关保护 → `0x3f`
从 GPU VA 执行 AGC 命令流（IRAM 0x8800 分块、odd parity）→
`0x5f(1)` 恢复；然后 `GPMU_CM3_SYSRESET=0`，等 `GPMUReady==
0xbabeface`。我们用 host 直写 AGC（`sweet_gpmu_host_boot`）已能让
ready=0xBABEFACE，但 vendor 路径是 CP 执行。

### 安全切换（我们此前顺序不同）
在 zap PAS 认证**之后**再开第二个 ring 周期：
`0x66(0)` + `0x26` + `0x46 CACHE_FLUSH_TS|IRQ`（写 fence=1），
释放 CP，**成功判据 = fence==1 && TrustControl==0 && ring 排空**。
即：ME_INIT/GPMU 先行，secure-mode 是给已运行的固件的一次事务；
我们此前把 0x66 放在 ME_INIT 前面（v340 仍如此）。

### lilac 落点
- v340：现有顺序 + SCM pre-resume + SECVID dump + shim EL。
- **v341（已构建待发）**：`sweetgpuringsplit=1` —— ring 拆两周期：
  A) 仅 ME_INIT → 等 RPTR 前进（CP 活的首要证据）；
  B) 若 A 成功：halt → 追加 0x66/0x26 + MEM_WRITE + 0x46 fence，
     释放后判据 fence==1 && trust==0。A 失败则明确打印并跳过 B。
- 日志关键字：`CP split A ... mi=`、`CP release`、`CP consumed=...
  fence=... CP LIVE/no write`。

### SMMU CB0 与 vendor 的差异（v342 素材，未改）
`gpu_smmu.h::initialize` 的 vendor 写法 vs 我们（sweet_gpu.c 的 CB0 块）：
- TCR：vendor `(1<<23)|(2<<12)|(64-input)`，input 按其 IDR 取 40；
  我们固定 `28|(1<<23)`（等价 36-bit）且表按 36-bit 构建。**自洽，
  所以 ATOS 挂起不应归因于此**；若 v342 要对齐，必须同时改页表层级。
- vendor 还写全局 `SCR0 |= 0x1c36`（先屏蔽 fault 相关位）、`0x1800=1`、
  `0x1000=0x1f300`、`GR1_CBAR=1<<16` 与 `CBA2R=1`、`CBn_TTBCR2=...`
  ——我们只写 SMR/S2CR/GR1/CB0，没有动词全局 SCR/SACR。
  这些属 TZ 遗留全局寄存器，**默认不动**；仅作为 v342 有人值守候选。
- vendor 先 `TLBIALL` 再使能 CB，并在每步 readback 校验；我们缺 TLBI
  全清与 readback 门禁（v342 可补 TLBI 这一安全项）。

## 晚间判定树（20:00 后，有人值守）
按 v340 →（可选 v338 对照）→ v341 顺序，日志关键行：
`SHIM EL`、`SECVID cfg/trust/tsb_base/size/cntl`、`CP split A ... mi=`、
`CP consumed/trustoff/fence/ME_CNTL/RBBM/ahb_err`。

| 观察 | 结论 | 下一步 |
|---|---|---|
| v340 `consumed=1` 或 `trust=0` | CP 已取指 | 立即加 CP_MEM_WRITE+fence 验证（v342a），并准备 GPMU ring 路径 |
| v340 consumed=0, trust=1（预期） | 旧顺序不行 | 发 v341 |
| v341 `mi=1`（ME_INIT 被消费） | **转折点**：纯 ME_INIT 即可启动 CP | v342 = v341 的 A 周期 + GPMU ring + 0x66/fence；同时查 SECVID TSB 真值 |
| v341 `mi=0` | CP 根本不取指 | 依序试 v342b：CB0 vendor 对齐（TCR/SCR/SACR/TLBIALL）/ 尝试把 ring 放进 TSB 窗口 / 复核 SCM resume 时序 |
| `SHIM EL=2` | shim 在 EL2 | 战略转 shim 冷启动生产者（可 RBBM reset/全平台接管），XNU 只接管 handoff |
| `SHIM EL=1` | 只能 XNU 路径 | 继续顺序/寄存器假设；RBBM reset 仍禁用 |
| SECVID `tsb_base/size` 非零且窗口不含 0xFC000000 | TZ 限定安全取指窗口 | v342c：把 ring/固件/页表整体搬到 TSB 窗口内再试（不改 trust） |

### v342 预置（`sweetgpunozap=1`）
动机：TZ 已把 trust 置 1（说明安全域已处理 GPU）；我们的 PAS/ZAP
round-trip 可能不是 CP 取指的前提，反而其 `auth_and_reset` 会扰动状态。
v342 = v341 + **跳过 sweet_zap_load**（GPMU host 保留、SCM resume 保留、
split ring 保留）。纯单变量，日志会打印 `zap skipped`。
判定：若 v342 的 `CP split A mi=1` 而 v341 `mi=0`，说明 zap 反而有害；
若都 0，说明与 zap 无关，转向 CB0/TSB/SCM 方向。



## 后续（CP 打通之后）—— Metal/IOGPU 路线备注
- sagit（Mi 6 / SDM835）的 QcomGPU 是 **kext + kernelcache patcher**
  （`ios_sagit/Makefile`：build-drivers → 注入 iOS18 kernelcache，
  配 EFI bridge）；我们当前是 monolithic 内核内置驱动。将来若要
  直接复用其 Metal/IOGPU 栈，需要评估 kernelcache 注入模型或把
  核心（AdrenoSubmit/Pipeline/Mapper）裁剪进 bsd/dev。
- 该工作在 CP 取指成功前没有意义；先按判定树推进 CP。

### CP 打通后的 IOGPU/Metal 移植清单（sagit 文档索引）
1. **IOGPU 桥**：`docs/gpu/native-iogpu.md`——IOSurface 导入、作业私有
   页表、原生准入门控、Mercury 初始化；先在 QEMU/offline 过编译。
2. **Metal 桥**：`docs/gpu/metal.md`——AIR/metallib AOT+JIT、Render/Compute
   编码、纹理/sampler、常量区、IOSurface 纹理与混合状态。
3. **显示**：零拷贝 DPU scanout 我方已验；需把 IOGPU framebuffer 与
   DPU 绑定（sagit `ui-render-handoff.json` 可作契约参考）。
4. **交付模型选择**：sagit = kext 注入 iOS18 kernelcache；我方要么
   kernelcache 注入，要么把核心 C++ 裁剪进 bsd/dev（需评估 XNU 内核
   内 C++ 支持与体积）。
5. **验收**：先 hello triangle（离屏），再 SpringBoard 合成出主页。

### 移植可行性量化（2026-10-30 盘点）
- 规模：`ios_sagit/drivers/gpu/QcomGPU` 共 **9,325 行 C++**（含
  AdrenoSubmit/Startup/MMU/Mapper/Pipeline/Native/Intercept 等）。
- 依赖：libkern C++（OSData/OSArray/OSNumber）、IOKit（IOService/
  IODeviceMemory）、`mach/clock`、以及 Apple 私有 GPU 头
  `GPUNativeMemory.h`、`GPUDiagnostics.h`、`A5xxRegs.h`（sagit 仓内
  自带）。均为内核内可用类型。
- 我方可行路径：不用 kext 注入，而是把 QcomGPU 源**编进 bsd/dev**
  （内核内 C++，与 libkern 同环境），由 sweet_gpu.c 的 CP 成功路径
  手动实例化 `QcomGPU::start(provider)`，平台数据（SMMU 域、firmware
  VA、pm4/pfp）由现有探针直接提供。
- 前置：CP 必须真实执行（本晚 v340-v344 目标）；否则 IOGPU 无硬件可谈。
- 预估：先接 AdrenoSubmit+AdrenoMMU+QcomGPUMapper（≈3k 行）做
  Fill32/timestamp 真机闭环，再上 Native/Pipeline（≈6k 行）。

---

## 2026-10-31 阶段纠偏：ZAP/PAS 搬到 shim（依据师兄 0238c014）

### 实测事实
- 屏幕读回：**SHIM EL = 1（EL1）**，PHYS base=0x9F800000，
  VIRT=0xFFFFFFF006000000，ENTRY=0xFFFFFFF007427488。
  sagit 的桥在 EL2；我们在 EL1，但 SCM 走 SMC conduit 在 EL1 合法
  （Linux qcom_scm 同级），PAS/ZAP 可在 shim 阶段发起。
- v324 卡死正解：SCM 被中断续传 = **x0=1 + x6=cookie（monitor 返回），
  x1-x5/x7 不变**；此前把 args6 放 x6 是错误。

### 新主路径（lilac）
1. **shim 阶段 GPU 生产者**（在 XNU 之前）：
   a. MMIO：GPU 0x05000000/0x40000、SMMU 0x05040000/0x10000、
      GPUCC 0x05065000；shim 已有 MMU，直接 map device。
   b. 时钟/电源：GPUCC PLL + GX/CX GDSC（沿用内核已验证序列）。
   c. SMMU bank0：TTBR/TCR/CBAR/SMR/S2CR（沿用内核已验证块）。
   d. PAS：init_image/mem_setup/auth_and_reset（ZAP blob 从 payload
      读，carveout 0x95200000+0x2000，与内核版相同）。
   e. CP：ME/PFP base + ME_INIT + GPMU（host 或 ring）+ quiesce。
   f. 生成 40B `qcom,gpu-handoff` 写入 ADT /chosen 交给 XNU。
2. XNU 只做校验 + 接管（后续 Metal 栈来自 sagit）。

### 为什么可能解决 CP 不取指
- ABL→shim 的信任上下文尚未被 XNU/BSD 初始化扰动；
  TZ 对 PAS/ZAP 的判定可能只在“平台刚交接”窗口内成立。

### 下一步（shim 代码，待做）
- 新增 `shim/gpu.c`：SCM 封装（含 x6 cookie 续传）、PAS、ZAP 载入、
  GPUCC/SMMU 初始化、屏幕/UART 打印每一步结果。
- 先做 **PAS-only 探针**：只 init_image/mem_setup/auth_and_reset，
  屏幕打印 status；成功即证明“阶段”假设。
