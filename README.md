# UniXNU elden

Lenovo Legion Y700 Gen4 / TB322FC / SM8750 的 UniXNU 适配源码。

**最近已验：v91 运行 iPadOS 17.7.11 (21H461)，通过软件上滑和短按打开有内容的 Settings。**
真实手指交互、显示流畅度、持久 Data、完整充电、无线等仍在推进，尚非完整可用的 iPad。

- [当前实测状态与边界](docs/CURRENT-STATUS.md)
- [源码复现、已通过的干净构建与剩余缺口](docs/REPRODUCIBILITY.md)
- [下一阶段计划](docs/PLAN.md)
- [目录维护与参考仓同步](docs/MAINTENANCE.md)
- [历史文档索引](docs/archive/README.md)

本仓是唯一源码入口。`drivers/`、`soc/`、`package/`、`patches/` 与 `scripts/` 保存开发实现；
`profiles/v91/` 保存实际编译输入及重建材料，供 Jerry 和其他维护者核对。
`tools/legacy-workbench/` 保存旧配方源码，其宿主路径和增量依赖尚在迁移，不能当作通用刷机入口。

已经从全新目录重编 v91 的 owned dylib，并从自备原始系统文件重建载荷头，SHA 均与实机版本一致。
XNU 对象、14 个原生库、native 链接和 prelink 的独立源码构建也已通过；
entry 转换、ADT/菜单/FAT 和签名 boot 的工具也已生成通过静态核验的候选；
独立 EFI 与 Data seed 也已完成源码生成及离线核验；**UEFI FD 的源码生成链仍在核对**。
默认 `board.toml` / `unixnu.lock` 仍是早期配置，不能据此声称一键得到 v91。

原始 iPadOS/固件、签名密钥、设备备份与生成的二进制载荷头不提交到本仓。
Boot rollback floor 保持 `1738713600@footer0`，每次设备写入须核身份、电量、签名与完整 SHA。
GPU 最后推进；扬声器先讨论衰减和保护，双口供电不得绕过原厂电气及温控约束。
