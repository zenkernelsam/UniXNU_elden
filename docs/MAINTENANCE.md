# 目录维护和归档规则

源码唯一入口为本仓。配套工作台目录为 `Patch/Y700-Elden`，设备备份与资产不提交。

| 工作台目录 | 内容 |
| --- | --- |
| `research/` | 经 Git 顶层核验的直接参考克隆；不放笔记、截图、输出 |
| `lab/records/` | 历史实验、只读记录及冻结输入 |
| `lab/reference-reviews/` | 每轮上游更新的证据和结论 |
| `archive/research-notes/` | 旧 handover、探索文档 |
| `archive/session-logs/` | 原 AGENTS 历史；包含私有现场信息，不公开提交 |
| `maintenance/` | 整理事务、路径迁移表、回收凭据、同步报告 |
| `edl_work/` | 尚在迁移的冻结产物和历史运行记录；不要新建独立源码 |
| `Backup/`, `signing/`, `vendor-pack/` | 备份、恢复和外部输入；不得按废件清理 |

一次性工具放 Windows TEMP。Windows 退役文件只进回收站；WSL 退役文件先移至
Windows 并核对内容，再进回收站；macOS 文件进用户 Trash。不要清空垃圾桶或永久删除。
回收站仍占磁盘空间，不能报告为“已释放”。

历史路径通过 `maintenance/cleanup-20261111/research-moves.json` 查原位置和新位置。
Mac 有自己的对应表。不要盲目全局替换历史 JSON，否则会破坏证据的哈希关系。

每六小时的引用同步使用：

```powershell
py -3.12 tools/maintenance/sync_reference_repos.py --workbench D:\Desktop\Git\Patch\Y700-Elden --apply
```

脚本在 `maintenance/IN_PROGRESS.json` 存在时直接跳过；`--inventory-only` 只做目录核验。
只快进干净未分叉的跟踪分支；有本地修改或未跟踪文件的仓只 fetch，固定检出不换版本。
报告写入 `maintenance/reference-sync`，不操作设备。
