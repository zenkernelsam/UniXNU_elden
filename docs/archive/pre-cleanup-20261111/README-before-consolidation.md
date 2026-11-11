# UniXNU-elden

Lenovo Legion Y700 Gen 4（TB322FC / elden / SM8750P），已实机运行 iPadOS 17.7.11（21H461）。

2026-11-10：UCSI完整通知初始化和双口事件ACK已实机修复，两组测试正常回程并恢复Android。
后续电源基线使用ucsi-init2；共享500mA输入预算仍待处理。见[UCSI事件流程](docs/UCSI-EVENTS-20261110.md)。

2026-11-10：原厂充电固件ULog已在XNU只读接通，两页完整CRC通过；9V与自动回程保持，已恢复Android。
见[固件日志通道](docs/CHARGER-ULOG-20261110.md)。双口防倒灌/最大快充仍未完整验收。

2026-11-09 PD续轮：原厂GET确认第二口5V/3A合同及缺失能力位的兼容性问题；
原生PDO/RDO只读候选已签、尚未刷，见[PD合同续档](docs/PD-CONTRACTS-20261109.md)。
Lenovo55已核为强制关机相关处理，未用作PD选路。

2026-11-09温控执行层：真实两路GPIO控制、合格温度缓存、故障闭锁/恢复和停止接口已原生构建。
只读检查/保护测试候选已签，尚未上机；见[温控保护续档](docs/THERMAL-PROTECTION-20261109.md)。
双口PD净充电和新物理保护仍待验收。

2026-11-09 ADC续轮：原厂两口温度请求/满量程已由实机SPMI追踪确认，
原生读取后端和三份诊断候选已编译/签名，尚未刷入；见[ADC温控续档](docs/ADC-THERMAL-20261109.md)。
温控GPIO执行和双口PD选路仍未完成。

2026-11-09续轮：修正了ADSP初始默认电量/电流误报，生产C回放71轮实机数据后，
真实电池发布候选与关闭对照已编译/签名，尚未刷入；见[电量发布续档](docs/BATTERY-PUBLICATION-20261109.md)。

2026-11-09：AW99706的GENI后端与原生AppleARMBacklight服务已编译、重链和签名，
芯片检查/调暗恢复/亮度服务三份候选均通过主机核验，尚未上机。
见[背光续档](docs/BACKLIGHT-RUNTIME-20261109.md)。[iPad Pro无Home键候选](docs/IPAD-PRO-GESTURES-20261109.md)
也已签好；导航条/上滑仍待实测。平板保持原厂Android补电，双口PD净充电仍未完成。

2026-11-08续轮按电池/双口充电、触控、Wi-Fi、蓝牙优先。原厂PMIC链已加入原生电池发布器，
完整周期/失联回退/净充电判断通过主机检查及iOS17重链；两份stock-index候选已签，尚未上机。
发布须先核OEM单位/电流方向，真实快充与双口净输入未验；见[电池续档](docs/BATTERY-TELEMETRY-20261108.md)。
电源验收后保留M3并合入触控；无线仍按各自硬件关口推进。[蓝牙主机桥](docs/WCN-NATIVE-20261108.md)已离线编译，daemon/配对未验。

2026-11-08夜班：已恢复原厂Android补电，当前仅离线构建；明天再恢复UniXNU实测。
9组iOS17外设核心与完整51-object Adreno830独立archive已构建/核验，尚未链接或启用硬件。
详情见[离线外设构建](docs/NIGHT-OFFLINE-20261108.md)。以下11-07为已验iPadOS控制件的结果。

本轮随后已把完整Elden ADSP／PMIC原固件传输链接进新XNU，并生成三档独立签名候选，
存储/HID原绑定与原Apple执行SHA保持；实际充电、双口与IRQ仍未上机验证。
见[ADSP／PMIC链](docs/ADSP-PMIC-RUNTIME-20261108.md)。平板继续留在Android补电。

