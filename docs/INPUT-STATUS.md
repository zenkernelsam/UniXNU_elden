# 输入与阻塞审计 · 2026-11-13

11月14日对照用户提供的myron HID说明，当前独立Digitizer/原EventDriver/edge-tip/
投递与抬指链路已具备，未发现值得本轮修改的新缺口。v106后来已实指打开Settings/
General，About等待后也出现；下文较早的“实指未验”只适用原v94离线审计。
本轮不重写触控或改变时间戳接口，细项见[参考对照](TOUCH-REFERENCE-CHECK.md)。

11月13日首个v100纯实指窗口已收到212次控制器变化和212次成功HID报告，invalid0，
没有软件手势注入。用户确认能上滑解锁并有多指反应，但画面很卡；Settings与滚动抬指
未完成时测试自动回程。八核online/work/timers/ipis均ff，运行中online持续ff；内存仍6GiB。
27个整屏copy样本中位133.86ms。该轮同步RPC与完整stock恢复通过，日志drop54。
见[实指记录](../profiles/v100-hid-delivery/physical-result.json)。这完成上滑的局部验收，
完整触控和性能仍待验证；下文保留11月12日的离线审计。

v94 已通过软件上滑、短按并显示 Settings 内容。当前没有证据要求再次更改坐标比例、
触点编号或 HID 时钟。真实手指操作仍未验收；用户要求继续离线，本次只分析已有日志和源码。
v95 仅为未刷入的显示 paired-store 候选，不能作为新的输入或性能成功证据。

## 已有日志能说明什么

证据见 [v94-input-audit.json](evidence/v94-input-audit.json)，绑定签名像
`cea9703fdb9a98a8bf48ea9a885242fe7caafee5de20c62279682f0ff2b4bc80`、
session `bc0a28dcaf1688e2` 和原始 USB 日志 SHA。原始文件保留在工作台
`edl_work/live-fbwc-v94-20261112/usb/20261111-230741.bin`。

| 层级 | v94 记录 | 结论边界 |
| --- | --- | --- |
| 软件发布 | down 调用 92µs、up 调用 88µs，两次开始相隔 101099µs | 是软件短按，不是手指到屏幕的延迟 |
| UIKit | 四条 SpringBoard sendEvent；两组首条间隔 100034µs | 这次没有重现 v77 约3.96秒的组间隔；不是显式 down/up 事件 |
| 控制器 | 收尾累计7579帧、invalid=0、published changes=0 | 有采样，无本轮实指变化；不能判物理输入成功或失败 |
| HID | 28条 event6、返回值均0 | 包含软件上滑和短按；不能算成28条真实触点 |
| CPU | 准入结束 online/work/timers/ipis 均ff，error=0 | 八核曾通过工作检查；不是持续负载、DVFS或长期SMP验收 |
| 显示 | 整屏copy中位数134.14ms；采样Settings画面与v91一致 | 已知显示耗时仍明显；不等于整体FPS或UFS性能 |

完整日志有40字节 dropped，旧判读器仍返回 `complete_observation=false`。
本次另核四条hook记录的连续编号、计数、恢复和无溢出，记为
`captured_hook_record_accounting_complete=true`，不把它升级成全日志完整。
不能用缺少 Preferences 的 sendEvent 断言其没有收到事件或缺驱动。

原版21H461 UIKitCore 的 sendEvent 调用点 `0x18a18a600–620` 把
`timestamp` 的转换值传给 kdebug arg2。两组arg2差1941111；**若按19.2MHz换算**，
间隔为101099.53125µs，与软件短按间隔一致。转换系数没有动态读取，
故不据此宣称所有用户态时钟已校准，也不把通用sendEvent当成抬指证据。
旧v77/v91与本轮配置不同，这组比较不用于归因某一个补丁。

## 对照 sheng 与本机 Linux 的结果

- 本机NT36536使用固件点报文；sheng的NT36532E从THP矩阵提取接触点。
  可复用HID接入方法，不能直接套用sheng的矩阵阈值、轴最大值和校准算法。
- 本机报文包含寄存器字节共109字节；1..65字节和为0，10个六字节记录，
  状态1/2活动，硬件ID1..10，X/Y为大端16位。解码器与固定Linux源码一致。
- 固定协议来源 `6f97250ead155fc3b980856323e99a4623dae73d` 的文件SHA为
  `67295bfd775d8eafdd09e07db9d70c690054dec72dcae0c922345944dd1b1a66`。
  当前参考DTS的size为19041×30401，对应包含端点的max19040×30400，未设轴交换/翻转。
