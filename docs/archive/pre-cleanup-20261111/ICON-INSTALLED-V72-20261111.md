# v72：状态钩子修复，CPU图标补丁已实测安装，桌面交互仍未验收

完整Goal active；充电500mA限制、基本外设和真实触控仍未完成。GPU最后，扬声器先讨论，双口供电保护保持。本轮没有新增电源控制。

## 当前恢复态

已恢复stock boot_a 8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890，全像SHA匹配；edl_work/icon-observer72-stock-20261111/flash/result.json、android-final.txt。Android/root/green/boot_completed1，98%/4455mV/-408mA/Charging，熄屏。全部构建、监听、watcher、刷写、Mac只读挂载已结束。下次reference=stock并fresh准入，floor1738713600@footer0/parent3不变。

## 确定修复与实际安装证明

详见ICON-OBSERVER-BUILD-FIX-20261111.md：OWNER_DATA_MOUNT原本仅传给kern_exec.o，kdebug/kern_exit观察块被编译掉。v71实际三个入口都没有调用辅助函数；旧核helper存在并不能证明入口接通。新专用XNU_ELDEN_ICON_OBSERVER保留原编译选项；独立机器码审核必须确认syscall179/180及proc_prepareexit真正可达helper。拒绝参数最多8条，正常标记64条，原owner政策/库/TC/驱动保持。

v72核c07c5fcce0269e74886a67a364f9260834856b3f115f947f71defec088238100，183冻结来源；像e73891f34fccc5652a8d170a4548bbf5b16cd333ac0a849e44234ca51c51b7f2，session97e937cd08a4ba68，power35/backlight13。原30模块、HID207/220/37及52绑定、恢复和AVB过；fresh4436mV/yes，boot_a全SHA匹配。资产edl_work/icon-dependency-20261111/v72与v71相同。

实机捕获PID111的`ELDEN_ICON_CPU loaded`与`installed`各一次、拒绝标记0。这证明精确框架UUID/原getter指令/ABI检查及method_setImplementation安装成功，启用了原框架非CoreImage模式；**不等于所有图标渲染和Settings内容已通过**。v67–v71缺标记不能继续当作库未加载的证据，不需要再为此盲改TC、UUID、CS_SIGNED或目录。退出观察rv0/ns0出现在本次exec交接后、loaded之前，不能仅据此称服务已经死亡；目标随后可采到用户态线程。

## 本轮交互与限制

在PC控制器尚未发送任何输入时，明确停止旧watcher并保留trial-before-settle.json、watcher-before-settle.txt；同镜像/同logger启动支持`--desktop-settle-seconds 45`的新watcher。记录watcher-config-change.json。不是观察超时重启任务，没有重复上滑或重复点击。

锁屏CRC图通过，一次swipe-up完整释放；后续只有Dock与中间的白条，没有可识别的完整桌面。单次已验证Home调试命令按下/抬起均0，画面出现壁纸，但仍无可定位的Settings图标。控制器因此没有发送tap-settings，45秒“桌面稳定后点击”条件未达到；**不能写成延后点击失败或Settings已被本轮测试**。最后主动按已有RPC完成sync并回fastboot，没有等待无新证据的完整截止时间。

现有只读栈research/ui-waits-v72-20261111：早期SpringBoard在LaunchServices同步请求，后两次主线程已回CFRunLoop/UIApplication事件循环；iconservicesagent亦有dispatch等待。local symbols稀疏时的大偏移不是精确函数名。只说明采样时的主线程状态，不能排除其他线程、渲染合成或输入消费问题，也不能仅凭主线程空闲宣称整个系统不卡。

日志3659164B，raw SHA b9491632ad675d3cf8f48d09999ffd359167babac57c4167a25b6d434a513a92，采集763.954秒，**最大dropped=1415**，不完整。loaded/installed完整正向记录有效，但不能用未见某错误证明全程没有GL崩溃。不给该缺口安排单纯重复冒烟；后续功能轮保留关键原子记录并控制无关大日志。机器可读总结research/icon-observer-build-20261111/v72-live-assessment.json，画面同目录。

## 下一步

1. 保留现已接通的库安装和退出标记，不再沿“库一定没加载”的方向造件。
2. 沿无GPU显示转场、HID抬指到用户态消费、系统时间／布局状态核当前Dock-only现象。现有100ms注入时序不是消费证明；原sheng在ShengHidInput中明确实现消失ID的tip-up、confidence与edge位，可作逐层对照。冻结Y700源码同样有显式tip-up，不能只因为画面异常就删改它。
3. sheng已有package/BootDiagExport.c与TouchTrace.h触控路径记录入口，先核成功方法和本机ABI，避免重新发明不完整的状态钩子。下一轮应验证实际桌面/Settings功能并保留必要端点证据；不能用Home调试命令冒充最终无Home正常交互。
4. 充电访问路线仍按原厂保护/原生权限边界研究，原生FIFO异常和GLINK status1不重试；本轮未改变500mA预算。完整Goal不缩减。

无v73候选。当前工作文件为v72加PC可选桌面等待参数，设备是stock Android。
