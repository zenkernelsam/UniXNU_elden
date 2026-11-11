# A540 CP 取指解锁配方（sagit 实机版 · 2026-10-29）

> 来源：`research/ios_sagit`（Mi 6，**同一颗 MSM8998+A540**），
> iPadOS 18.7.10 / XNU 11417 真机验证——round71 已完成 22 笔原生
> 合成 GPU 作业、round73 DPU 零拷贝扫描 2,073,600 像素全对。
> 我们 v222-v237 卡在 `consumed=0 RPTR=0 trust=1`；他们的代码里有活答案。
> **lilac 移植优先级：最高。**

## 与我们的差异（五个关键点）

### 1. PAS-13 不是一次 SCM，是完整握手（QcomGPUSecure.cpp + SecureFirmwareProtocol.h）
我们 v236 只发了裸 `SET_REMOTE_STATE(0,13)`。他们的协议是
**Acquire → Resume → Validate** 三步带 token 的状态机：
- `QcomSCM` IOService 暴露 `SecureFirmwareRequest` 平台函数
  （64 字节 ABI，peripheral=13 与我们一致）
- Acquire 拿 token → Resume（PAS-13 warm handoff）→
  **Validate 必须回报 `state==Warm` 且 `capabilities&RemoteState`**
  才算数——我们缺的正是"握手确认"环节
- 返回带 `Quarantined` flag / `state>Faulted` 判故障；
  非 Warm 不往下走
- 文件：`drivers/gpu/QcomGPU/QcomGPUSecure.cpp`（85 行）、
  `drivers/include/SecureFirmwareProtocol.h`

### 2. 安全模式由 CP 自己在命令流里切（AdrenoSubmit.h start()）
- `authenticatedZAP` 时**在 ring 里发** `CP_SET_SECURE_MODE(non-secure)`
  （`packet7(0x66,1); put(0)`）+ `packet7(0x26,0)` 等待包——
  **trust 由 CP 取指执行时自己清掉**，不是 CPU 写
- 注释原话："A secure CP needs a confirmed PAS-13 operation and a
  firmware-handled CP_SET_SECURE_MODE transition. **Never write the
  trust register.**"——我们 v234 直接写 SECVID_TRUST 立即复位的
  坑他们早就标了红线
- **trust=1 不是死路**：`trust>1` 才拒绝，`trust==1 &&
  authenticatedZAP` 放行——我们读到的 trust=1 状态其实可解

### 3. 前置条件检查比我们的严格（GPU::Result start 的 13 项门禁）
逐项全过才允许 start：
- `read64(MEBase)==pm4 && read64(PFPBase)==pfp`（微码基址 latch）
- `GPMUReady==0xbabeface && GPMUError==0`（我们的 BABEFACE 同款）
- `ContextSwitch==0`、`(MEControl&Halt)==Halt`(0x303)、
  **`(VBIFHalted&15)==15`**（VBIF 四位全 halt——我们只查了部分）
- `haltedPipelineIdle(Status)`、`Status3&BIT24==0`、
  `InterruptStatus&ErrorIRQs==0`、`CPInterruptStatus&0x3f==0`
- `RBControl==RingControl && read64(RBBase)==ringVA`
- **`RBRead==RBWrite`**（环空游标校验）

### 4. 不重编程 RB_CNTL（保留固件的 PFP fetch 状态）
注释："Reprogramming RB_CNTL resets RPTR **without retiring the
retained PFP fetch state**. Reuse the already owned ring after
ME_INIT/GPMU; preserve its retired prefix and append at the
verified empty cursor."——**我们每次都在 zap 后重排 RB_CNTL**，
可能正是把固件已建立的 PFP 预取状态打掉了。
retained-firmware 路径：不写冷启动寄存器表、不走 SCM bypass。

### 5. 释放顺序：先 VBIF 后 ME
`r.write(VBIFHalt,0); r.write(MEControl,0)`——VBIF 释放先于 ME
释放（与我们顺序/时序可能不同），然后 100ms 内轮询首个
CP 生成的时间戳确认活。

## 启动序列摘要（照抄顺序）
```
halt 确认(ME=0x303, VBIF=15) → InterruptMask=0 → *fence=0 →
RBWrite=read → InterruptClear(OwnedIRQs) →
[authenticatedZAP: ring内 CP_SET_SECURE_MODE(0)+wait] →
timestamp 包 → publish RBWrite → VBIFHalt=0 → MEControl=0 →
轮询首 timestamp
```

## 相关文件（全在本仓库）
- `drivers/gpu/QcomGPU/AdrenoSubmit.h` — start() 全序（493 行）
- `drivers/gpu/QcomGPU/AdrenoStartup.h` — zap/初始化（530 行）
- `drivers/gpu/QcomGPU/QcomGPUHardware.cpp` — 1581 行硬件主体
- `drivers/gpu/QcomGPU/QcomGPUSecure.cpp` — SCM 握手（85 行）
- `drivers/include/SecureFirmwareProtocol.h` — 64B ABI 定义
- `drivers/gpu/QcomGPU/QcomGPUMapper.cpp` + `AdrenoMMU.h` —
  SMMU/IOMapper（显示 MMU 上下文+VBIF halt 建立）