双口UCSI查询和45项原始供电字段也已独立编译/重链/签好，未上机。
原ROM约束、GPIO数据选择、属性55及实际测试前置见[双口UCSI](docs/UCSI-DUALPORT-20261108.md)。

同机NT36536 PIO/SRAM上传及独立HID也已完整编译/预链接，五档stock-index候选已签，未上机。
坐标和原21H461私有220/37槽已核，保留USB/自动回程；见[物理触控准备](docs/NT36536-RUNTIME-20261108.md)。
真实FIFO/GPI/触点与电源仍待资格，平板继续留在Android充电。

持久Data固定尾部布局、主备GPT验证、原UFS owner内的有界写/同步/读回与复位fence也已完成离线重链，
两份只读候选已签且无写入授权；见[持久Data前置](docs/PERSISTENT-FOUNDATION-20261108.md)。
真实落盘、加密/Keychain与崩溃恢复尚待资格，旧RAM Data与已验GUI资产保持。

WCN7861外围/TME-L启动核心与只读UART14调查也已离线重链，12个真实固件模型场景通过；
两档候选未刷，真实蓝牙与native HCI尚未接。见[蓝牙前置](docs/WCN7861-STARTUP-20261108.md)。

2026-11-07晚：原始OS/SystemOS、可写512MiB RAM Data、launchd和SpringBoard已运行；
原生IOHIDEventDriver匹配自有虚拟输入源，PC会话绑定的Home按下/松开使实际画面从锁屏进入主屏。
本轮420秒、136次状态采样、USB dropped0、无kernel panic。详见[Home实机验收](docs/HID-HOME-LIVE-20261107.md)。
真实触控、持久Data、GPU、无线、音频、电源/时间与睡眠仍按日用对齐计划推进。

当前使用6GiB高地址窗口与固件GOP CPU呈现，保留8秒倒数自动入核、USB日志/画面回读、
panic后180秒定向fastboot以及主动RPC回程。画面有白色widget和壁纸/条带瑕疵，尚非完整显示验收。
当前RAM KeyStore状态兼容层仅服务无保护临时Data，不提供日用安全存储。

日用候选使用经过审计的独立增量构建与签名流程；`board.toml`的原phase1默认配置继续作为早期恢复基线。
每次记录kernel/ADT/Data/trust/FD/shim指纹、组件与原始21H461 ABI。boot签名固定1738713600@footer0，
父链boot位置3，设备写入按所有者已授权的工具流程做完整读回与分区身份检查。

阶段1菜单/音量键/FAT、阶段2交接和早期检查点资料保留在下文；它们是历史构建与定位记录。
当前启动与输入状态以上面的实机验收及最新porting记录为准。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| [`board.toml`](board.toml) | 格式 2 的四层配置选择、`elden` 板级、固件和载荷参数 |
| [`feeds.toml`](feeds.toml) | SM8750 SoC 来源；当前为可审计的本地过渡 profile |
| [`dts/elden.dts`](dts/elden.dts) | 板级内存和 SoC DTSI 引用 |
| [`soc/sm8750/`](soc/sm8750/) | 正式共享 SoC 仓发布前的过渡 profile |
| [`configs/`](configs/) | XNU defconfig 与阶段 0 恢复资产清单 |
| [`drivers/`](drivers/) | 仅存放设备专属驱动，目前为 `EldenRamDiskDxe` |
| [`package/`](package/) | UniXNU 菜单和载荷配置 |
| [`patches/`](patches/) | 设备 XNU 补丁序列和固定版本第三方源码覆盖 |
| [`scripts/`](scripts/) | 依赖准备、固件构建、载荷生成和产物检查 |
| [`kits/`](kits/) | 后续设备 kit 的预留目录 |
| [`docs/`](docs/) | 移植分析、阶段状态和交接文档 |
| [`assets/`](assets/) | 实机验收截图等仓库资源 |
| [`porting.toml`](porting.toml) | 统一维护者与 agent 适配记录 |

