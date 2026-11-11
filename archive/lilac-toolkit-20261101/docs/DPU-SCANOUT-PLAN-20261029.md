# lilac 零拷贝 DPU scanout 移植计划（2026-10-29）

目标：把当前 SweetFramebuffer 的 CPU 拷贝/合成路径，替换为
**IOSurface 物理页 → 显示 SMMU CB → SSPP/VIG0 → CTL0 flush** 的零拷贝
scanout。参考成品：`research/unixnu-nativemobiledisplay`（SM7325 实机）。

## 参考件分工
- `NativeDpuSurface.{h,cpp}`：IOSurface 物理页 → 显示 SMMU 映射。
- `NativeDpuScanout.{h,cpp}`：SSPP 配置 + CTL0 flush。
- `NativeDpuInterrupt.{h,cpp}`：done/TE 中断与 vsync 注入
  （GOP 无 vblank 时由定时器 work-loop 发，`sheng-vsync=<Hz>`）。
- `NativeMobileDisplay.cpp`：IOMobileFramebuffer 发布层（接我们的 ABI）。
- `NativeLegacyDisplay.cpp`：旧拷贝路径 fallback（保留开关）。

## lilac 落地步骤
1. **寄存器表（SDE v1）**
   - 表源：`research/sony-kernel-4.4` 的 mdss/sde 头文件与
     `msm8998-*.dtsi`（mdss、dsi、iommu 节点）。
   - 产出：lilac 专用的 SSPP/VIG0/CTL/DSI 寄存器偏移与位域表。
2. **显示 SMMU**
   - 仿 GPU CB0：建 CB（mdss iommu 节点）、4KB 页表映射 IOSurface
     物理页；先做 **passthrough CB 直打 PA** 的最小路径（picasso 的
     USB 有先例），绕开 hyp_secure_alloc 风险。
   - 失败判据：SMMU 写被 TZ 复位/无 fetch → 立即回退到拷贝路径。
3. **SSPP/VIG0**
   - 配 src/dst rect（调试分辨率 ~600×300，lilac cmd 面板缩小源面
     再由 pipe 放大）、format/stride、scaler 表（来自 sde_hw_sspp）。
4. **命令模式 DSI**
   - CTL kick + done/TE 中断替代连续 vblank；`sheng-vsync=1` 先锁 1fps。
5. **验证与仪表**
   - 先用 CPU 填色的 surface 验证零拷贝上屏（无需 GPU）；
   - 记录 composition→fence→DPU submit→scanout 的 us 级链路
     （参考 512 帧延迟仪表）。

## 风险与回退
- mdss iommu 可能吃 `hyp_secure_alloc`（v237 墙）→ 先用 passthrough CB；
  再不行就保留现有拷贝路径，本条线暂停，不阻塞其它工作。
- 保留 boot-arg 开关（建议 `sweetdpu=0/1`），默认走旧路径，逐级验证。

## 顺序
CPU 调度（ArmCpu）→ sheng-vsync=1 → DPU 零拷贝 scanout → 再上 GPU。
