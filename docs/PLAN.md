# 下一阶段顺序

## 0. 整理和复现归仓已完成

源码到 v91 签名候选已完成独立重建和静态核验；完整外围 Goal 仍未完成。
维护标记若仍存在，先完成提交/回收凭据收尾，期间不刷平板。
参考仓只同步引用，冻结候选不自动换料。旧证据不改写，失效路径通过迁移表解释。

11月13日已恢复无人值守真机授权。官方
[步骤](https://github.com/UniXNU/UniXNU/blob/7d85ede80092050f4d87f72ac7229bb605ce07fb/docs/zh/porting/steps.md)
以平台/存储/显示/输入为依赖起点；本项目按用户要求将充电及基本外设优先、GPU最后，
不直接套用其把GPU排在无线前面的顺序。新官方独立克隆加入清单，现44仓；
逐项审核在工作台lab/reference-reviews。最新ShengUfs最终树与更新前完全相同，
容器布局改动已撤回，不为其构建新的“提速”候选。

## 1. 稳定无 GPU 的实际操作

以 v91 软件 Settings 成功为控制组。下一轮合并有依据的显示/输入修复，
先对照 sheng 成功实现和现有日志，再选择一轮能改变决策的功能测试：
稳定画面、正确触点、点击 Settings、上滑/退出编辑，以及可用的串口回程。
v92原稿保留。其后续v93/v94已完成映射与软件Settings验证，整屏拷贝中位数
716.65→134.14ms；见FRAMEBUFFER-WC.md。v94继续保留为已验显示/软件输入基线，
不能把采样截图一致或copy提速直接当作实际手势、60fps或长期稳定性验收。
离线输入审计见[INPUT-STATUS.md](INPUT-STATUS.md)：v94软件短按的UIKit组间隔约100ms，
没有重现旧版数秒拉长；坐标/ID暂不改。实指窗口关闭自动注入watcher，
一次完成上滑、Settings短按与滚动释放。v95保留为单独的未验性能候选。
离线故障注入另确认了失败抬指丢失与并发快照覆盖，已完成两组件修复和原生对象核验，
见[EldenHidDelivery](../drivers/EldenHidDelivery/README.md)。[v100](../profiles/v100-hid-delivery/README.md)
已完成独立完整候选签核，仅替换两个触控对象；静态通过不等于实指通过。
实指窗口用v100做一次上述功能验证；若需回归对照再用v94，不默认重复两轮。

v100软件上滑/Settings与同步RPC回程已于11月13日通过。抓帧工具上限已跟随内核改为12，
并保留逐行/CRC/会话/断点保护；不再为同一软件基线重复刷写。实指仍待人在场。

CPU 同时记录实际上线、调度/IRQ 与负载，UFS 记录真实 I/O 延迟和吞吐。
吐日志慢不直接等同存储慢；开启所有核也不能替代阻塞定位。
优先复用 sheng 已验证的 UFS 方法，板级参数按 TB322FC 的 ArmadaOS/Linux 对版。

## 2. 完成基本外设

充电继续保留已验电池、UCSI、温控和回程，剩余 500mA/双口问题使用有明确结论的功能对照，
不再反复扫描已否定的 GLINK 写路径。旁路、80% 上限和保护窗不能因基本充电通过而省略。
现行电源验收和v96电池界面候选见[CHARGING-STATUS.md](CHARGING-STATUS.md)。
v96只恢复既有UI发布器，已实测真实96%状态栏及净放电标志；双口输入仲裁仍需原厂证据，不把它当快充修复。

Wi-Fi、蓝牙以 sheng 成功驱动规范为先，具体接线、时钟、复位、寄存器采用本机源码与实测。
Wi-Fi现状和下一步见[WIFI-STATUS.md](WIFI-STATUS.md)：原认证服务已适配arm64/iOS17并离线签名，
未安装/联网；本机Linux已有独立Peach配置，不能照搬未实测的整片QCC2072映射。
Peach要求固件OL-CPR处理先于TME-Lite；已将新适配统一到已有WcnPower规划/发送代码，
但发送器当前只有BT owner资格，Wi-Fi QMI→原生WLAN owner路由仍缺，不得用假成功或BT上电流程替代。
9文件Peach差异已回移到官方锁定的Linux7.0.14，6个变化C单元及输入/流程检查通过，
见[回移说明](../drivers/EldenWifi/peach/README.md)。完整OS适配和125对象构建已通过，
见[运行时说明](../drivers/EldenWifi/RUNTIME.md)；下一步审核保留警告并接入真实WLAN owner、
PCIe/DMA/MHI与网络回调，再做整核集成。对象/host通过或0x53非致命例外均不能当作已联网。
MHI错误读取误报下载成功已修复并完成125对象重建，见[MHI记录](../drivers/EldenWifi/peach/MHI-STATUS-READ.md)。
其余类型/未初始化警告与板级回调仍待处理，避免为消除警告直接设置掩盖错误的默认值。
蓝牙常驻模式已补离线停止接口与上行背压退出，见[WCN-SHUTDOWN.md](WCN-SHUTDOWN.md)；
v97已完成整核/恢复钩子的静态资格，见[构建档](../profiles/v97-wcn/README.md)，尚未entry转换/签包。
随后仍需真实WCN/HCI及daemon验证。现有候选不自动换料。
v98已离线构建只读HCI/主机状态接口和arm64/iOS17 BlueTool服务，原21H461 XPC协议及地址格式已核。
下一步补齐签名runtime/launchd合并与准确版本的transport/chipset适配；不直接套用27版arm64e补丁。
v99已完成最小runtime签包：保留原561任务/缓存，只挂载BlueTool，并按原签名绑定普通流分支。
详见[候选档](../profiles/v99-bt-runtime/README.md)。入口地址变化已按相同start.s对象核验。
该候选会尝试真实上电WCN。11月13日首测runtime准备成功，但在父时钟检查处停止，
尚未进入UART/HCI；已正常同步回程和恢复Android。[父时钟修正](WCN-PARENT-CLOCK.md)
按ArmadaOS固定的四代SM8750源通过host/native检查，v101完整候选实测已越过该门，
六路rail和power stage6通过；下一处为合并启动流程−110，先区分UART-open/peripheral/HCI子阶段。
同步回程和stock恢复通过，仍无HCI身份/配对验收；
不将三代TB321FU/SM8650的寄存器或固件带入，不把静态通过当蓝牙可用。
触控稳定后加入休眠/唤醒及双击亮屏，随后推进持久 Data、容量/内存、时间和日常应用稳定性。

## 3. 高风险和后续功能

扬声器启用前先讨论衰减和保护；GPU 最后。
KShimC + UniXNU + Android/recovery/fastboot 多入口路线保留，不能覆盖现有安全回程。

每轮只选一个明确功能目标；复用有效冻结证据、合并已明确修复。
必须保留 fresh 电量、目标身份、签名 floor、完整 SHA 与自动回程检查。
一次成功只覆盖该次工况，不代表全部外设或长期 Goal 已完成。
