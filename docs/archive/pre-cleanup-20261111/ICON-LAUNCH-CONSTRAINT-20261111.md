# v69：实际阻塞是 RAM 副本的系统服务启动约束

v69 138113B/drop0，原路径绑定成功、环境追加、激活 stage7/result0，然后精确 PID112/unique112 捕获 error1/mac0/namespace3/code4。选中时名字仍为 xpcproxy，解释了 v68 进程名过滤漏报。XNU reason.h 明确 code4 为 Launch Constraint Violation；普通签名有效并不等于允许该系统服务启动。

原始 AMFI 同时输出 c[8]p[1]m[1]e[6]、validation-category1、launch-type1。`inspect_amfi.py` 和 `decode_rules.py` 在精确原 AMFI a1088aaa…中提取规则：c8 是报告上下文，e6 对应系统服务规则6，不应把 c8 当 trustcache 类别。规则6还要求以下任一：不在带约束类别的TC / SIP保护 / 授权authapfs卷 / 系统卷。RAM bindfs副本不满足该卷身份。原程序TC类别1同样要求系统/authapfs卷；单改条目类别不能合理解决。

上游 Enc3l 的 zzyc-owner-skip-launch-constraints.patch 用 sheng-no-lc 绕过整项检查。此次保留原检查调用，仅给已校验的单个 owner 副本做兼容：explicit iconCPU开关+完整RAM部署成功+原路径选中的进程uniqueID、UID276、readonly union bindfs、launch-type1、精确副本CDHash152bdc…，且原失败必须是EPERM/namespace3/code4及已观测 c8/p1/m1/e6。其他结果原样拒绝；随后原签名处理继续。TC类别、库、agent字节和其他代码均不变。这是明确的单服务启动约束例外，不是假称Apple原策略已认可RAM副本。

v70已完成一轮实测：3419056B/drop0，核47c38f6e…、像73535ff8…、sessiondb0bebeba406c9eb。一次精确owner admission成功，原拒绝已跨过；loaded/installed均0，不能称用户态库修复成功。host直接编译生产Common.inc的拒绝门与逐字节CDHash反例通过ASan/UBSan。编译遇到XNU只导出strnstr的问题，已改为有界查询，原失败日志和SHA绑定修正记录均保留。

PC流程：340.515秒收到锁屏、385.594秒请求上滑、386.640秒完整释放确认、402.250秒桌面后唯一Settings点击，402.765秒down/up均0。414.797/422.703秒两张后续CRC画面显示桌面与编辑标记，没有Settings内容。不能把HID API返回0等同UIKit点击验收；也不能仅凭截图就定性为驱动抬指丢失或UFS阻塞，缺少实际事件时间和消费链证据。582.375秒自动发fastboot，sync/RPC正常。没有第二次点击，没有真实手指测试。

v70已完整SHA恢复stock，Android/root/green/boot_completed1，98%/4452mV/-415mA/Charging熄屏。恢复icon-owner70-stock-20261111。所有构建/设备/监听任务结束，下次reference=stock并fresh准入。原始日志与trial、restore信息已绑定在v70-live-assessment.json；截图另存同目录。

下一步应一次收齐剩余进程链：在现有精确uniqueID关联上观察launch约束之后的process_signature各段、task-access-port/任务签名调用，以及目标iconservicesagent或尚未exec完成的xpcproxy等待栈。原runtimeObserver只采SpringBoard/backboardd/Preferences等，漏掉了目标服务，所以无库标记不能区分内核后段等待与dyld初始化等待。旧v69行尾flags2200不完整，不能以此断言CS_SIGNED缺失（见下面v71更正）；匹配CDHash本身也不是完整签名流程的充分证据。下一份同时记录软件点击的实际按下/抬起时间；先修已证实原因再上机，不把同一拒绝重复试验。

v71进行中更正：PID109的flags-copied和signature-return均明确为0x22002301，包含CS_SIGNED；process-signature与thread-signature返回均0，直接跳过taskgated路线。因此签名后段已通过，缺CS_SIGNED的假设被否定，未据此改任何标志。SYS_kdebug_trace180也已从实际syscalls.master/kdebug.c确认无条件转入被观察的kdebug_trace64，不是仅凭编号179/180不同就认定通道错误。当前继续采目标用户态等待与点击间隔，尚未完成本轮验收。

充电完整性、物理触控和GPU均未因本轮取得新验收。power35保持冻结，没有重试GLINK或原生DC写。

v69已全SHA恢复stock8b8e72da…，Android/root/green/boot_completed1，99%/4448mV/-426mA/Charging熄屏。恢复icon-signature69-stock-20261111。完整Goal active。

补充参考：https://theevilbit.github.io/posts/amfi_launch_constraints/ （macOS机制说明，非本机规则证据）；本机结论以原21H461二进制及实测为准。
