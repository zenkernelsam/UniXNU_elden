# 群友触控链路说明对照 · 2026-11-14

用户提供的 myron 说明强调正确依赖链：触控 IC → Digitizer HID 设备 →
IOHIDEventDriver/IOHIDEventSystem → backboardd → SpringBoard。这与 Elden 当前架构一致。
说明是参考线索，不是本机已验证参数，也不替代准确版本的函数 ABI。

| 群友检查项 | Elden 已有实现与证据 | 本轮决定 |
| --- | --- | --- |
| 加载 IOHIDFamily/EventDriver | 冻结 EldenHidGlue.cpp 的 worker 先加载两者，并核 IOHIDDevice 元类大小208和身份 | 已有，不新增初始化 |
| 独立 Touch Screen Digitizer | NativeHidInput 的 touchDescriptor 使用 Usage Page0x0d/Usage4，10个finger、TipSwitch、Confidence、ID、X/Y、ContactCount，独立于键盘设备 | 已有；不搬FT3683协议 |
| init/attach/start | 以 IOHIDResource 为父，独立touch设备执行 init→attach→start；下游设置 Built-In、DisplayIntegrated、PrimaryUsage | 已有，失败仍拒绝ready |
| 正确坐标 | 本机NT36536按原厂/Gen4 Linux报文解码，原始max19040×30400映射0..32767，HID逻辑max32767 | 保持本机已验映射 |
| 事件派发与边缘手势 | 通过 handleReport 交原IOHIDFamily解析；只给已核本机EventDriver的vtable副本安装edge wrapper，给finger与collection补FromEdgeTip | 已有，原Apple事件派发保留 |
| 抬指/并发 | v100两组件修复成功后提交历史、返回真实IOReturn、发布锁保护完整快照；瞬时失败最多24ms显式等待 | 保留该修复 |
| backboardd阻塞分流 | v105点击设置曾在显示DC ZVA异常后reload；v106修managed RT DRAM属性后实指Settings/General打开，About稍后也出现 | 证据支持先查实际依赖，不能看到无响应就重写触控 |

`mach_continuous_time` 不能直接当成所有 HID 入口的普遍要求：此处使用 IOHIDDevice
的 report 接口，没有自行调用带时间参数的 Digitizer event 构造器。当前实指上滑、
多指、Settings/General已有用户确认，没有本轮证据要求改变时钟域。休眠/唤醒仍需
独立测试；将来直接构造事件时须核对目标 API 的时钟约定。

当前 `research/UniXNU-myron` 固定HEAD b3ce4ca3、可见origin/main包含后续引用；其
NativeHidInput是子模块28f9b463，当前可见树未找到消息中指定的
`package/HidInputProbe.cpp`/`MyronTouch.cpp`。因此不把消息行号冒充已核当前源码，
也不把“myron成功”自动扩大成所有当前分支均实机通过。Elden自己的冻结输入、
源码差分及实指证据足够确认上述链路已经具备。

本轮仅审核、整理清单，没有触控代码、坐标、描述符或候选更换，也没有要求用户
重做验收。双击唤醒、休眠恢复、长期抬指稳定性仍保留在计划中。

依据：`profiles/v91/sources/tree/build/sheng-hid-input/src/NativeHidInput.cpp`、
`profiles/v91/sources/tree/scripts/driver-patches/EldenHidGlue.cpp`、
`drivers/EldenHidDelivery/SourcePatch.py`、`profiles/v100-hid-delivery/physical-result.json`、
`profiles/v106-rt-memory/hardware-result.json` 及现有 INPUT-STATUS.md。
