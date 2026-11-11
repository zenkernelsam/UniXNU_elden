# Settings 45秒等待对照与后续画面准备

2026-11-11。完整Goal继续active；只推进Settings交互/显示观察。平板已恢复Android，所有任务已结束。

## 同一v87的45秒对照

本轮使用已有v87像`e56f2493f78452bc26a5eff5f49f844f5016640139f6e73ffec23f1fdd1f78be`，核`78e6b46d15a541539d39631e6f3fcfda327c43ba741e8f82f56bb1212980c1a9`，session `19fa368d1597c72f`。fresh4405mV/yes，boot_a全SHA核对，floor1738713600@0不变。

`edl_work/live-ui-settled-v87-20261111`自动识别CRC锁屏、上滑，匹配Settings齿轮后等待45秒，才发一次100ms短按。软件down→up100681µs，UIKit两组99508µs，原event tick差按19.2MHz为100689.79µs。四条同PID27/TID322，序号/总数完整、overflow/revoked/emit-fail0，filter-restored1。

整段drop16，观察窗口前后计数稳定，窗口内四条记录完整；不称整段无丢字。原始SHA `87894af96ba711e89735d98fb17698704fcccb3eb087640daea6d114e59617d6`。判读`ui-dispatch-20261111/delivery-delay-v87-settled45.json`。

早期after-tap5/6仍是桌面，Settings图标外观变化，但没有设置内容。因此45秒等待并未在这两张图里证明打开成功。**不能据此说Settings后来从未打开**：当前只取最早两次完成显示的帧；随后frame14/15全屏非黑像素，均accepted/retired成功，却没有相应CRC截图。不要再用额外Home动作改变页面后反推原结果。

下一步先补这些后续完成帧，再决定是否需要修命中/应用启动或显示等待。不再为同一事实重跑CPU准入、换等待时间或关闭服务。

## 四帧观察组件已原生构建，尚未链接/刷入

canonical `scripts/driver-patches/EldenDisplayGlue.cpp`的`elden_preview_after_input`只把计数2改为4；总cap10不变。它仍只在已有显示完成回调中读取原映射，没有新输入、并行读映射、寄存器写或缓存属性变化。增加的是后续完成帧，**不承诺固定20秒采样或系统不再绘制时也能得到新画面**。

Mac和本机`research/preview-four-20261111/`已有源、对象、archive和审核。`edl_work/build_preview_four_mac.py`先重编原源，所有非调试section与冻结对象一致；再编计数4，只有TEXT变化。`audit_preview_four.py`核实际1699条指令、无FP，仅offset8的`mov w9,#2`→`mov w9,#4`改变，其它指令逐字一致。

- 新源SHA `0a50a29a9291e094047c0d1ceae314af29cf196f582563625058255e281076ac`。
- 新archive SHA `21545dfbefd29149401844fc600c269b86cf05bdeb6680e7f13eefaefa9d8a7a`。
- 旧archive SHA `911b9544e7c51fb930462d53b0901412f1f79eb5e14beebbba2acf63746bf32a`。
- 本轮仅对象与archive，**没有v88整核/boot像/签名/实机结果**。

下一步把该archive链接进冻结v87体系，准确更新该Glue源/对象/归档记录，其余CPU、回程、HID、power35、backlight13保持。新整件审核之后再跑一次真实Settings操作，重点看追加的后续CRC帧。不得把它塞进旧像后继续沿用旧整核SHA/资格。

## 显示缓存映射：仅离线证据

原TB322FC Linux固定`bf96c3d4048973298ede78487177c6f1b4f6bc0f`的`sm8750-lenovo-elden.dts`中，chosen framebuffer与splash reserved-memory均为PAfc800000/长度2b00000、1904×3040、stride7616，splash有no-map。

实际CPU8编译记录预处理证明`HAS_UCNORMAL_MEM`未定义，因此`video_console.c`选择`VM_WIMG_IO`；当前NativeLegacyDisplay使用`kIOMapInhibitCache`。`PE_create_console`给局部Video副本加物理地址marker，保留PE_current_console真实PA；pe_kprintf沿已初始化console输出。原源与宏记录已存`display-alias-review-20261111/`。

上游NativeMobileDisplay的`e6dc9eab8f989fb801ffcfc139093e1f0b6e2fb4`增加T733限定write-combine，并明确要求console同属性。这个方法提供下一步审查方向，但本机所有活动别名、页表/分配属性和T733硬件验证范围还未全部核定，**本轮没有改任何映射，不直接设全局HAS_UCNORMAL_MEM**。先确认后续画面，避免把一次页面观察缺口扩成缓存实验。

## 设备恢复

`edl_work/ui-settled-v87-stock-20261111/flash/result.json`确认stock `8b8e72da…`完整SHA恢复。`android-final.json`：root/green/boot_completed1，97%/4421mV/-426mA/30.1°C/Charging，已发熄屏命令、Dozing。新reference=stock，下一写fresh电量。logger、watcher、构建均已终止；没有待用户操作的设备阶段。电气/温控、声音先讨论、GPU最后均保持。
