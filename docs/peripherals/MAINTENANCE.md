# 外设工作树整理与可恢复产物

本周把三份累积状态文档收敛到当前入口：AGENTS、STATUS 和主线 handoff。
完整旧字节、原 SHA 与大小保存在 docs/archive/peripherals-20261121/manifest.json。
组件技术文档仍在原位置供旧 build/audit 引用，INDEX.md 按链路排序。

已核完成：独立 Mac 外设工作树的348项过时输出（allocated3,504,148,480B，
约3.26GiB）和16份一次性传输／辅助文件已进入原生垃圾桶，完整摘要搬后核对。
当前实验 lab 从约6.2GiB整理到2.9GiB；不是释放3.26GiB。最新相机内核摘要与
1066项实际传递构建输入仍全部匹配。主线自身另有137项21.25GiB回收记录，
本次只引用它的凭据，没有重复处理主线/GPU/Lilac或更改任何设备状态。

相机接口修复 `c15d94e` 完成后，再处理17项本轮失败／已替代产物和传输
文件（allocated105,926,656B，约101MiB）。全部搬前／搬后文件清单与SHA
一致，新kernel c12fcece及1066项真实链接输入重核通过。新版Windows归档
camera-bridge-offline.tar.gz 的644成员也逐一核对；旧frame归档继续保留。
补充凭据在workbench maintenance/camera-bridge-cleanup-20261121；恢复依
native-trash.json 的 source→trash_path。本轮结束后的一次性辅助文件凭据
另存temp-native-trash.json。当前剩余约59GiB，垃圾桶未清空。

macOS 产物只使用 NSFileManager `trashItemAtURL` 移到用户垃圾桶，不执行 unlink、
递归永久删除或清空垃圾桶。原项目目录到垃圾桶的路径、文件数、完整指纹
与移前/后验证存入工作台 maintenance/peripheral-cleanup-20261121。

规则：先验证 Windows edl_work 中的已交付归档，再查实际进程、当前链接命令/
filelist/源 manifest 的引用。当前内核、必要原 OS/固件/SDK、源码及其 Git、
他人未提交/正在构建输入保留。没有引用也不能把整个外设/主线项目当作废件。
被判为一次性或已替代的 trial/delivery 输出，保存小型 JSON/log 凭据后移入
垃圾桶。恢复按 native-trash.json 的 resultingItemURL 找回原路径，内容 SHA
和文件清单会再次核对；不要靠猜目录名恢复。

同一 APFS 盘上的垃圾桶仍占空间，所以“整理了多少产物”不是“已释放多少容量”。
本线程不清空垃圾桶，用户决定何时回收。前次主线 maintenance/cleanup-20261121
已完成的清理单独引用，不重复移动它的产物，也不伪称本线程完成了他人构建。

代码仅在 offline-peripheral-fixes；Patch 只提交本轮证据/维护清单，并保留同仓
AnyRouter 等任务的 staged/unstaged 字节。research 只允许独立 Git 克隆；一次性
脚本在 WindowsTemp，Mac辅助在/tmp，清理后也使用原生垃圾桶退役。

当前完整离线候选：c15d94e，CAMERA-FRAMES.md／bridge-validation.json；
原sharedSMMU父d7571f2，旧frame父f3329c9。docs/archive/peripherals-20261121
保存旧状态／旧相机说明，当前文档不再累积多轮互相过时的结论。
物理设备工作、主线 Wi-Fi/GPU 和所有 Goal 状态不由本周整理更改。
