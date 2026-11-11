# IDA 分析笔记：LinuxLoader-p114.pe（Sony ABL）

> 2026-10-23。目标文件：`unlock/LinuxLoader-p114.pe`（PE32+ / AArch64 / imagebase 0，
> .text 0x1000–0xE7000，.data 0xE7000–0xFD000，.reloc 0xFD000–0x101000）。
> 通过 IDA Pro 9.0 + ida-pro-mcp 分析（MCP 端口 13337）。

## 1. fastboot 命令分发器 = sub_281FC（0x281FC，0xD78 字节）

一个函数里串起了全部 fastboot 命令：`boot` / `flash` / `erase` / `getvar` /
`download` / `reboot` / `reboot-bootloader` / `oem` 等。

### `boot` 命令路径（0x28690 起）

```
0x286d4  bl   sub_1CD24        ; 取锁状态（返回 byte_F3B85）
0x286d8  and  w8, w0, #0xff
0x286dc  cbz  w8, 0x286f8      ; ==0 -> "Not support for locked device" / "Command not allowed"
0x286e0  cmp  w20, #0x65F      ; boot image 最小尺寸
0x286e4  b.hi 0x28734          ; -> 解析 boot image 头
0x286e8  ...  "Invalid Boot image Header"
```

- **xperable `-x -4` 的补丁点就是 0x286DC**：`nop` 掉 `cbz` → 永远走 boot 路径。
  （README 的 `286DC%1f2003d5`，`1f2003d5` 小端 = `0xD503201F` = NOP。）
- `0xB51C2` = `"Command not allowed"`，xrefs：0x28724（本函数）、0x4DAFC（sub_4D940）。
- 相关字符串：`"Not support for locked device"`、"Invalid Boot image Header"、
  "BootImage is Incomplete"、"Failed to load/authenticate boot image: %r"。

## 2. 锁状态：两个全局字节

| 函数 | 作用 |
|---|---|
| `sub_1CD24` (0x1CD24) | `return byte_F3B85;` —— 主解锁状态 |
| `sub_1CD68` (0x1CD68) | `return byte_F3B86;` —— unlock critical |
| `sub_1CF18` (0x1CF18) | `oem unlock` 的设置函数：改 `byte_F3B85` / `byte_F3B86`，再调 `sub_17CF0(1)` 持久化 |
| `sub_1D42C` (0x1D42C) | 设备信息初始化：读 device-info，校验 magic `"ANDROID-BOOT!"`；magic 不对就清零 `byte_F3B85/86` |
| `sub_17CF0` (0x17CF0) | 通过 **VerifiedBoot 协议** 的 `VBRwDevice` 读写 device-info 块（持久化） |

结论：解锁状态存在一个 `"ANDROID-BOOT!"` 的 device-info blob 里（由
VerifiedBootDxe 的 `VBRwDevice` 服务读写），`oem unlock Y/X` 会写它。
`secure` / `unlocked` 的 getvar 与这个字节对应。

## 3. boot 镜像校验 = sub_80BC（0x80BC，0x123C 字节）= `LoadImageAndAuthVB1`

流程（精简）：

1. 找到 VerifiedBoot 协议（`off_E9710[40]`）；
2. 从分区加载 boot image（`sub_11988`）；
3. 处理 dtbo（AVB，`sub_AEF0`/`sub_17814`）；
4. `sub_BC70` = `XBootResetSignerCert`；
5. `sub_1CD24()` 取解锁状态 → `sub_1CD68()` 取 critical；
6. 遍历 signer certs → `VBVerifyImage`（AVB 校验，`sub_BCB4`）；
7. 打印 `"Sony image verification done! boot state is: ..."`，拼
   `androidboot.verifiedbootstate=` / `veritymode=` 进 cmdline；
8. 失败路径：`"No match of Signer Certs found"`、`"VBVerifyImage failed with: %r"`、
   `"Failed to load/authenticate boot image: %r"`。

### 两道门

