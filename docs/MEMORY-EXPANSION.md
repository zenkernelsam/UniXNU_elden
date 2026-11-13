# Y700四代内存扩展顺序

用户要求尽量用满16GB，本项现在进入准备阶段，不等待所有外设完成。
v109首次实机已将窗口扩大到8GiB，8个CPU保持上线；增加内存不会自动扩大512MiB的RAM Data。
v106修复Settings图像清零异常后，已保留6GiB基线并独立验证扩容启动。

11月14日v109：原加载器成功分配0x8c007c000+0x200000000，XNU实际报告8GiB，
锁屏采样图通过CRC，六次IOKit检查busy0/providers3、八核持续onlineff，同步回程及
stock恢复通过。日志drop17，尚非逐页压力/长时验证。页表池16→24页、汇编清零和
边界常量同步；仅重编三个核心对象，保留v106 RT和v108原版网络组件。
细节及初次生成头覆盖错误见profiles/v109-memory8。扩大到8GiB没有改变512MiB Data。

上游已有可用的额外内存方法，固定参考官方4f3ff5c的window-outside-memory-adoption：
sheng实测接纳458MiB，并用受控读取拒绝固件误标的不可访问区域。该补丁属XNU12377/
macOS profile，Y700的XNU10063需要逐项回移pa_index/pa_valid/页表与KVA边界，不能直接粘贴。
其window-memory-excluded-ranges同时处理范围内空洞，必须与准确EFI地图/本机保留区配套。
下一步先评估8GiB工作负载和更大连续窗口对KVA的挤占，再决定12GiB窗口及剩余分散页，
保留16GB最大利用目标；不把旧照片当当前可分配凭据。

历史原厂MemTotal为15517668KiB，旧EFI照片给出0x8c0000000起13GiB可用大区。
两者均是历史快照，不能据此直接授予未来分配。旧DarwinBoot请求上限8GiB，native4KiB
早期页表只按6GiB预留16页，且XNU还有KVA约束；只修改ADT容量不足以安全扩展。

下一阶段要同时核对当前固件的AllocatePages可用区、原厂DTS/保留区、加载器窗口上限、
XNU bootstrap页表/KVA和DMA地址宽度。优先沿ArmadaOS TB322FC/SM8750的实际布局，
并参考sheng已实现的窗口外可用内存收编；不能把sheng的PA或TB321FU/SM8650范围搬来。
更大连续窗口先单独验证，剩余非连续可用段再逐段纳入，保留原厂固件/DSP/安全区。
OS可用量应扣除真实保留区，目标不是强行显示一个16GB数字。

RAM Data扩容也单独记录：调整实际APFS容器和RAM块设备大小、容量上限及原始inode/UUID
校验，再确认正常设置和日志写入。v105的ENOSPC发生在重复崩溃之后；v106本轮未再出现，
不能将其直接归因6GiB内存不足，也不必继续制造崩溃填满原盘。
