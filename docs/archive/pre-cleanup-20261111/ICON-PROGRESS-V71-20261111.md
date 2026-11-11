# v71：完整签名链已通过，点击实际101ms，下一步核实用户态加载

完整Goal active，充电500mA限制、Settings正常交互与真实触控均未完成。GPU最后，扬声器先讨论，双口电气保护保持。本轮没有新增电源控制或重试DC失败写路。

## 当前设备与已结束任务

已完整SHA恢复stock boot_a：8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890；恢复edl_work/icon-progress71-stock-20261111/flash/result.json。Android/root/green/boot_completed1、99%/4452mV/-415mA/Charging，已熄屏。构建、监听、watcher和只读Mac镜像挂载均结束。下一发reference=stock，重新读取电量；floor1738713600@footer0/parent3不变。

## 一轮合并的实际结果

v71/power35/backlight13，182冻结来源；核9d26047f1ceb73a34ef07c67a866075f1f2e1ef383694b81f0a0c031a66e783f；像653ce00911230b2940280f78ae800f719bc87c8e5d0bebe0217aa25e09be6883；sessiond0bb6a9bd9ff3605。位置edl_work/basic-bundle-20261108/v71/final/icon-cpu-focused-gestures。签名、完整读回、原30模块与HID207/220/37、52绑定均核；fresh4431mV/yes。

新代码仅11处签名阶段记录、按原路径选中uniqueID采目标线程、保留无upcb线程的内核等待、软件点按计时。移除这些观察后，v70控制源码逐字相同（格式新增空行单独复原）；独立审计edl_work/audit_icon_observer_delta.py及research/icon-launch-constraint-20261111/v71-observer-delta.json。实际9个触控/等待对象、progress78和observe32指令无FP/SIMD。

日志3420165B/drop0，raw SHA bf12d4b8f46a748343daef1a247377a1786a4212d93c85274f51941960f46cc5，USB采集545.734秒；watcher含刷写前等待总581.985秒后正常sync/RPC回程。机器可读证据research/icon-service-v71-20261111/live-assessment.json。

PID109/unique109完成：flags-copied=0x22002301、限定owner例外成功、process-signature-return0、thread-signature-return0、signature-return0，未进入taskgated路线。**包含CS_SIGNED**，原v69混排行尾2200不完整，旧“缺签名标志”推断已否定，未据此改标志。目标进程名已为iconservicesagent，线程进入用户态；栈含dispatch_main+0x78等。不要把没有库marker继续解释成内核签名挂住。

软件上滑到桌面后只点一次Settings。实测down-call89us、down-to-up101115us、up-call101us，排除了此次注入线程被拖成数秒长按。两张CRC后续画面仍为桌面/编辑标记，没有Settings内容。没有第二次点击，也没有真实手指验收；101ms不能证明用户态正确消费抬指，编辑标记也不能单独定性为硬件故障。

## 用户态加载仍需核实

loaded/installed均0，但此时服务已进入用户态，**缺标记不等于已经证明库没加载**。下一步应先用该进程的只读dyld image-info/映射核实：实际主程序UUID、附加LC_LOAD_DYLIB、EldenIconCpuMode是否在已加载列表、对应已签库的UUID/初始化节。结合完整目标等待栈与事件数据再改，不继续猜目录/TC类别/CS_SIGNED。

已做离线排除：

- 实际SYS_kdebug_trace180无条件调用kdebug_trace64；179/180不同不构成通道故障。实际ARM64间接syscall0读取x0作调用号并跳过第一个参数，库编译码的varargs布局与原libsystem_kernel._syscall匹配。尚未证明此调用在服务sandbox里到达marker，不能将静态ABI匹配当实测。
- 原库的__init_offsets存在，值0x4000对应install；代码/字符串/TC仍是v67起同一冻结资产，不缺构造器的静态描述。library-sections.json、library-code.asm和syscall.asm在research/icon-service-v71-20261111。
- 精确dyld1165.3源码现已补PrebuiltLoader、Loader、JustInTimeLoader、DyldRuntimeState到research/loader-policy-20261111。initializeClosureMode按内核传入的executable_cdhash验证主程序预构建依赖图，**不是只查Mach-O UUID**。新UUID本身不能作为原依赖图已失效的验收；当前传入字符串/运行时选图尚未实测。
- 精确shared cache符号地址已只读导出；local symbols稀疏时“最近符号+很大偏移”不能直接当真实函数名。新增libsystem_kernel导出地址可用于准确识别syscall，原3库文件的SHA未变。

## 工具与后续构建

edl_work/finalize_icon_bundle_mac.py支持v71增加实际触控/等待对象审计；prepare_icon_cpu_qualification.py只对已审核v71观察差异复用旧触控证据，power35仍逐字比对。不要无条件把此规则扩展到新的功能修改。v71资产已冻结edl_work/icon-dependency-20261111/v71。

未创建v72，不存在待刷新候选；当前工作文件即v71观察版。下一轮先补明确的加载证据，再合并真正修复与必要观察。完整快充及外围Goal不缩减。