1. **锁状态门**（LinuxLoader）：`byte_F3B85`；`-x -4` 打 0x286DC。
2. **AVB 签名门**（VerifiedBootDxe 模块）：`-6` 打模块内偏移 **0x25FC**：
   `3d000014` → 0x1400003D = `b +0xF4`（跳过校验）。

所以 `fastboot boot` 未签名镜像必须 **同时** 打 `-x -4`（放行 boot 命令）
和 `-6`（跳过 AVB）。只打 `-4` 不够。

## 4. 和实测行为的对应

- 打完 `-x -4 -6` 后，用 **xperable 自己** 的 `-I img -1 -c boot -1`
  发送 boot，镜像真的执行（beacon PSHOLD 断电实测）。
- 标准 fastboot 客户端在同一会话里发 boot：第一发常 0.027s OKAY 但留
  fastboot（暂存），第二发才跳（USB timeout）。这是 ABL 的 boot 命令
  状态机怪癖，和校验无关。
- 补丁只在 ABL RAM 中，重启即失效。

## 5. VerifiedBootDxe（从 XBL 提取，O_82）

- XBL = ELF64 → FV → gzip 压缩的内层 FV（GUID `1d301fe9-be79-4353-91c2-d23bc959ae0c`，
  数据是 gzip）→ 103 个 FFS 文件。
- 已提取：`research/xbl-O82-VerifiedBootDxe.pe`（53,248 B，PE32+/AArch64）。
- `-6` 补丁点：模块 RVA **0x25FC**，原指令 `tbz w8,#0,0x2618`
  （`GetBootState`：检查 .data+0x290 的"镜像已校验"标志位 bit0；
  未校验则报 `"VB: GetBootState: call the API after image verification"` 并返回 0x50）。
  xperable 把它改成 `b 0x26F0`（`0x1400003D` = `b +0xF4`）：
  0x26EC `mov x19,xzr` → 0x26F0 `b 0x26DC` → `mov x0,x19; ret`，
  即 **GetBootState 永远返回 EFI_SUCCESS**，绕开校验状态门。
- 其他 XBL 模块（同目录 `xbl-O82-*.pe`）：`SecurityDxe`、`SecRSADxe`、
  `ASN1X509Dxe`、`HashDxe`、`QcomBds`、`StartFlagDxe`、`BootCounterDxe`、
  `XResetReasonDxe`、`QcomChargerDxeLA`、`QcomChargerApp`、`ChargerExDxe` 等。

### 5.1 P_114 VerifiedBootDxe（2026-10-23 从真机 RAM dump）

- 文件：`research/VerifiedBootDxe-P114.pe`（53,248 B，0xd000；
  `xperable -M VerifiedBootDxe -s 0xd000 -r -O ...`，dump 时已打 `-6`）。
- `sub_1494` = `VBDeviceInit`：
  ```
  v4 = *a2; word_C258 = *a2 | (a2[1]<<8);
  if (v4) { dword_C190 = 1; "Device is unlocked! Skipping verification!"; }
  else     { dword_C190 = 0; }
  ```
  即**解锁状态下 AVB 校验直接跳过**（ABL 日志实测打印了这句）。
- `sub_2460` = VerifyImage/GetBootState：
  - 0x252C 打印 `"VB: VerifyImage: Image verification done! boot state is:"`，
    0x2544 `CMP W8,#3` → 0x254c 跳转表 → GREEN/ORANGE/YELLOW/RED/UNDEFINED。
  - 0x25F0 `LDR W8,[X25,#dword_C190]`；0x25F8 `CMP W8,#1`；
    **0x25FC 被 `-6` 改成 `B loc_26F0`**（原始为条件分支）；0x26F0
    `MOV X22,XZR; STR WZR,[X25,#dword_C190]; B loc_250C` → 返回成功。
  - Hex-Rays 在 `sub_2460` 报 `switch information is incomplete or
    incorrect (0x2558)` —— 正是补丁破坏跳转表所致，反向印证补丁点。