完整的 `third_party/` 源码、`.venv/`、`.cache/` 和 `out/` 构建产物均为
本地文件且不会提交到 GitHub。

## 文档

- [阶段 0：恢复路径](docs/PHASE0.md)
- [阶段 1：构建与测试](docs/PHASE1.md)
- [阶段 1：实机验收状态](docs/PHASE1-STATUS.md)
- [阶段 2：实施计划与验收关卡](docs/PHASE2-HANDOFF.md)
- [阶段 2：主机状态与基线](docs/PHASE2-STATUS.md)
- [阶段 2.1：FDT、ADT 与内存审计](docs/PHASE2.1-STATUS.md)
- [阶段 2.2：可追溯的最小 XNU](docs/PHASE2.2-STATUS.md)
- [阶段 2.3：大载荷 RAM 通道（主机侧）](docs/PHASE2.3-STATUS.md)
- [阶段 2.4：DarwinBoot 交接与内核入口探针](docs/PHASE2.4-STATUS.md)
- [阶段 2.5：XNU 入口检查点与平台依赖](docs/PHASE2.5-STATUS.md)
- [完整移植分析](docs/porting-analysis.md)
- [格式 2 迁移记录](docs/FORMAT2-MIGRATION.md)
- [第三方源码版本](docs/third-party.md)
- [第三方版本锁](unixnu.lock)

## 准备

从 GitHub 克隆后，在仓库根目录执行：

```sh
python3 scripts/bootstrap_third_party.py --clean-build-deps
```

该脚本克隆固定版本的 UniXNU 和 `mu_wide_platforms`，并应用
`patches/overlays/` 与 `drivers/` 中的项目修改。完整 EDK II 构建还需要
初始化上游子模块：

```sh
python3 scripts/bootstrap_third_party.py --init-submodules
```

参考资料仓可通过 `--all-references` 一并获取。当前验证环境是 Intel macOS
、MacPorts LLVM/LLD 20、Python 3.12 和 mtools。

## 格式 2 配置验证

格式 2 解析器位于当前 UniXNU 主仓。首次解析会复用仓库内的
`soc/sm8750` 过渡 profile：

```sh
PYTHON=.venv/bin/python
UNIXNU_DIR=../UniXNU

"$PYTHON" "$UNIXNU_DIR/unixnu" defconfig elden_defconfig -D "$PWD"
"$PYTHON" "$UNIXNU_DIR/unixnu" status --json -D "$PWD"
"$PYTHON" "$UNIXNU_DIR/unixnu" tree -D "$PWD"

"$PYTHON" "$UNIXNU_DIR/scripts/porting-record.py" check --device "$PWD"
```

硬件 DTS 可独立编译：

```sh
dtc -I dts -O dtb -i soc/sm8750/dts -o /tmp/elden.dtb dts/elden.dts
```

首次 `tree` 会初始化共享组件子模块；若 `status` 提示组件补丁缺失，先运行 `tree`
再重试。源码树可组装不代表 XNU reference、SDK、kernelcache 和 ABI 输入已齐备，
内核构建前仍需完成对应准备。当前 `third_party/UniXNU` 仍固定为阶段 1 已验证的提交，因此阶段 1 构建继续使用
`scripts/build_phase1.py`，不会因迁移改变已验收镜像的构建路径。

## 阶段 1 构建

```sh
cd UNIXNU_elden
source .venv/bin/activate
export PATH="/opt/local/libexec/llvm-20/bin:/opt/local/bin:/opt/local/sbin:$PATH"
python scripts/build_phase1.py --prepare-only --skip-submodules
```

完整构建需要已验证的 Crypto 归档：

```sh
python scripts/build_phase1.py \
  --skip-submodules \
  --crypto-path "$PWD/.cache/phase1/basecrypto"
```

产物写入 `out/`。现行阶段 1 基线镜像 SHA-256：

```text
94b6a6315b04fc9d276bc15e9519107be292244380fc3bf139f5af6c96a3bb0a
```