- PointReport映射为0..32767；HID中的 `transformCoordinate(value,32768)`
  对此范围是恒等变换。更下游0..65536固定点表示造成的数值约翻倍，不是坐标重复缩放。
- `elden-touch-id-base=1`使硬件槽位映射为1..10，没有无限增长的ID。
  接触生命周期分配与sheng仍有语义差异，尚无证据应替换当前方案。
- `elden_hid_request_stop()`停止软件/属性线程，不卸载sink；物理线程仍能发送收尾释放。

正式仓的PointReport/Runtime与冻结v91副本逐字一致。实际编译的冻结EldenHidGlue
比通用补丁模板多三行既有UIKit观察器武装代码，原输入发布部分一致。
**分析实际运行行为须使用profiles内的冻结源码**，不能将模板缺少观察代码误判为运行缺陷。
所有引用文件、版本与SHA均在证据JSON中；参考克隆未修改，候选未换料。

## 源码风险与离线修复

`NativeHidInput::touchSend()`在分配/handleReport成功前更新lastSeq和prevIds；
失败的释放报告可能不能重试。桥的sink为void，publish返回0也不等于handleReport成功。
此外，全局快照写完后解锁再runAction，多生产者并发时可能覆盖快照；
“torn read: retry on next tick”注释与当前无周期发送器的实现不一致。

后续故障注入已从原始方法及publisher中复现上述两种失败；新派生源码改为成功后提交触点历史，
返回真实发送结果，以独立发布锁保留同步回调期间的完整快照。仅临时错误最多四次尝试，
显式等待总计不超过24ms，无空闲定时轮询。五帧正常多指报文指纹保持一致。
ASan/UBSan错误/并发用例及两个ARM64/iOS17对象、延迟初始化/导出接口检查已通过，
见[修复与复现说明](../drivers/EldenHidDelivery/README.md)。后续[v100](../profiles/v100-hid-delivery/README.md)
已完成整核链接、签包及独立检查，仅替换两个触控对象，菜单和其他资产保持v94；尚未实测。
冻结v94及其他候选没有变化；这些故障仍不能当作早前真机症状的已确认根因。
实指验收窗口仍不得并行软件点击/上滑，避免输入来源混淆。

## 下一轮一次功能验证

仅在用户解除离线限制后，用已签核v100做一次实指验收；v94保留为回归对照，需要时再刷：

1. 按现行设备工具核目标、fresh电量、floor、完整写后SHA和回程。启动监听，保留本机后备定时。
2. 不启动 `watch_icon_trial.py`，不发软件home/swipe/tap命令。它会自动注入，不能用于纯实指窗口。
3. 锁屏稳定后由用户上滑，再短按一次Settings，最后拖动一次列表并抬手。
   记录操作时段与画面，同时收触点变化、HID结果和收尾报告。
4. 成功判据是操作与界面对应、释放后停止拖动、无自行长按/编辑；
   不能只看控制器ready或handleReport返回0。回程后保存完整证据并恢复Android。

失败分流：无控制器变化查SPI/固件；有raw变化但坐标不对查板级映射；
有正确触点但HID失败查发布路径；HID正常且应用崩溃则修实际依赖；
用户态进展正常而画面慢则继续显示。日志缺失先标未知，不发明缺失驱动。
当前UIKit观察器由软件Settings请求武装，纯实指窗口可能没有UI_DISPATCH，这是预期边界。

v95性能测量另有已签候选，不把它的未验证改动混入首次实指基线。
UFS以真实I/O延迟/吞吐证据决定下一步；“吐日志慢”不足以要求换存储驱动。
充电500mA、Wi-Fi/蓝牙、双击唤醒、持久Data等仍按PLAN保留，完整Goal不变。

## 离线复核

现有判读器可直接重跑（不会打开设备）：

```powershell
py -3.12 tools/legacy-workbench/assess_ui_dispatch.py --run D:/Desktop/Git/Patch/Y700-Elden/edl_work/live-fbwc-v94-20261112 --session bc0a28dcaf1688e2 --output D:/Desktop/Git/Patch/Y700-Elden/lab/records/input-audit-20261112/strict-dispatch.json
```

它应报告SpringBoard派发已观察、完整观察为false、dropped40。
补充计算是event[2].us−event[0].us与两组arg2之差；没有更改旧判读器的丢包规则。
