# 从零构建的四组原始输入

适用于 `tools/build_clean.py`，在仓库根目录执行，完整命令见
[CLEAN-BUILD.md](CLEAN-BUILD.md)。2026-11-14复核的源码提交为
`7e538277af5a061a81b6c2469b1009d25c1d6ac1`。

旧 `build_wcn_kernel.py --v94-build ... --runtime ...` 是历史增量流程。
当前入口不需要 v94 内核、旧对象、v99 runtime 或 Patch 工作台。
下列原始OS/固件是另外提供的二进制输入；不把它们当作遗漏的自有源码。
这里的“从零”指自有源码与固定公开源码重新编译，仍依赖原Apple组件、
触控/WCN固件和BaseCrypto预编译发布包。

## OS.raw

来源为Apple的
[iPad Pro 17.7.11 / 21H461 IPSW](https://updates.cdn-apple.com/2025SpringFCS/fullrestores/122-77166/DCA6CFE8-90E9-4B92-880D-3DE171798E40/iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw)。
取其中 `044-76040-038.dmg`，再解码其 **Apple_APFS区域**。
DMG为5157406069字节，SHA256为
`3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d`。

最终OS.raw应为6077546496字节，SHA256为
`d8808bea18b949142b3e47bbcb780c3f22faaacd9b40a88f77c23557181edc9c`。
不是整个带GPT的磁盘，不是同IPSW的Cryptex1,SystemOS区域，也不是运行后的Data。
原APFS区域起始扇区为34；不要只改扩展名或任意截取DMG字节。
现有独立解码器是 `tools/legacy-workbench/extract_root_apfs.py`，只做镜像解码，
不需要执行该目录其他设备工具。它额外需要Python的 `lzfse` 模块：

```sh
.venv/bin/pip install lzfse
.venv/bin/python -B tools/legacy-workbench/extract_root_apfs.py \
  --input /path/to/044-76040-038.dmg \
  --expected-sha256 3f99b859c9728379110b66657c7b15285bcb1925ad8b5726542e2cdcf8b1028d \
  --output /path/to/new/OS.raw
shasum -a 256 /path/to/new/OS.raw
```

将其填入JSON的 `os_image`。这里只记录已有解码证据，本轮没有重新下载IPSW。

## Novatek触控固件

准确文件名 `novatek_ts_csot_3k_fw.bin`，来自TB322FC原厂固件/对应Recovery的
`vendor/firmware/`。固定参考Recovery源为
[polygraphene/android_device_lenovo_TB322FC](https://github.com/polygraphene/android_device_lenovo_TB322FC)，
固定提交 `c9c6a9f1287e5b416c3480c5fe06b23dc54c9ab8` 的
`recovery/root/vendor/firmware/novatek_ts_csot_3k_fw.bin`。
[准确原始文件](https://raw.githubusercontent.com/polygraphene/android_device_lenovo_TB322FC/c9c6a9f1287e5b416c3480c5fe06b23dc54c9ab8/recovery/root/vendor/firmware/novatek_ts_csot_3k_fw.bin)
已在本轮通过HTTP取回并校验，不需要访问平板。

253952字节，SHA256为
`413216d0e8738078414fa681262439ab6d1c181f02974215e0394f102fcc08dc`。
填入 `nvt_firmware`，构建工具重新生成载荷头，不要求旧生成头。

## 五件WCN固件

已有取源证据来自
[mydanyi/armadaos-freedom](https://github.com/mydanyi/armadaos-freedom/tree/8d85484e0c7d1f3e34875b0ea405cb30c6095532/system_files/usr/lib/firmware)，
固定提交 `8d85484e0c7d1f3e34875b0ea405cb30c6095532` 的
`system_files/usr/lib/firmware/`。使用TB322FC四代文件，不替换为TB321FU。

将 `wcn_firmware` 指向包含 **qca目录** 的根目录，保留如下相对路径。
五件文件不能平铺到该根目录，JSON也不能指到qca内部。

| 相对路径 | 字节数 | SHA256 |
| --- | ---: | --- |
| qca/brhbtfw20.tlv | 219296 | 837aea39d33e95a4ba272ef2a2306e5ecf9848853332d2f7b8002901d6af771d |
| qca/lenovo/tb322fc/brhbtnv20.bin | 13319 | f91354b0872f1f7680142df6852b2c8e67e3151ee2ad406b3d183a2fb539702e |
| qca/brhperifw20.tlv | 19884 | 9a8de09a127a5943f8c23d4ee6005531b900f085740a292c9ab5a20e76db6cb5 |
| qca/brhperinv20.bin | 741 | 3c73215352ba6422a9f2639124d27f64ec83a4d2aa91b388439ca88ea31098d0 |
| qca/tmel_peach_20.elf | 56752 | a175c8d9d0e6b38b5ae23ff0175da795b14b8268919463d2b6e1cb2bfeebc9f8 |

构建前按 `profiles/v110-bt-fifo/plan.json` 逐件核验。
本轮已从上述固定提交的raw.githubusercontent.com端点取回五件并核对全部SHA，
无需新编译或读取设备即可补齐这组输入。

## BaseCrypto

需要固定原始 **预编译发布包** 的解压根目录，不是私钥或用户密钥，
也不是corecrypto源码仓。目录中应包含 `ALL/`、`Driver/` 等完整包结构，
包括 `Driver/Bin/CryptoDriver.inc.dsc`。填入JSON的 `crypto`。

已保留原始 `basecrypto.zip`：255451552字节，SHA256为
`2136c410dc44135830e2e33062243563d642e0be868d6affc2930c6b0854f5cf`。
解压后工具会检查 `profiles/v91/firmware-build/plan.json` 中全部994件文件，
不能只复制AARCH64的一个EFI，也不能用任意新版包替代。

目前证据没有锁定该原始ZIP的可重复下载URL；可使用已校验的留存原始包。
这是外部输入获取记录的限制，不能宣称仓库能自动下载全部依赖。
2026-11-14留存ZIP与994件解压文件已重新校验通过。

## 不必等待完整boot输入的目标

`--target kernel` 不需要OS.raw、BaseCrypto、Mu/UniXNU固件源或签名输入；
仍需要原agent、原kernelcache、触控固件和工具链。蓝牙variant另需五件WCN固件，
GUI variant不需要WCN固件。只运行内核目标不等于生成可刷boot。
完整boot另按CLEAN-BUILD.md准备stock模板及签名材料。

验证工具链是Apple clang17 / iPhoneOS SDK26.2 / LLVM20，SDK26.0没有跨版本验证。
若后续报告SDK头指纹不一致，应核对工具链，不能用旧对象或放宽哈希掩盖问题。
