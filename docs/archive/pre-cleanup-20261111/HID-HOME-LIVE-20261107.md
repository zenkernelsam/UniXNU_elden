# Y700：虚拟 HID Home 实机验收（2026-11-07）

已从锁屏通过原生 HID Home 事件进入 SpringBoard 主屏。真实 NT36536 触控仍在下一工单。

## 资产与证据

- `local evidence: edl_work/hid-20261107/boot-signed.img`，SHA256 `7282b272395a3c7a9a099bad3ab1f10b20a6951320129a96373f3410a868a363`。
- 内核 SHA256 `660a459018ef1e7e359fb901d3a891d29090a98f3e5773976ca32d6cf7a0c4f5`，会话 `908a25f4b860f157`。
- boot **1738713600@footer0**，stock Props/RSA/data digest 通过；仅写 boot_a，现场 GPT 边界与完整设备 SHA 回读通过。
- 原 FD、shim、ADT、trust、512MiB RAM Data及6GiB内存窗口保持。HID复用 canonical `ShengHidInput`，运行类 `NativeHidInput`。
- 所有207个原始 HID vtable 槽、208B base instance、constructor/destructor及绑定原始指令已核。
- 实机日志：`family=0 matching=0 original-size=208`、`metaclass=0`、`event-driver=1`。
- PC一次发送会话绑定的 Home，收到 ACK；事件4按下和事件5松开均 result=0。
- 连续420.063秒、136次状态、dropped=0、无kernel panic。原始日志1,672,886B，SHA256 `4dfbfed5448becd795e1a3df49974665c14c86e6fc0fc7f3b4eb2805bbbab7ab`。
- 资格JSON：`local evidence: edl_work/hid-20261107/qualification.json`。已冻结本轮自有构建脚本和glue：同目录 `qualified-source/`；组件来源仍按固定Git提交读取。

## 真实画面

四份图均380行完整、CRC通过。frame1/2是锁屏时钟；Home之后frame3/4有App Store、设置和Dock，
为SpringBoard主屏。frame3 CRC `67e5bebb`，frame4 CRC `44dc86a3`。
图在 `local evidence: edl_work/hid-20261107/frames/`，另有3倍无插值查看副本。

当前没有壁纸，部分widget是白块，显示还须完善；这次不宣称条带/GPU问题已修复。
Data仍512MiB临时RAM，系统日期/时钟尚非RTC验收结果。真实触控、持久化、联网、音频等均未由Home事件证明。

## 使用

在新候选入核前运行：

```powershell
python -u -X utf8 <host-workspace>/edl_work/elden_usb_console.py --listen --expect-session 908a25f4b860f157 --status-interval 3 --seconds 420 --wait-seconds 150 --home-after-ready --output edl_work/hid-20261107/new-run
```

此选项等正常root/Data/PID1状态持续10秒、原生HID event driver匹配且出现非启动图案，再只发送一次Home。
驱动100ms后总会尝试松开，即使按下返回错误也不能留粘键。会话不符、驱动缺失或已有pending操作会拒绝。
不要与另一监听器争用同一个COM端口；COM号按现场完整PNP身份发现，不硬编码。

Home、fastboot RPC为两种独立动作，不能同时选用。现有8秒倒数入核、180秒panic回fastboot与主动RPC保留。

## 下一发：实际触控总线只读取证

固定Armada引用的h0cheung Linux提交 `6f97250ead155fc3b980856323e99a4623dae73d`：
SPI4 `0xa90000`、wrapper1 `0xac0000`、GPIO48/49/50/51、IRQ162、10MHz、NT36536。
Y700的GCC S4 RCG `0x183dc`、S4 branch `0x183cc`；core2x/core/M-AHB/S-AHB分别为
`0x2315c/0x23148/0x23140/0x23144`。它用shared RCG；sheng的SM8550 DFS地址不能复制。

新 `elden-touch-inspect=1` 仅读GCC、TLMM，并在所需时钟branch都开启时读GENI身份。
不做clock vote、GPIO写、SPI命令、DMA、控制器reset或触摸flash操作。结果将决定下一发初始化方式。
新构建独立输出，当前已验Home件和老services件均保留。
