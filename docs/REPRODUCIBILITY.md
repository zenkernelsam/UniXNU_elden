# 源码与复现

`UniXNU_elden` 是唯一源码仓。工作台中的历史源码副本和构建记录用于核对，后续修补在本仓提交。
原始系统、固件、签名材料和设备备份由复现者自行提供，不进入 Git。

## 已验证的复现层

1. [`profiles/v91/manifest.json`](../profiles/v91/manifest.json) 锁定编译时的源码快照与补充构建材料。
2. 从 XNU `d8b80295118ef25ac3a784134bcf95cd8e88109f` 的干净 Git 对象重建源码，
   应用宿主补丁，再应用 v91 编译快照中的文件，已实际通过。
3. 在全新输出目录用本仓源码重编 owned IconServices dylib，SHA 与实机 v91 **逐字一致**。
4. 从复现者提供的原始 `iconservicesagent` 重做本地依赖和签名，并生成
   `EldenIconCpuPayload.h`；签名执行件及最终头文件 SHA 也与 v91 **逐字一致**。
   原始代码段及 entitlements 均复核未变。

**完整干净整核构建、prelink 和最终 boot 包尚未重新验收。**
历史入口曾依赖旧对象、ABI 导出和增量记录；归仓不等于这些依赖已经消除。
`board.toml` / `unixnu.lock` 的默认值仍是早期引导配置，不能作为 v91 的一键复现入口。
不要直接把同步到最新的参考仓替换冻结依赖。

## 已维护的命令

以下命令从本仓根目录执行。Python 3.12；构建组件需要 macOS、Xcode iPhoneOS SDK。
本次验证工具链为 Apple clang 17.0.0 / iPhoneOS SDK 26.2。

```sh
python3 tools/reconstruct_v91.py --verify-only
python3 tools/reconstruct_v91.py --xnu-repository /path/to/pinned-xnu --output /path/to/new-source-output
python3 tools/build_icon_v91.py --output /path/to/new-icon-output
python3 tools/prepare_icon_agent_v91.py --original-agent /path/to/original/iconservicesagent --library /path/to/new-icon-output/EldenIconCpuMode.dylib --output /path/to/new-agent-output
```

所有输出目录必须新建，工具不重置已有目录，不签设备 boot、不刷机。
原始 agent 的 SHA 必须为
`dd01b74e4413e0bb72de4a62f3f7675deb5026cad79d376fde6570abd0d012f8`。
生成的二进制头已加入 Git 忽略规则，不能把它作为源码提交。

## 材料分工

| 路径 | 用途 |
| --- | --- |
| `drivers/`, `soc/`, `package/`, `patches/`, `scripts/` | 当前开发源码；可能包含未验收工作稿 |
| `profiles/v91/sources/` | 实际编译输入的冻结源码，排除二进制载荷头 |
| `profiles/v91/reconstruction/` | XNU、UniXNU、固件宿主改动与依赖观察记录 |
| `tools/legacy-workbench/` | 原工作台配方源码，保留原字节；不是可移植入口 |
| `tools/device/` | 维护中的 AVB、Firehose、完整刷写和 USB 日志/帧解码；工作台路径显式传入 |
| `archive/host-differences/` | Mac 与 Windows 不同版本，供审核，不自动覆盖当前源码 |
| `archive/kshimc-wsl-20261111/` | 相对固定 ArmadaOS 快照的 KShimC 差异，保留未来双系统路线 |

参考仓观察清单 `config/reference-repositories.json` 用于维护，不是构建 lockfile。
`observed-repositories.json` 和上游 overlay 的 provenance 记录捕获时的依赖状态；
其中 dirty 状态必须结合补丁和冻结快照使用，不能单凭 HEAD 宣称复现。

## 剩余验收

- 用声明的外部输入生成固件、ABI 和系统资产；为所有依赖固定 origin、commit、补丁顺序。
- 将 native archives、XNU、prelink、ADT/payload 的增量链替换为干净构建入口。
- 消除有效配方对旧工作台路径的依赖，保留原始证据哈希。
- 完整重建后核对导出、模块 ABI、签名 floor 和 boot 资格，再宣称 Jerry 可以复现最终镜像。