- `docs/gpu/hardware-bringup.md` — 逐轮实机记录（round 系列）
- `docs/gpu/hardware-submission.md` — PM4/Compute/纹理提交细节
- `drivers/gpu/QcomGPU/AdrenoPipeline.h` — Render/Compute
  AOT/JIT（1227 行，IR3 指令、纹理状态、sampler）

## 额外收获（同一仓库）
- **XNU 11417 源构建**（Darwin 24.6，Linux x86_64 主机，无 Mac）
- **DVFS 全套**：QcomOSM 双簇 LUT、SAW AVS、CPRh——4+8 核真机
  （300MHz–2.36GHz，22/30 档）——"CPU 调度"问题的完整答案
- **kernelcache_patcher**：分析 iPad7c 原生 kernelcache 228 个
  kext，虚表 ABI 比对——跑原版 Apple kext 的工具链
- **DPU 零拷贝已通**：iOSurface→显示 MMU→DPU IOVA 直扫，
  与 nativemobiledisplay 同源思路但已在 MSM8998 上验证
- `powerctl` 等一套 ctl 用户态工具（带原生 entitlement 过沙箱）

## 建议行动（给师弟主线）
1. 先读 `AdrenoSubmit.h` start() 13 项门禁——对照我们 v225 的
   `CP cfg` 日志逐项补差（VBIF 15 位、ContextSwitch、ErrorIRQs
   大概率是我们没查的项）
2. 加 `authenticatedZAP` 路径：ring 内 CP_SET_SECURE_MODE + wait 包
   ——不动 trust 寄存器（v234 已证写即复位）
3. PAS-13 握手改三步：Acquire→Resume→Validate(Warm+RemoteState)
4. 停止 zap 后重排 RB_CNTL，复用固件环游标
5. 释放顺序 VBIF→ME

## 2026-10-30 增补：配方升级为"完整架构答案"

又细读了 sagit 的 `boot/bridge/gpu_handoff.cpp` + `drivers/security/QcomSCM/SCMPAS.h`，
结论修正：**他们把 GPU 安全引导全部放在 UEFI 桥阶段**（XNU 之前），内核只通过
40B `qcom,gpu-handoff` DT 接管已活硬件。我们在内核 bsd_init 期做 PAS/zap，
阶段可能本身就是错的——ABL 刚交权的上下文里 PAS 才认。

### SCM 延续 ABI（v324 hyp_assign 卡死的正解，两处独立印证）
- sagit `Transport::invoke`：SMC 返回 x0=1 = 被中断 → 重发
  `x0=1` + 原 x1..x5/x7 + **x6=monitor 返回的 cookie**，最多 1023 次；
  x0=-12 busy → sleep30ms 重试 ≤20；**WAITQ_SLEEP 不是 MSM8998 ABI，
  不能当新事务重试也不能当完成**（注释原文）。
- Bergamota `9c43ca4`："GPU zap loader resumes an interrupted SCM call
  like Linux (original x1-x5, x6 token, x7 = 0)"——他们也在 GPU zap
  加载里修同一个坑。**我们 v324 把 args6 塞 x6 正是踩了这个**：
  x6 是 continuation/extra_arg_buf 指针不是普通参数。

### PAS-13 精确调用编码（`SCMPAS.h`，Linux v6.12 qcom_scm ABI）
- sip = `0x02000000 | (svc<<8) | cmd`；peripheral=13=GPU
- 探测：`svc6/cmd1` 先试 64bit 再回退 SMC32（probe 不改外设态）
- `IS_SUPPORTED`：svc2/cmd7, arg=13
- `INIT_IMAGE`：svc2/cmd1, args(13, metadata_addr)
- `MEM_SETUP`：svc2/cmd2, args(13, addr, bytes)——**固定地址 MDT 可跳过
  （程序头自带目的地址，Linux 加载器故意 skip cmd2）**
- `AUTH_AND_RESET`：svc2/cmd5；`SHUTDOWN`：svc2/cmd6
- **`resume()`= svc1/cmd10, args(0,13) → Warm**——对应内核侧
  `SecureFirmwareRequest` 的 Resume
- 状态机 Empty→Metadata→Region→Authenticated→Warm/Faulted，
  相位错直接 Invalid 不发 SMC——比我们的裸发严谨

### 桥架构（`gpu_handoff.cpp`，真机已验证冷启动+PAS+DMA提交）
UEFI 阶段完成：SMCCC probe → PAS support-query → init(metadata)→
setup(image)→authenticate(ZAP) → CX/GX+私有GPU时钟+RPM通道 →
RetainedPageTables（**不建表，校验固件保留表契约**）→ 桥内
CP 提交自检（Fill32/Copy32/Barrier+timestamp 核对）→ 40B DT 交接内核。
内核 `AdrenoSubmit.h`：接管 32KiB ring、禁 RPTR shadow、
CP_INDIRECT_BUFFER_PFE + CACHE_FLUSH_TS|IRQ 门铃、真实 IRQ +
10ms 定时兜底、500ms 无完成判故障——**先等 ring 消费+idle 才开放能力**。

