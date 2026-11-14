# Y700 原厂 Android 双马达手动测试

只在 TB322FC / ADB HA2A1PT2 的原厂 Android 使用，需当前 KernelSU root。
不刷机、不改内核。启动面板不会振动，按按钮后倒数3秒。

启动：Windows 桌面 **Y700 双马达测试** 快捷方式，或：

```powershell
py -3.12 -B D:/Desktop/Git/UniXNU_elden/tools/device/y700_motor_panel.py
```

- **只测 L / 只测 R**：对应原厂驱动标签，不预设横竖屏下物理左右。
- **L → R**：两颗依次各三次；两组之间再倒数3秒。
- **轻柔**：增益32、每次250ms；**中等**：增益64、每次350ms。
  初始原厂增益128。增益数字不等于机械振幅百分比。
- **立即停止**：只向经可执行文件路径核验的测试进程发SIGTERM，随后由它停止并恢复。
- 测试时轻握平板，尽量不用手指操作屏幕，避免系统触感与测试重叠。
- 两组都可感觉到，只能说明两颗都会响应；强弱还受握法、机壳传导、摆放影响，
  不能单凭主观感受判定故障。SLA0815是用户提供的马达型号线索，尚未独立核验。

实机已读出两颗 **AW86937驱动芯片**：

| 标签 | I²C设备 | 本次evdev | 输入设备名 |
| --- | --- | --- | --- |
| L | 2-005a | /dev/input/event1 | aw-haptic-hv |
| R | 2-005b | /dev/input/event4 | aw-haptic-hv |

程序每次沿sysfs重新寻找evdev并检查芯片、I²C、输入名、FF_CONSTANT能力，不依赖
event编号永久不变。系统 VibratorManager只公开逻辑ID0，因此普通振动测试App不能
证明两路独立工作。这里直接使用两路原厂input force-feedback接口。

读取精确本机haptic.ko SHA256
`f688fcaec0d33a7c70c83af8e6fcb1aca015097282da30dfcf1e7314ad5935b9`核对：
input_upload_effect FF_CONSTANT使用duration；input_playback只调本设备worker；
原厂timer有自动停止，input_erase清duration。gain_store只改该设备增益，
activate_mode_store只存模式；不触碰电压、频率、校准、固件或任何原始寄存器接口。
原厂RAM短效果结束后state可仍为1，故以framework IDLE和两路duration=0判断准入；
允许原厂RAM模式1或定时模式5，结束恢复原gain/index/mode，保持vmax8500mV。
测试使用互斥锁、三次脉冲、8秒进程时限、原厂硬件停止路径及退出清理。

初轮低增益32/180ms验证：L的三个计时器103/111/111ms，R均0；R的三个
计时器108/108/107ms，L均0，恢复gain128/index1/mode5/vmax8500/timer0。
用户没赶上且反馈不明显，**不算机械振动确认**。手动版已部署并完成只读survey，
中等档尚待用户手动感受；打开窗口与UI smoke不触发振动。

产物在工作台`edl_work/motor-test-20261114`；运行记录在
`lab/records/motor-test-20261114/manual-sessions`。部署文件：
`/data/local/tmp/y700_motor_test_manual_v2`，SHA256
`56969bdd5fc37127bab5bc838c8a30813e57641b462412c436bc9fc916dec3ee`。

复现ARM64静态工具：WSL Ubuntu-22.04的aarch64-linux-gnu-gcc，
`-static -O2 -Wall -Wextra -Werror -Wno-misleading-indentation`编译y700_motor_test.c。
源码有固定时长与强度上限；GUI核验部署的全SHA后才运行。