- **反汇编流程（P_114，已打 -6）**：
  ```
  0x2498 BL sub_4988                 ; 取时间
  0x24A8 TBZ [byte_C25A],#0,0x24FC   ; 未初始化 -> 0x50 错误
  ; 参数/大小校验 -> 0x25F0
  0x25F0 ADRP X25, dword_C190
  0x25F4 LDR  W8, [X25,#dword_C190] ; 设备状态：1=unlocked
  0x25F8 CMP  W8, #1
  0x25FC B    loc_26F0               ; <- -6 补丁（原始为条件分支）
  0x2600 MOV  X22, XZR ; B 0x250C    ; unlocked 时的成功短路
  0x2608..0x26EC                     ; AVB 校验主体（XBootResetSignerCert /
                                     ;   VBVerifyImage / signer certs）
  0x26F0 MOV  X22, XZR               ; 补丁目标：status=SUCCESS
  0x26F4 STR  WZR, [X25,#dword_C190] ; boot state = 0 (GREEN)
  0x26F8 B    0x250C                 ; 打印 "VerifyImage done! boot state is:"
  0x252C 打印 boot state；0x2544 CMP W8,#3 -> GREEN/ORANGE/YELLOW/RED/UNDEFINED
  ```
  即 `-6` = **跳过 0x2608–0x26EC 的 AVB 校验块，强制 boot state=0(GREEN)、
  返回 EFI_SUCCESS**。解锁时（dword_C190=1）本来就会在 0x25FC 短路成功；
  `-6` 是为锁定状态准备的。
- 结论：解锁后 `-6` 不是必需（DeviceInit 已跳过校验）；锁定状态下
  `-6` 才是跳过校验的关键。`-x -4`（LinuxLoader boot 命令锁检查）仍必需。

## 6. "电池标志画面" = XBL 的充电模式 UI（不是崩溃/杀手）

XBL 内包含完整的充电应用与素材：

- `QcomChargerApp`：充电模式 UEFI App，`QcomChargerApp_Entry` /
  `MonitorCharging` / `DisplaySignOfLifeOnVBat` / `KeyPressHandler`；
- `QcomChargerDxeLA`：`ChargerPlatform_CheckIfOkToBoot`（按电池电压/SoC 阈值
  判断是否允许启动 HLOS）、`ChargerPlatform_TakeAction`、
  `EmergencyShutdownVbatt`、`CHGAPP_RESET_SHUTDOWN/COLD` 等；
- 素材：`logo_sony_720_1280.bmp.gz`、`logo_charging_600_1024.bmp.gz`、
  `logo_no_charging_600_1024.bmp.gz`、`battery_symbol_Soc10.bmp`、
  `battery_symbol_NoBattery.bmp`、`battery_symbol_LowBattery*.bmp`、
  `verifiedboot_state_orange/red*.bmp.gz`；
- 配置：`QcomChargerCfg.cfg`（CfgVersion 25，8998）、`BATTERY.PROVISION`
  （2800mAh profile）、`BDS_Menu.cfg`；
- 重置原因：`StartFlagDxe`（`GetSonyResetReason` / `XResetReasonApi_IsWatchDog` /
  `IsS2Reset` / `IsWarmReset` / `IsHardReset`）、`XResetReasonDxe`、`BootCounterDxe`。

**机制**：任何 PMIC 级断电（PSHOLD / 看门狗硬复位）后，只要 USB 还插着，
PMIC 会被 VBUS 重新上电，XBL 的 BDS/Charger 逻辑把这次启动归类为
"充电器启动" → 运行 `QcomChargerApp` → 显示 Sony logo + 电池/充电图标
（Android cmdline 里 `startup=0x00000004` 就是这个状态）。
**按住电源键约 2 秒** `KeyPressHandler` 才会退出充电 app 启动 Android。
Android 侧的 `chargemon` 随后接管同一套充电 UI（E1F4 是它的 USB 组合）。

