# v88四帧判读：已出现白色应用页面，IconServices仍在CoreImage GL崩溃

2026-11-11。完整Goal保持active；当前主工单收敛为白页对应的图形服务依赖，不继续调整触控等待时间或重复CPU准入。

## 实机结果及更正

v88真实重链了四帧观察归档，运行段只有一个字节（输入后计数2→4）改变，运行符号不变。Mach-O STAB中的源路径/对象时间不属于运行符号，构建器按真实n_type区分；没有放宽实际符号/代码核对。199来源、其余19个显示成员及CPU/电源归档相同，原30模块/52HID、floor审核通过。

- 核：`df3b6bc9a2bdce7e443799159f3459ca9aac3a6d4b03c905d9cec1d8073c5629`。
- 像：`6c11e6a3d45b0f139af6291dc6d2c3290072251c8241cf19fad58683df5f4b22`。
- session：`ec2c8bc76ec729ae`；boot nonce：`6702658d7d3a7962`。
- fresh4434mV/yes，boot_a全SHA通过；footer1738713600@0不变。
- 日志：`edl_work/live-preview-four-v88-20261111/usb/20261111-105941.bin`，SHA `cb11b625dc4a445f381be3f1dfaeffcb5247f4e0ce813325a35227c10a1dab5e`，全段drop82，不能称无损。

CRC锁屏/桌面识别后上滑、等待45秒，只点一次Settings。HID101092µs，UIKit两组99573µs，4条和摘要计数完整；观察窗口有其它日志丢失，范围见`ui-dispatch-20261111/delivery-delay-v88.json`。

**after-tap9/10是几乎全白的应用尺寸页面，底部为黑色导航条，已经不同于桌面。** 它没有Settings内容，所以仍不验收Settings；也不能把它称为“还停在桌面”或“回到锁屏”。旧watcher的亮像素规则把白页误标成lockscreen_match=true，未造成第二次上滑（本轮已发过一次）。已用实际CRC锁屏与白页两张图修正这个保守分类，见`icon-gl-retry-20261111/lockscreen-regression.json`；历史trial.json原样保留。

## 确定的故障与尚未确定的调用方

12条完整故障行均为iconservicesagent，PC/FAR0，LR1a091aac4，原cache slide09220000。去slide得到`0x1976faac4`，准确对应已提取21H461的`CI::GLContext::init()+0xdc`，与最早的GL故障相同。原mode0库的loaded/installed在故障前已出现，不能再把问题归因库没加载。

`icon-gl-retry-20261111/v88-faults.json`绑定全部完整记录。日志有丢字，12是完整行数量，不声称总共只崩12次。

原ISCompositor的canUseCoreImage/ForEffects确实调用renderingMode getter，因此没有证据支持“它们直接读ivar使getter替换失效”。该覆盖范围之外仍有CI调用：精确CoreUI字节中，图形变体、形状效果和文本效果均有sharedCIContext调用。`context-callers.json`及逐函数反汇编已归档；**静态调用索引不是实机调用链**，不能选一个嫌疑方法直接返回nil/原图或关闭服务。

原`CIContext initWithOptions:`先尝试Metal，失败后进入internalGLContextWithOptions；`software_renderer`在这条已核分支被取出移除并转成priority_request_low，不是本轮已证明可用的纯CPU后端。还没有证明所有CPU替代方法都不可行，也没有GPU提前移植授权变化。

## 下一发必需证据：有界故障调用栈（对象已编译，尚未整核链接）

新增`drivers/EldenFaultTrace/`：

- `FaultFrames.h`：16层上限、用户地址/16字节对齐/返回地址/单步1MiB/单调链检查，copyin失败立即退出，明确区分截断与错误。
- `IconFaultCapture.c`：使用真实CPU8 C编译记录和内核头，在当前故障线程中比对EL0 saved-state PC/LR，读取FP链；拒绝中断或关中断上下文。没有读取其他进程、写用户内存或修改异常处理结果。
- `IconFaultTrace.inc`：只作用于前4个iconservicesagent PC0故障，最多一条600B原子队列记录，失败不在异常里等待重试。不输出原始栈内容，只输出返回地址。
- `.inc`与C分开编译，避免将C内核私有thread结构头直接注入现有IOKit C++单元。原故障回调ABI保持8个参数。

`prepare_icon_fault_trace.py`在研究副本中为现有USB故障回调加调用，canonical USB文件没有默认接入。`build_icon_fault_trace_mac.py`生成真实archive，host ASan/UBSan过；实际C捕获对象113条指令、UFS对象19222条，无FP/SIMD，包含真实copyin和调用入口。新archive SHA `941f6e684601a7c1aeca6769beaa789c89eec9ccd92bdd4bdd19712aa78b5686`。原源/zip/对象/命令/汇编/结果在双方`research/icon-fault-trace-20261111/`。

**尚无v89整核、签名或实机调用栈资格。** 下一步把这份归档与已验v88四帧体系正确链接，绑定新增C对象和源，验证异常回调可达与原模块/回程，获得一次真实故障链后决定哪条CoreUI/IconServices CPU路径需要修复。不要再造单纯重复PC0的镜像，也不直接提前GPU或修改显示缓存。

## 当前设备与回程

v88已按PC RPC完成ACK/sync/SDAM回到实际HA2A1PT2 fastboot。随后stock `8b8e72da…` 全SHA恢复，`edl_work/preview-four-v88-stock-20261111`确认Android/root/green/boot_completed1，99%/4455mV/-426mA/28.3°C/Charging，已发熄屏命令、Dozing。所有logger/watcher/编译均已结束；下一reference=stock/fresh。

完整充电、真实手指、持久Data、Wi-Fi/蓝牙等范围未缩减。声音先讨论衰减/保护、GPU最后及floor/电气/热控要求保持。
