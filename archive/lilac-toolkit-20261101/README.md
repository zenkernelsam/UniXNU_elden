# lilac-toolkit — lilac 战场淘来的调试神器（elden 适配版）

> 2026-11-01 从 `Patch\Xperia\` 精选收编。lilac 没有串口，全套"屏幕+相机+USB"调试体系
> 是在零输出环境里硬磨出来的——**elden 同样无串口，这套装备直接是刚需**。
> 方法论总纲在 `research\lilac-archive\METHOD-DEBUG-PORTABLE.md`（先读那个）。

## 分类索引

### `screen-capture\` — DroidCam 屏幕抓图（⭐ 无串口环境的第一器官）
- `CAMERA-PROTOCOL.md`：**唯一正确流程** `override→af→2s→MJPEG 流→抽帧`，连拍单帧是错误用法
- `grab_stream.py/.sh`：MJPEG 流抓帧主力；`grab_cam.py` 已**弃用**（协议里写着，留档）
- **elden 适配**：平板大 12.1"，相机距离要调；UniXNU 菜单/DarwinBoot 输出全靠它读
- **TODO**：DroidCam IP 到时候按 elden 实测改（lilac 是 `192.168.68.60:4747`）

### `unattend\` — 无人值守发射+收割流水线（⭐ elden 点名要的体系）
- `harvest.sh/harvest-pon.sh`：COM 口一开即抓、定时窗口收割日志
- `unattended-loop.sh`：发射→等待→收割→回 fastboot 全自动循环
- `watchdog.sh` + `usbctl.ps1` + `install-usbctl-task.bat`：**USB 继电器硬重启**（卡死自动断电复位）
- `arm-pon-fastboot.sh/arm-xnu*.sh`：PON 按键脚本（电源键 GPIO 当重启开关）
- `run_v238.sh/fire_once.sh/wait_and_fire.sh`：单发执行模板（fastboot boot + COM 即抓）
- **elden 适配**：COM 口→QcomXnuUsbDebug CDC 或 ESP 读回；PON=PM8550 系 SPMI 键码（待补常量表见 METHOD-DEBUG §193）；usbctl 继电器即插即用
- **纪律**：lilac 血泪换来的——一次只发一发、等 fastboot、超时不重发、route 只用 0x06（0x86 进 Android 的坑 elden 对等项目是"测试镜像写错槽"）

### `panic-decode\` — panic/assert 解尸工具
- `panic_caller.py/panic_hash.py/panic_strings.py`：从屏幕 panic 串定位 caller/模块
- `assert_decode/assert_enum/assert_modscan`：assert 站点枚举+解码
- `phase_hash.py/pon_steps.py`：阶段哈希+启动步骤编号（快速定位挂在哪一步）
- **elden 适配**：XNU panic 格式跨设备同构，直接可用；配 screen-capture 抓 panic 屏

### `boot-image\` — boot 镜像手术刀
- `repack_kernel_boot.py/repack_boot.py`：内核 payload 注回 boot 镜像（lilac v4 格式）
- `mkimg_swap_payload.py`：镜像内 payload 换件不重打包
- `raw2simg.py`：raw↔sparse（super/edl 物件转换）
- **elden 适配**：stock boot=v4 格式；Jerry 探针镜像是现成 boot.img——**`fastboot boot` 前 testkey 签**就行，一般不用重打包；真要自造镜像时改 header version/pagesize 参数

### `devicetree\` — 设备树/ADT 解剖
- `fdt_dump.py`：FDT 全 dump；`dump_adt_cpus.py`：CPU 拓扑
- `dis_kva.py`：kernel VA→文件偏移反查
- **elden 适配**：⭐ **重点**——Jerry 探针 ADT 是占位符（chip-id=1），我们上真机要真 ADT；`tb322fc-linux-kernel` 的 `sm8750-lenovo-elden.dts` 是翻译原料，lilac 有 dtb→adt 翻译先例可翻

### `ida\` — 逆向工程套件
- `IDA-BOOTLOADER-NOTES.md`：abl/XBL 逆向方法论（**下次审 abl 二进制就用它**）
- `ida_dump_range.py`/`locate_*`：段 dump+符号定位；`mcp_client.py`：IDA-MCP 桥
- **elden 适配**：abl_old.img(`92551b2e`) 逆向核它的 fastboot 命令集/`fastboot boot` 是否开放——METHOD-DEBUG 的待办

### `gpu-probe\` — GPU 状态抓取（Adreno 族通用方法）
- `kgsl-dump.sh`：Android 下 kgsl 寄存器快照；`twrp_kgsl_probe.sh`：recovery 态探测
- `run_gpu_test.sh`：GPU 测试发射模板
- **elden 适配**：基址全换（A830≠A540，参照 `tb322fc-linux-kernel` DTS + data-a-live）；**方法论直接抄**——lilac 学到最痛的一课是"读原厂驱动留下的活状态"比"自己摸索寄存器"快 10 倍

### `ios-rootfs\` — iOS rootfs 打包（wsl-tools 全套）
- `mkiosroot.sh`：IPSW dmg→可挂 rootfs 流水线
- `harvest.py/harvest2.py`：rootfs 文件收割；`gensymvers_final.py`：符号版本
- **elden 适配**：iPadOS 21H461 IPSW（Pagani 同款）挂载流程直接可用

### `docs\` — 方法论卷宗
- `GPU-CP-FETCH-RECIPE-20261029.md`：Adreno CP 取指全配方（sagit 血统，A830 换址换固件但流程同构）
- `GPU-HANDOFF-LILAC-PLAN.md`：GPU 交接计划模板
- `DPU-SCANOUT-PLAN-20261029.md`：显示直扫计划（SM8750 DPU 参照 nativemobiledisplay）
- `TONIGHT-RUNBOOK.md`：**发射runbook模板**——六发序列/判定树/三件套记录格式，Phase8 测试夜照抄格式

## 没抄的（lilac 专属，过来也是废铁）
- `research/patch_v*.py` ×200+：lilac 内核定点 patch，址全绑 MSM8998
- `research/make_v*.sh` ×150+：lilac 镜像构建流水线（elden 用自己的）
- `fix_*.py`、`apply_*.sh`：lilac 一次性补丁
- lilac 仓 shim/kernel 源码本体：在 `D:\Desktop\Git\ios-xperia-xz1c`，要参考去那读
