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
5. 用正式仓的编译清单在新目录重建 **1,010 个 XNU 对象**，全部通过代码/数据段的字节、
   大小与类型比对，未复制旧对象。包括原 XNU 的 HIB/KLD/LAST 编译后处理。
   入口和边界见 [`object-build`](../profiles/v91/object-build/README.md)。这不是最终链接验收。
6. 正式仓入口又完成 **608 个原生对象、14 个静态库**的独立重建，核对代码/数据段、
   全局及绝对符号绑定、整库导出/导入符号集合。见 [native-build](../profiles/v91/native-build/README.md)。
7. 正式仓入口完成 native 链接和 prelink：50 个 native 装载节的内容、地址、属性和全局符号对齐；
   实际读取并哈希确认 30 个原始 Apple 组件的执行区保持不变。见 [image-build](../profiles/v91/image-build/README.md)。

8. entry 转换、ADT/菜单/信任缓存再生、FAT16 与签名 boot 的工具已实现，并生成通过独立静态核验的候选。
   详情见 [boot-build](../profiles/v91/boot-build/README.md)。没有刷机或硬件验收。

**源码到 prelinked kernel 已闭环；完整 boot 的资产源码链仍未闭环。**
当前候选使用三个明确列出的固定外部资产：已验 UEFI FD、独立 EFI 与 Data seed。
不能把“已生成并签名”说成这些资产也已独立源码重建。
新内核允许 UUID、调试链接信息及 `_PrelinkKCID` 随构建变化，不宣称整文件 SHA 与旧像相同。
历史打包入口仍依赖旧资产和增量记录，不能直接当作新的完整复现入口。
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
python3 tools/build_v91_xnu_objects.py --xnu-source /path/to/new-source-output/xnu --xnu-repository /path/to/pinned-xnu --payload-header /path/to/new-agent-output/EldenIconCpuPayload.h --output /path/to/new-object-output
```

后续原生库与 prelink 的完整命令见上述两个 profile README。NT36536 头使用
`tools/generate_nvt_firmware_header.py --firmware <原厂输入> --output <新头文件>` 生成，
输入和输出均按 v91 指纹验证；不把该二进制载荷头加入 Git。

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
| `profiles/v91/object-build/` | 可分发的 XNU 编译清单、生成源码及逐对象验收指纹 |
| `profiles/v91/native-build/` | 14 个原生库的固定来源、编译及符号验收清单 |
| `profiles/v91/image-build/` | native 链接、链接元数据再生与原始组件 prelink |
| `tools/legacy-workbench/` | 原工作台配方源码，保留原字节；不是可移植入口 |
| `tools/device/` | 维护中的 AVB、Firehose、完整刷写和 USB 日志/帧解码；工作台路径显式传入 |
| `archive/host-differences/` | Mac 与 Windows 不同版本，供审核，不自动覆盖当前源码 |
| `archive/kshimc-wsl-20261111/` | 相对固定 ArmadaOS 快照的 KShimC 差异，保留未来双系统路线 |

参考仓观察清单 `config/reference-repositories.json` 用于维护，不是构建 lockfile。
`observed-repositories.json` 和上游 overlay 的 provenance 记录捕获时的依赖状态；
其中 dirty 状态必须结合补丁和冻结快照使用，不能单凭 HEAD 宣称复现。

## 剩余验收

- 独立复核三个固定外部资产的源码/生成方法，见 boot-build/external-inputs.json。
- 完整资产闭环后再做最终候选资格；当前 prelink 执行区不变的结论不涵盖之后的 AMFI 单指令转换。
- 消除有效配方对旧工作台路径的依赖，保留原始证据哈希。
- 完整重建后核对导出、模块 ABI、签名 floor 和 boot 资格，再宣称 Jerry 可以复现最终镜像。
