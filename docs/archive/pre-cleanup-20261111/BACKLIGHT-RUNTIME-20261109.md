# AW99706 原生背光与无 Home 键候选交接

完整 Goal 仍 active；充电/基本外设优先，GPU最后。本轮完成离线原生构建、签名和核验，
没有刷机、重启、改 GPT/root/userdata，也没有原生背光实测。

**当前 boot_a 仍为 stock8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890。**
原厂 Android 保持充电；本轮只读67%、4082mV、-483mA、Charging。
可选“长边PD/短边电脑”换线问题尚未回复；保持 Android，不重启打断换线。
没有剩余编译、EDL、USB监听进程。新写 reference 必须用 stock；电量门仍是
fresh>=3500mV 与原生 fastboot battery-soc-ok=yes，所有 boot 1738713600@footer0/父链3。

## 完成的代码

设备仓 `drivers/EldenBacklight/`：

- Aw99706：承接已验证的原厂0x76/ID07及04/05/0d寄存器合同；仅亮度与使能位写入。
- Platform：复用固定 sheng `GeniI2c.c/.h` 原字节，以本机 wrapper2/SE2@888000 接入。
  SDA/SCL=8/9，HWEN88；不改引脚/HWEN、PLL/频率、电源轨、DMA/SMMU、固件或 MTP。
- 启动保持原来的 RCG 分频与 SCL 配置，临时投票供时钟；拒绝活动命令/GPI/DMA/非空FIFO，
  保存并恢复所改配置。PIO只轮询，物理IRQ输出保持屏蔽；失败不重发芯片命令。
- 取消/中止超时必须另验硬件 idle，不能把软件 Idle 当完成。未知状态保留映射与时钟到整机复位。
- 共享 GCC 票据只修改自身位；SE6蓝牙、SE7或IBI仍有票时保留公共时钟。
  **SM8750 SE7是bit17，IBI是bit27/28，不能套连续bit4..11。**
- BacklightRuntime：真正继承原始 AppleARMBacklight，拥有独立工作循环、非阻塞 setProperties、
  CoreBrightness 的16.16秒 FadePeriod、0..4095上限与新请求替代旧渐变。
  只有芯片/配置读取成功，IODisplayParameters与必需属性发布成功才注册服务。
  NativeSandboxGrantProcess沿用sheng约定；CoreBrightness实际请求/权限仍须实测。

原始21H461 AppleARMPlatform整段代码身份、C2/D2、0x370对象尺寸、169槽与三条原始绑定均已核。
实际链接后的169槽再次逐项对照自有方法或当前IOService；没有复制超出边界的320槽表。
类静态初始化移除并延后至 recovery/root/Data/PID1门通过后的显式启动；slot168未知调用会诊断panic。

## 构建与候选

最终组合 **basic bundle v15 / power v14 / backlight v5**。
kernel=52658280B，SHA256 `7a6eafc9f1f418cf0951a0608163ed73ae5a21077ab76ae2de5d139d20212ea0`。
背光archive=268760B/SHA `6d8d30a8be20590ca71eb8dba3cfcc42dc740fa292068415a779ad8790507b5e`。
101份源码及构建输入已冻结于 `research/basic-bundle-20261108/source-compiled-v15/`。
v13/v14为中间构建，不作推荐件；旧CPU调查v12及全部恢复件保留。

以下目录均位于 `edl_work/basic-bundle-20261108/v15/final/`，目标文件为 `boot-signed.img`：

| 目录 | session | SHA256 |
|---|---|---|
| backlight-inspect-focused-gestures | b1f3bc4baa2710fc | fe3a3475aa281b4d1c73509883e1b85b53f2398c9b76aebf867fd1fabae09072 |
| backlight-roundtrip-focused-gestures | be5b91dbf72ad695 | 7ab4ea8da8cc6503dd136df8fd0ac3e55899c5317e8430f4e1bf97517a060409 |
| backlight-service-focused-gestures | 5963a83f2c59da6d | a343a2d6582d95354f2ca25d558f1e2e8b49e1d6ac57beac93947a87b7eb4f7f |

三件96MiB、原始30模块/HID207+220/37、所有资产、6GiB/RAM Data、FD/shim/FAT与独立AVB通过。
保留已验M3/UCSI/500mA/charge RPC及8秒默认倒数、USB日志和fastboot回程。
Backlight参数分别1/2/3；OFF菜单项不启动背光。电池发布器仍默认off，未启用无线或物理触控。

## 主机证据

- `edl_work/backlight-platform-20261109/result.json`：14726项含读写边界的ASan/UBSan检查。
  覆盖实际寄存器快照、正常读写/恢复、忙/GPI/DMA、错误引脚/协议、NACK、确认/未确认取消、
  计数器停止、错误读回及蓝牙/SE7/IBI并发票据。
- TestBrightness验证16.16换算、上限、向上/向下渐变与中途替换；原Aw99706核心95项仍适用。
- 各候选的 signing/independent-audit.json、最终 backlight-abi.json 与编译archive已保留。
- 原厂实机证据仍是 `backlight-oem-wake-20261109` 的46条正常Android I2C事件；
  **原生亮度尚未实测，不能把主机通过或服务注册当作背光验收。**

## 接机后次序

1. 先确认换线后的原厂输入/ADB状态，继续完成双口PD选路调查。CPU只读候选仍可独立测试。
2. 按既有电量准入和串号门使用 `flash_verified_boot.py`，首发inspect；USB监听先启动。
   预期acquire=0/stage5、inspect=0/ready1/chip-writes0、release0/quarantine0。
   这是芯片只读检查，控制器需要临时供时钟/配置PIO，不能称整个过程零MMIO写。
3. inspect实机通过后再测试roundtrip：实际亮度减半，500ms后恢复原值并释放控制器。
   原亮度<=1时不强行点亮；没有真实变化不能记成功。
4. 再上service观察CoreBrightness实际请求、亮度和画面。现有seed的no_auto_dim、面板睡眠、
   完整电源管理仍未改变；亮度服务不等于屏幕睡眠或双口净充电已完成。

只读判读脚本：

```text
python edl_work/assess_backlight_session.py RUN_DIR --expect-session SESSION --mode 1 --output RUN_DIR/backlight-assessment.json
```

脚本绑定本次USB session；旧充电日志已用作反例，不能凭fastboot ACK误判背光通过。
mode3只记录service_registered和请求，仍要求实机亮度验收。
ACK、RESET_AFTER_SYNC与Windows实际fastboot枚举是不同证据，继续按原无人值守流程逐项确认。

Mac重建顺序：prepare_backlight_upload.py→compile_backlight_runtime_mac.py；
prepare_basic_bundle.py显式传--backlight-version，随后build、derivative、export、accept、package、sign与audit。
新版本号保留所有旧冻结源；Mac操作仅在 `/Users/ciscohe/Desktop/UniXNU_elden`。

## iPad Pro界面

独立说明：`research/IPAD-PRO-GESTURES-20261109.md`。
已验证v11内核加iPad8,1/J317/noHome ADT，签名cfef1394…/session82eaa003c6589bf9，未刷。
与背光分开候选，保留单变量判读。物理Face ID、底部条与真实上滑不能混称已完成。