**结论**：电池画面 = "带 USB 的冷上电/充电启动"的正常 UI，不是 shim 被
某种 10s 定时器杀死的证据。要避免：
1. **断电前拔掉 USB**，再上电 → XBL 直接走正常启动；
2. 或者让 shim 用 `RR=0x77665500 + PSCI SYSTEM_RESET` **自复位回 fastboot**
   （beacon30 已验证），根本不做 PMIC 断电；
3. 已经进了电池画面时，按住电源键 2 秒即可进 Android。

## 6.5 SPMI 写不落地的排查（2026-10-23 晚）

- `shim/spmi.c` 的裸序列与内核 v3 驱动一致（map=core+0x800+4n、
  ch=stride 0x8000*apid、CMD/STATUS/WDATA/RDATA 偏移、OP_WRITE=14/READ=15），
  且已加 AHB dummy read，但 PMIC spare 仍写不进（SID0 0x88e / SID2 0x88e
  实测全 0，即使 beacon 已确认执行并 PSHOLD）。
- XBL SPMI DXE（`xbl-O82-SPMI.pe`）反汇编确认：
  - arbiter base 0x08000000（uefiplat.cfg "PMIC ARB SPMI" 0x02800000）；
  - EE 偏移 `<<12`（0x1000*ee，AP 为 EE0）；
  - channel = `0x8000*apid`；chnls=base+0x400000、obsrvr=base+0x1400000；
  - 所有权表 `cnfg+0x700+4*apid`（低 3 位 = write EE）。
- **已构建诊断模块 `research/kos/spmidump.ko`**（源码 `/root/sdmod/spmidump.c`）：
  ioremap core/cnfg/chnls/obsrvr，dump 512 项 APID 映射 + 所有权，并用与
  shim **相同的裸序列**对 SID0/SID2 PON 0x88e 做写+回读，结果打到 dmesg
  （前缀 `SPDUMP`）。手机回 Android 后：
  ```
  adb push research/kos/spmidump.ko /data/local/tmp/
  { sleep 5; printf 'insmod /data/local/tmp/spmidump.ko\ndmesg | grep SPDUMP\nexit\n'; } \
    | adb shell /data/local/tmp/bindershell
  ```
  预期：能列出 ppid 0x008/0x208 的 apid/owner，并给出 wstatus/readback。
  若模块的裸写成功而 shim 失败 → 差异在 shim 的执行上下文/APID 扫描；
  若模块也失败 → 序列/APID/EE 仍有错。

## 6.6 无人值守测试结果（2026-10-23 20:1x）

- beacon40（标准 fastboot boot + 2bit 延迟 + RR+SYSTEM_RESET）：第一发
  未跳，第二发执行，但**没有自复位回 fastboot**，最终落 E1F4 充电模式。
  → `RR=0x77665500 + PSCI SYSTEM_RESET` 在标准 boot 路径下不可靠
  （beacon30 的自复位结论需重验）；时序遥测不能依赖它。
- 手机停在充电模式（需按 2 秒电源键回 Android）。按约定已停止设备测试。

## 7. 回家后的执行清单（1-6，含 IDA 分析）

> 手机进 fastboot（蓝灯）后，全部在**同一个 fastboot 会话**里做；补丁重启即失效。

### ① 打补丁（每次会话必做）
```
cd Patch\Xperia\unlock
.\xperable.exe -B -U -s 0xf49880 -x -4 -6      # 两句 patched successfully
```

### ② 模块基址（IDA 定位用）
```
.\xperable.exe -m
```
记录 `LinuxLoader` / `VerifiedBootDxe` / `QcomBds` / `StartFlagDxe` /
`QcomChargerApp` 的基址。**IDA 预测**：VerifiedBootDxe 的 `GetBootState`
（RVA 0x25E8）在 0x25FC 是 `tbz w8,#0,...`。

### ③ ABL 日志（解释失败/充电判定）
```
.\xperable.exe -l | more
```
重点 grep：`startup`、`charger`、`boot`、`verified`、`unlock`、`Command not allowed`。
若能看到 boot 命令被拒的日志，直接定位是哪道门。

