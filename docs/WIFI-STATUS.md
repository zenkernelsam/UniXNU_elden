# Wi-Fi：协议服务已离线构建，硬件链未验收

当前没有已验证的en0、扫描、关联或联网。触控v100/蓝牙v99候选保持原样，
用户离线限制仍有效。没有访问平板或启动Wi-Fi射频。

## 已完成的前置项

原modem_a七件主要Peach固件的只读文件访问已有实现，尺寸/SHA按原厂备份固定。
路径与板级资源在`drivers/EldenWifi`、`EldenWifiResources.h`及工作台
`lab/records/wifi-elden-20261108/inputs.json`，不是PCIe/MHI已启动的证据。

官方Ath12kWifi已克隆到Windows和Mac各自Y700项目的`research/Ath12kWifi`。
正式参考清单现为42仓。采用固定提交
`5f7f5a8a9f02f7ab45ed10a2f226e348c519a838`，不自动替换已有候选输入。
其README记录的实机范围是sheng/24A446上的开关、扫描、WPA2、DHCP及流量，
不能转记为本机或21H461已通过。

该版本默认用户态构建目标为arm64e/iOS27；新工具
`tools/build_elden_wifi_userspace.py`在独立输出目录取固定Git快照，改为arm64/iOS17。
只下载并验证4.25MB的wpa_supplicant2.12原版归档，未为用户态构建重复下载Linux/FreeBSD。
保留原sheng控制协议、原SME/WPA认证代码和已审核的注册/通知改动；另核899个原版文件逐字不变。
参考克隆未修改。重复输出拒绝覆盖，旧文件不删除。

已构建并本地签名的`unixnu-wpa_supplicant`：

- SHA256：`f54eb63475c7f3b3cb0d1d1ed78728a941fb8c07e363f88cf8c5095a69adecb7`
- CDHash：`b02572b5f0fd9e79790645ad33df708b24530295`
- 实际Mach-O为thin arm64、最低iOS17.0；仅链接libSystem.B.dylib，编译无警告/错误。
- 控制端点`org.unixnu.wifi.control`，原version1／40B头；尚未验证本机内核端点。

工作台材料`edl_work/wifi-userland-20261112`；Mac为项目内
`lab/wifi-userspace-20261112/build`。已生成原sheng形式的job和空网络配置模板，
**没有安装、没有合并launchd缓存或更新trustcache，也没有运行服务或提供网络密码。**
静态目标和签名检查不等于iPadOS加载、控制通道或认证实机通过。
完整来源见[服务记录](evidence/wifi-userland-20261112.json)。

## Peach必须使用本机Linux配置

ArmadaOS的TB322FC原始DTS明确固定到h0cheung提交
`6f97250ead155fc3b980856323e99a4623dae73d`。该源码的`ath12k/wifi7`有独立
PEACH0x110e条目，不需要将整个QCC2072配置当作猜测起点。

| 项目 | 本机Linux Peach | QCC2072条目 |
| --- | --- | --- |
| 硬件版本 | Peach HW2.0；拒绝未知SoC major | QCC2072 HW1.0 |
| internal_sleep_clock | false | true |
| download_tme_lite | true | 未设置 |
| board_api1_only | true | 未设置 |
| QMI能力位 | WLAN_EN、AUX_UC | QDSS_CFG_MISS、PCIE_PERST_NO_PULL、AUX_UC |

数据通路使用QCC2072 ops/描述符/寄存器，CE/MHI等复用WCN7850，**这些复用不等于整份硬件配置可互换**。
Pagani的`peach.py`明确标有未验证推测，README最近实机仍停在S7I读取失败，未完成网络初始化。
可借鉴拆分方法，不能直接复制其七rail、低地址DMA池、跳过读回或整片QCC2072映射。
后续应逐项移植本机独立Peach配置及其QMI/固件依赖，再验证实际PCI身份、供电、DMA/MSI和启动。
来源与字段对照见[Peach记录](evidence/wifi-peach-profile-20261112.json)。

## 复现与下一步

在macOS、Python3.12/Xcode环境运行，所有输出用新目录：

```sh
python3 tools/build_elden_wifi_userspace.py \
  --component-repository /path/to/research/Ath12kWifi \
  --supplicant-archive /path/to/wpa_supplicant-2.12.tar.gz \
  --output /path/to/new-wifi-userspace
python3 tools/analysis/audit_peach_profile.py \
  --repository /path/to/research/tb322fc-linux-kernel --output /path/to/new-evidence
```

归档URL/校验值取固定组件`tools/source-lock.json`。构建工具只修改隔离快照的目标/依赖准入和输出保护；
认证源由原组件准备器生成，未引入自制认证或假成功响应。还需原生网络/Apple80211的21H461精确ABI、
内核控制端点、用户态job/trust与固件载入的完整整合；持久凭据、Wi-Fi/蓝牙共用供电与睡眠也未完成。

充电主线仍保留双口仲裁与完整保护周期缺口；本轮参考同步无相关新提交，
ArmadaOS仍明列双口充电待完善。完整外围Goal未完成，GPU最后、扬声器先讨论保护。