历史验收镜像 `b56ef4b7…` 的原始字节未保留。详细命令和排查步骤见
[阶段 1 构建与测试](docs/PHASE1.md)。

## 阶段 2.3 构建

阶段 2.3 使用独立的 `out/phase2.3/` 输出，不覆盖阶段 1 基线：

```sh
python scripts/build_phase2_firmware.py \
  --crypto-path "$PWD/.cache/phase1/basecrypto" --jobs 6
python scripts/make_phase2_probe.py
python scripts/build_phase2_image.py --kind probe
python scripts/build_phase2_image.py --kind boot
python scripts/verify_phase2_artifact.py --kind probe \
  --image out/phase2.3/elden-phase2-probe.img
python scripts/verify_phase2_artifact.py --kind boot \
  --image out/phase2.3/elden-phase2-boot.img
```

`payload.img` 由 UniXNU CLI 生成（需要 `dtc` 和 `--unique-chip-id`）。阶段 2.3
的设备步骤与哈希见 [阶段 2.3 状态](docs/PHASE2.3-STATUS.md)。

## 阶段 2.4 入口探针

阶段 2.4 复用阶段 2.3 的固件，只替换载荷里的内核：探针在入口画一条蓝色色带
再跳回内核，用来区分"XNU 入口执行"与"交接后挂住"。

```sh
python3 scripts/make_entry_probe_kernel.py
python3 scripts/make_phase2_payload.py   --output out/phase2.4/payload.img   --kernel out/phase2.4/probe/kernel.macho   --adt out/phase2.4/probe/kernel.macho.adt   --kernel-bytes 54050912   --kernel-sha256 a8f04b455996a4656a3055254c127e42f0676187c9550aa0b5fa57bbbe94e2d9
python3 scripts/build_phase2_image.py --kind boot   --fd out/phase2.3/SILICIUM_UEFI.fd   --payload out/phase2.4/payload.img   --output out/phase2.4/elden-phase2.4-probe-boot.img   --embed-payload --work out/phase2.4
python3 scripts/darwinboot_preflight.py   --kernel out/phase2.4/probe/kernel.macho   --adt out/phase2.4/probe/kernel.macho.adt
```

产物、判读表和设备步骤见[阶段 2.4 状态](docs/PHASE2.4-STATUS.md)。

## 阶段 2.5 检查点探针

MMU 打开后帧缓冲不再映射，屏幕不能继续当输出。阶段 2.5 改用"复位即检查点"：
`scripts/make_entry_probe_kernel.py` 把目标函数入口或任意指令地址原地替换成
`等 2 秒 → PSCI SYSTEM_RESET` 的 stub；设备在绿条后约 2 秒自行重启说明内核
到达该检查点，一直冻结说明没到。`--ret-at` 可把已知的 Apple 平台依赖（如
iBoot console）直接改成 `ret` 跳过。

```sh
# 示例：跳过 PE_create_console，复位放在 _machine_startup
.venv/bin/python scripts/make_entry_probe_kernel.py \
  --output-dir out/phase2.4/v31 \
  --reset-at _machine_startup \
  --ret-at 0xfffffff007941dec \
  --clang /opt/local/libexec/llvm-20/bin/clang
# 再用 make_phase2_payload.py / build_phase2_image.py / verify_phase2_artifact.py
# 生成镜像，参数与阶段 2.4 相同，只替换 --kernel 和额外字节数/SHA 断言。
```

检查点清单、每个镜像的 SHA-256 与反汇编依据见
[阶段 2.5 状态](docs/PHASE2.5-STATUS.md)。探针内核不是可启动系统，只用于
定位；跳过 Apple 平台初始化的镜像不能作为阶段 2 的交付产物。

## 设备支持