### ④ 从 RAM dump P_114 VerifiedBootDxe（不用 dd 分区）
```
.\xperable.exe -M VerifiedBootDxe -s 0xc000 -r -O research/VerifiedBootDxe-P114.pe
.\xperable.exe -M LinuxLoader -s 0xf0000 -r -O research/LinuxLoader-P114-ram.pe
```
然后 **IDA 打开 `VerifiedBootDxe-P114.pe`**：
- 跳到 RVA `0x25FC`，确认原指令 `tbz w8,#0,...`（`e8 00 00 36`）；
- `xrefs_to` 0x25FC 的调用者与 .data+0x290 标志位写入者；
- 找 `VBVerifyImage` / `XBootResetSignerCert` / `SecurityDxe` 的调用链；
- 与 `xbl-O82-VerifiedBootDxe.pe` 对比（同构则偏移一致）。
另外用 `objdump -d` 校验：`0x25FC -> b 0x26F0`（`3d 00 00 14`），
`0x26EC -> mov x19,xzr`，`0x26F0 -> b 0x26DC`。
`LinuxLoader-P114-ram.pe` 与本地 `LinuxLoader-p114.pe` 应一致（hash/节表）。

### ⑤ 触发标准 fastboot boot 失败 + 立刻读日志
```
fastboot boot beacon1f.img        # 第一发（可能只暂存）
fastboot boot beacon1f.img        # 第二发（异常）
.\xperable.exe -l | more          # 读刚才失败的日志
```
**IDA 对照**：若日志显示 "Command not allowed" → 锁门；"Failed to
load/authenticate boot image" / "VBVerifyImage failed" → AVB 门。
再结合 `sub_281FC`（0x281FC）与 `sub_80BC`（0x80BC）的代码路径判断。

### ⑥ 全量对照（可选，但最稳）
手机进 Android，bindershell root：
```
ls -l /dev/block/bootdevice/by-name/ | grep -Ei 'abl|xbl'
dd if=/dev/block/bootdevice/by-name/abl of=/data/local/tmp/abl.img
dd if=/dev/block/bootdevice/by-name/xbl of=/data/local/tmp/xbl.img
chmod 644 /data/local/tmp/abl.img /data/local/tmp/xbl.img
```
pull 后用第 5/6 节流程提取 P_114 的 LinuxLoader / VerifiedBootDxe，
与 ④ 的 RAM dump 互相校验。

### 电池画面（V2，随时可做）
- 插 USB：`adb reboot -p` → 上电 → 电池图标（`startup=0x4`）；
- 拔 USB → 上电 → 直接 Android。
- IDA 目标：`QcomChargerApp_Entry` / `KeyPressHandler`（0x2s 电源键）、
  `ChargerPlatform_CheckIfOkToBoot`（电压/SoC 阈值）、
  `StartFlagDxe` 的 `GetSonyResetReason` / `IsHardReset` / `IsWatchDog`。
  目标：确认 PSHOLD+VBUS 被归为哪种 reset reason → 走充电 App。

### 自复位（V3，已初步验证）
beacon30（`RR=0x77665500 + PSCI SYSTEM_RESET`）→ 自己回 fastboot，
免按键收尾；后续测试优先用它。

## 8. 待挖（可选）

- 从 `bootloader_...O_82_...sin`（已解压为 `bootloader110F.bin`，
  `bootloader.cms`）里提取完整 ABL / VerifiedBootDxe 模块，对照分析
  `-6` 补丁点和 `VBRwDevice` 的存储分区。
- P_114 完整 ABL 可从固件 `47.2.A.11.228` 的
  `boot/bootloader_X_BOOT_MSM8998_LA2_0_P_114_X-FLASH-ALL-C93B.sin` 提取
  （xperable Makefile 有说明），或从手机 `abl` 分区 dd 出来（只读）。