### 对 lilac 的行动修订
1. shim/loader 阶段做 PAS+ZAP（不等到内核）——我们有 XFL shim 槽位
2. 所有 SCM 调用包一层 Transport 延续逻辑（x0=1/x6 cookie）——
   这也修 hyp_assign 卡死
3. 页表策略改"校验固件保留表"而非自建（我们 v255 证 SMMU 无 fault
   但 CP 不取指——很可能就是自建表被 TZ 拒认）
4. 桥内先跑 CP 提交自检再交接——把 GPU 问题在 XNU 之前就关掉

## 2026-10-30 增补 2：Sony kgsl 答案对照（sony-kernel-4.4 实锤）

师弟报告"ME=0x404 / RPTR=0 / ucode 全F / SMMU 无故障"后，
在 Sony 下游内核找到逐条答案：

### Sony kgsl 的 GPU 启动序列（adreno_a5xx.c）
1. GPU power 上电（rail+时钟）
2. **PM4/PFP 微码不进 AHB**——`A5XX_CP_PM4_INSTR_BASE_LO/HI`
   与 `A5XX_CP_PFP_INSTR_BASE_LO/HI`（L2215-2229）写
   `pm4_fw->memdesc.gpuaddr` / `pfp_fw->memdesc.gpuaddr`
   = **GPUVA**，CP 经 gfx3d_user SMMU 从 DRAM 自取微码。
   **我们 v316 的 AHB CP_ME_RAM 路线全 F 不是错误信号，
   是这颗芯片本来就不走那条路**——该读数永远全 F，别再纠结。
3. `if (!device->mmu.secured) return 0;`（L2233-2236）——
   zap 只在 SMMU 有 **gfx3d_secure 上下文**时才加载
4. **SCM resume 调用就在 Sony 源码里**（L2243-2252）：
   `scm_call2_atomic(SCM_SIP_FNID(SCM_SVC_BOOT, 0xA), desc)`，
   `args[0]=0, args[1]=13, arginfo=SCM_ARGS(2)`，
   注释原文"write the zap shader base address into the
   appropriate register"——与 sagit `resume()` svc1/cmd10
   (0,13) **是同一个调用**，Sony 官方顺序就是先设指令基址
   → resume(0,13) → `subsystem_get(zap_name)` PIL 加载
   → CP ME_INIT。
5. RBBM_SECVID_TSB_TRUSTED_BASE=KGSL_IOMMU_SECURE_BASE、
   TSB_CONTROL=0（adreno.c L1799+）

### GPUVA 地图（kgsl_iommu.h L26-42，与 v239 数字全对上）
- `GLOBAL_MEM_BASE64 = 0xFC000000`，A5XX global 区 8MB
  → [0xFC000000,0xFC800000)（ring/ucode/memstore 都在这）
- `SECURE_SIZE = 256MB` → **secure 窗 [0xEC000000,0xFC000000)**
  ——v239 写的 TSB base 0xec000000 地址本身是对的
- DT iommu 上下文：`gfx3d_user`（非安全 CB）+
  `gfx3d_secure`/`gfx3d_secure_alt`（secure CB，
  `DOMAIN_ATTR_SECURE_VMID=VMID_CP_PIXEL`）

### ME=0x404 的统一解释（三假设按概率排）
**H1 zap 已跑过、GPU 已在 secure 态**：内核 resume(0,13)=0/0
= TZ 账本里 GPU 已 warm/owned——很可能某次 auth 真成功了。
此时 CP 取指走 **gfx3d_secure CB**（secure 窗 0xEC000000+），
该 CB 页表须 hyp_assign 分给 VMID_CP_PIXEL（v324 ABI 修好后）。
我们所有 0xFC000000 global-VA 尝试在非安全 CB 上做，
secure CB 无映射 → CP 取不到指，**故障打在 TZ 侧的安全 CB 上，
我们 CB0 的 FSR 当然看不到**。修法：secure CB 建表 +
MEBase/ring 放 secure 窗 + TSB（v239 半成品）→ 起指后
ring 内 SET_SECURE_MODE(0) 落回非安全。
**H2 取指流的 CB 不是我们配的那个**：GPU fetch 流路由到
别的 CB（SMR/S2CR 没全核）或 CB0 被 TZ 覆写 → 全部 CB 的
FSR + 完整 SMR/S2CR dump 一次查清。
**H3 zap 从没执行**：auth "到位"未核返回值，或 blob 不是
lilac 自己的 a540_zap.mbn（Sony 熔断签名，linux-firmware/
小米件必挂）——先查 shim 日志里 init/setup/auth 真实返回值。

### kexec 暖接管（Q3 答案）——可行，但先做更便宜的取证
`fastboot boot` Android → kgsl 把 GPU 起起来 → ramoops/debugfs
dump：全套 SMR/S2CR/各 CB 寄存器、TSB、trust、ME_CNTL、
ring/ucode 的 GPUVA。一份 dump = Sony TZ 的完整答案对照表，
之后照抄或 warm-takeover（sagit RetainedPageTables 就是为
这个设计的）。比继续盲试快一个量级。
