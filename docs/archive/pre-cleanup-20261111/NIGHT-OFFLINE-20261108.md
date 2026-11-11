# Elden离线外设构建（2026-11-08）

所有者将今夜改为离线准备，平板已恢复原厂Android补电；明天恢复UniXNU逐项测试。
原SpringBoard/HID控制件、6GiB、原根/RAM Data、56MiB FAT、FD及自动回程配置保持。
新archive没有加入已验内核、没有更改board.toml组件选择，也没有启用新硬件。

`scripts/build-elden-offline-cores.py`编译9组独立ARM64/iOS17库，16个对象：
GENI SPI、ShengNovatekTouch协议、本机NT36536报告、PMIC transport、Lenovo GET-only、
GENI I2C、GPR/AudioReach、本机BT firmware VFS、UCSI编译接口。
archive采用LLVM ar rcsD，蓝牙loader复用实际BSD vfs_subr记录，避免libsa/BSD头冲突。

完整Adreno830另外构建51对象，archive SHA256
`447826dccaa162e241c939e355d51eed1e313ed40a8a049628b3027542889ebe`。
四个原始IOAcceleratorES函数32B指令与本机已验kernel一致；最终外部符号解析、IOKit注册和硬件启动未验。
staged accelerator保持手动初始化；其它metadata initializer还需最终链接审核。
GPU/GMU控制器03da0000与UFS控制器15000000范围独立，CB0同号不冲突，旧推断已纠正。
归属按基址+bank记录；GPU自身映射/固件/供电仍待本机资格，UFS现用bank保持。

`scripts/audit-elden-offline-archives.py`独立读实际ar/Mach-O结构，核对象/源码/归档SHA、
iOS17平台、initializer、确定性元数据及本机原始入口指令。它只读普通文件，不链接或调用设备工具。
详细资产、来源、限制与明天顺序在Y700工作仓`research/NIGHT-OFFLINE-DELIVERY-20261108.md`。

原厂双电芯/双口PMIC接口、ADSP原签名输入已准备；GLINK transport和净充电仍待真机。
QcaBluetooth参考仅WCN7850，WCN7861外围处理器/TME-L、8Mbps和iOS17 native runtime要独立适配。
新增本机`drivers/EldenBluetooth/Wcn7861Peripheral`纯codec已编译1对象/3704B，archive SHA
`2c559df4a5472b93e4714adc8588afc15e9429fa87f59140b5dfedd7272d2f68`。
真实3份固件的ASan/UBSan1128项检查通过；整文件预检、243B分段、NVM副本调整、版本/完成/通知与current-baud规则已具备。
TME-L为RISC-V243，原文件保持；UART/供电/普通HCI启动与iOS17用户态仍未连接。
完整source/许可证与结果在工作仓research/wcn7861-peripheral-20261108及edl_work/night-wcn7861-20261108。
音频路由、功放校准/保护、Wi-Fi、传感器/触觉、持久Data与完整GPU都仍是后续工作。

boot签名1738713600/footer0/父链3保持；低电例外只恢复精确stock镜像，新实验仍须native电源准入。
所有者禁止QEMU；必要的主机格式/协议检查与实机资格分开记录。