| 功能 | 当前状态 | 证据和边界 |
| --- | --- | --- |
| UEFI / BootShim / FAT / 音量键 | 实机通过 | 固定旧FD、56MiB payload、菜单与倒数 |
| XNU / boot CPU / 内存 | 实机通过 | 原始21H461，6GiB窗口；扩大内存与多核另验 |
| UFS / OS / SystemOS | 实机通过 | 只读根窗口与原始APFS挂载；写入与持久Data另验 |
| 用户态 / SpringBoard | 实机通过 | 原launchd、锁屏与Home主屏，真实帧回读 |
| USB日志 / 回程 | 实机通过 | 同会话PC日志、RPC与panic后180秒SDAM回fastboot；插拔可靠性仍需完善 |
| 虚拟HID Home | 实机通过 | 原生IOHIDEventDriver、按下/松开、主屏变化 |
| 实际NT36536触控 | 进行中 | 点报告协议已主机核验，SPI4/IRQ/固件待实机整合 |
| GOP CPU显示 | 部分通过 | 有真实提交/完成/图像；白块、壁纸和条带仍待修 |
| Data保存 / Keychain | 待完成 | 当前只有512MiB临时无保护RAM Data |
| GPU / 网络 / 音频 / 其它外设 | 待完成 | 按日用功能Plan逐项验收 |

## 阶段 2 推进顺序

按[实施计划](docs/PHASE2-HANDOFF.md)依次通过：**2.0 基线与配置 →
2.1 硬件树/内存与 2.2 内核输入（并行）→ 2.3 大载荷 RAM 通道 →
2.4 DarwinBoot → 2.5 XNU 入口、平台专家和 boot CPU**。

2.0 的结果见[阶段 2 状态](docs/PHASE2-STATUS.md)，2.1 的结果见
[阶段 2.1 状态](docs/PHASE2.1-STATUS.md)，2.2 的结果见
[阶段 2.2 状态](docs/PHASE2.2-STATUS.md)。历史验收镜像的原始字节
未保留；当前 `out/elden-phase1.img` 已由设备所有者完成两次 RAM 冷启动复核，
成为现行阶段 1 基线。
阶段 2.3 的嵌入载荷通道、阶段 2.4 的 DarwinBoot 交接与阶段 2.5 的入口检查点
都已取得设备证据。该阶段的历史结论：入口执行、`arm_init` 全程与线程启动都已确认；
当时内核死在 `kernel_bootstrap_thread` 第 16 号 `kernel_startup_initialize_upto`
里程碑内部。下一步先做 console 补丁（iBoot 交接区缺失时改用 `boot_args`
的 `Video[0..5]`），让内核日志能上屏，再继续二分该里程碑直到 `_bsd_init`。
DarwinBoot 的输入校验日志不计为 XNU 验收。

## 迁移边界

- `components.include` 只选择第一里程碑需要的平台、CPU、AES、预链接和
  DarwinBoot 组件；UFS、显示、触摸、GPU、网络和音频在实现与验证前不虚报。
- `dts/elden.dts` 使用 UEFI `DXE Heap` 中的 256 MiB 连续 XNU 窗口、
  8 核 MPIDR、GICv3 和 PSCI 输入。`dts/elden-firmware-memory.dts` 保存脱敏的
  固件 RAM／保留区描述；UFS、显示和触控节点仍待后续适配。
- `soc/sm8750` 是过渡目录，用来在没有公开
  `UniXNU-sm8750-common` 时保持格式 2 可解析。发布前必须迁入共享仓并在
  `feeds.toml` 固定 40 位提交，不能把本地目录当作发布来源。

## 安全边界

- 所有设备启动只由设备所有者本人执行；仓库不提供启动自动化。
- 阶段 1 只允许 `fastboot boot <image>` RAM 启动。
- 禁止刷写、擦除、格式化、槽位切换、分区或 GPT 修改。
- 每次测试记录镜像 SHA-256、构建来源和测试时间。
- 同一镜像至少两次 RAM 启动结果一致后，才继续后续调试。
- 不提交序列号、唯一芯片 ID、恢复备份、密钥或完整固件备份。

