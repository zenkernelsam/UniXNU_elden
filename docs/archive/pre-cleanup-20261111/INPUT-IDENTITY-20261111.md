# 完整iPad Pro身份与Home首击对照：点击仍未正常，Android已恢复

完整Goal active。充电断线对照e1edfb1c…仍未刷，等用户方便；本轮未改变任何充电控制。GPU最后，扬声器先讨论，floor和双口保护保持。

## 当前恢复态与候选

当前boot_a为stock8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890，全像校验恢复。Android/root/green/boot_completed1、98%/4450mV/-440mA/Charging，熄屏。恢复edl_work/icon-home72-stock-20261111/flash/result.json、android-final.txt。全部写入、USB监听、watcher、构建和挂载任务结束；下次用stock reference并fresh准入。

本轮复用v72 kernel c07c5fcce0269e74886a67a364f9260834856b3f115f947f71defec088238100，183冻结来源；没有重编内核。配置为icon-cpu-focused-gestures-ipad-pro：image fafa70ffa916a1b2903e3dcd81f42ea624fa81d026010362372b7b12c3ce9afa，session9af4602e5779ee12。只补model/target/product描述/fallback/artwork subtype六项，iPad8,1/J317，原无Home按钮属性保持。其他ADT资源、板级compatible、6GiB窗、根卷/TC/控制保持。原始RAM Data没有旧MobileGestalt缓存。

官方指南明确无Home手势需搭配无Home机型；旧实机其实一直inherit iPad7,1/J120，11月9日的完整Pro候选未刷。本次补齐用户早已要求的机型配置，内核实报iPad8,1；不提供FaceID硬件认证，也不由机型字符串宣称手势正常。

## 两个有区分价值的结果

1. **上滑→桌面→等待45秒→一次Settings**：live-icon-pro-v72-20261111，3487084B/drop0，raw21H461日志SHA记录在ipad-gestures-20261111/pro-swipe-assessment.json。图标库installed1；上滑后识别桌面，点按实际间隔100310us，最后画面是桌面编辑标记，没有Settings内容。已曾stock全SHA恢复后才开始下一轮。
2. **Home→桌面→一次Settings，不先上滑**：同一签名像，live-icon-home-v72-20261111，3688937B/drop0，installed1，software swipes0。新机型齿轮较旧模板右移约5px且素材不同，严格模板未过；模型视觉确认齿轮框[191,139,210,158]，既定点[198.18,150.00]在框内。停止PC watcher、保存记录后只发一次相同tap-settings；实际间隔104332us，结果仍为编辑标记，没有Settings内容。PC800秒后备正常回程。不是完全由旧模板自动通过的流程，判断与唯一点击UTC均在manual-tap-assessment.json。

这排除了“只改机型就能解决”以及“只因为先做了上滑才失败”的简单解释。不能把编辑标记直接当作已测定的UIKit长按状态；没有消费端手势状态记录。没有真实手指测试，也没有重复点击。

## 静态ABI与时间路径核对

原handleReport槽181仍为0xfffffff004add0c4，self/report/type/options分别保存后调用clock_get_uptime，再将时间戳、report/type/options转到槽197。其时钟import槽0xfffffff005d00fb0确实绑定native clock_get_uptime；后者把mach_absolute_time写入调用者指针，没有发现这里的错误原型/错绑。

精确IOHIDFamily-2031.120.4的IOHIDEvent.h/cpp/Data.h已下载到hid-event-time-20261111并记录SHA。原IOHIDEvent **getTimeStamp是槽16**（0xfffffff004afb494），读取对象+0x30；options在+0x48，bit7表示continuous，必要时转absolute。槽17是setTimeStamp；槽19不是时间戳，不可按猜测调用。

实际native ml_get_timebase读CNTVCT并加CPU偏移；原用户态mach_absolute_time的常规快路径也读CNTVCT并加commpage偏移，selector0则走Mach trap。因此“内核CNTPCT、用户CNTVCT不同”这一简单假设不成立。当前**尚未测得实际事件时间戳、实际commpage selector/offset、用户消费时刻**，不能宣称时钟一致或不一致，更不能盲改计数器/偏移。

下一轮若需要补关键证据，优先一次记录原事件时间戳/选项与内核now，并对照用户commpage时基及消费端状态，而不是继续重复同一100ms点击。所有新增读法先对原始机器码/API和真正调用入口核验，避免重演v72之前“helper存在但未编进调用点”的遗漏。

## 接触ID原型：仅研究，未集成

Y700 Linux驱动用硬件ID选input_mt_slot，再由input_mt_report_slot_state在新接触时分配ABS_MT_TRACKING_ID。sheng发布c[i].tracking_id，并采用1..NVT_SLOTS的有界分配，避免超过backboardd路径索引上限；本机当前桥直接使用硬件槽位id+1。这是值得审核的语义差异，**Home后首次点按同样失败，不能将它当本轮已证明根因**。

contact-identity-20261111/ContactIdentity.h和test.c仅在research：活动ID稳定、优先避免紧邻重用、禁止同一报告立即复用刚结束的ID；容量不足返回需要先发释放边界，不能丢掉旧触点的up。1000接触、满容量、50000集合转换、连续性/唯一性/拒绝不改状态的ASan/UBSan检查通过。尚无调用者释放边界接入，也未编进任何内核；不可直接当合格候选。

ShengNovatekTouch参考HEAD69fdd6223ed2572aa8bd8351e77c53a238276130；本机协议源引用6f97250…的novatek-nvt36xxx-spi.c。没有更换已验证的SPI、DMA、SMMU、固件或触控源。

当前PC控制器新增可选--entry-gesture home，默认仍swipe-up；45秒桌面等待可选。无v73候选，已验Pro身份可以保留作后续配置，但图形/触控的完整功能仍不合格。
