# 目录维护和归档规则

源码唯一入口为本仓。配套工作台目录为 `Patch/Y700-Elden`，设备备份与资产不提交。

常用设备工具实现位于 `tools/device/`。旧 `edl_work` 下的七个入口已改为转发，
由 `tools/maintenance/install_workbench_launchers.py --workbench <路径>` 校验并安装。
它只接受已归档原文件或完全相同的现有转发文件，拒绝覆盖未知修改。
刷写器要求显式工作台；监听器要求显式日志目录。19 项离线模型测试与端口归属/就绪断言通过，
维护标记存在时 `--write` 在读取候选或访问设备前被拒绝。

| 工作台目录 | 内容 |
| --- | --- |
| `research/` | 经 Git 顶层核验的直接参考克隆；不放笔记、截图、输出 |
| `lab/records/` | 历史实验、只读记录及冻结输入 |
| `lab/reference-reviews/` | 每轮上游更新的证据和结论 |
| `archive/research-notes/` | 旧 handover、探索文档 |
| `archive/session-logs/` | 原 AGENTS 历史；包含私有现场信息，不公开提交 |
| `archive/legacy-workbench-source-20261112/` | 已逐字匹配正式仓的447份旧顶层源码副本 |
| `maintenance/` | 整理事务、路径迁移表、回收凭据、同步报告 |
| `edl_work/` | 冻结产物、历史运行记录和七个兼容入口；不要新建独立源码 |
| `Backup/`, `signing/`, `vendor-pack/` | 备份、恢复和外部输入；不得按废件清理 |

一次性工具放 Windows TEMP。Windows 退役文件只进回收站；WSL 退役文件先移至
Windows 并核对内容，再进回收站；macOS 文件进用户 Trash。不要清空垃圾桶或永久删除。
回收站仍占磁盘空间，不能报告为“已释放”。

历史路径通过 `maintenance/cleanup-20261111/research-moves.json` 查原位置和新位置。
Mac 有自己的对应表。不要盲目全局替换历史 JSON，否则会破坏证据的哈希关系。

新增 `legacy-source-moves.json` 记录447份源码副本的归档路径；现行实现和修补只在正式仓维护。
`retire-superseded-boot-packages.receipts.jsonl` 记录328份过时 boot/FAT 包的回收路径和完整摘要，
约17.5GiB，仍占回收站空间。v89–v92、内核/日志、备份与恢复资产保留。

每六小时的引用同步使用：

```powershell
py -3.12 tools/maintenance/sync_reference_repos.py --workbench D:\Desktop\Git\Patch\Y700-Elden --apply
```

脚本在 `maintenance/IN_PROGRESS.json` 存在时直接跳过；`--inventory-only` 只做目录核验。
只快进干净未分叉的跟踪分支；有本地修改或未跟踪文件的仓只 fetch，固定检出不换版本。
报告写入 `maintenance/reference-sync`，不操作设备。
