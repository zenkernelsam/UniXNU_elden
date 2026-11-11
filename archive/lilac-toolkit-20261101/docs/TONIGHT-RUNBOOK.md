# 今晚实机 Runbook（2026-10-30 20:00，lilac）

> 目标：一次把 6 个镜像按序跑完，一字不漏；每发的日志/照片/结论都留档。
> 前提：用户在家；手机在 **fastboot（蓝灯）**；手机没电先充 >15%。
> 红线：无人确认不发弹；不写 TRUST、不做 RBBM reset、不碰
> SMR/S2CFG bypass、不跑 sweetgpuphys/phys2、不写 UFS。

## 0. 开跑前 60 秒核对

```bash
cd D:/Desktop/Git/Patch/Xperia
# 1) 手机必须在 fastboot
fastboot devices            # 期望：BH905ND69G  fastboot
# 2) 六个镜像 sha 必须与表一致（TONIGHT-MANIFEST.md）
for f in research/shim-4k-v340-preresume-boot-gzdtb.img \
         research/shim-4k-v341-ringsplit-boot-gzdtb.img \
         research/shim-4k-v343-minit-first-boot-gzdtb.img \
         research/shim-4k-v344-rm-boot-gzdtb.img \
         research/shim-4k-v342-ringsplit-nozap-boot-gzdtb.img \
         research/shim-4k-v338-el-secvid-boot-gzdtb.img; do
  echo "$(basename $f) $(sha256sum $f | cut -c1-16)"
done
```

期望前缀：v340 `9e4a79de` / v341 `e7b81d46` / v343 `88f14067` /
v344 `a25855ab` / v342 `1239a26c` / v338 `dce4f1b2` /
v345 `4531b3b1` / v346 `f1f28b23` / v347 `09ef919a`。
不一致 → 停止，先查文件被谁覆盖。

相机自检（可选）：`curl -m 5 http://192.168.68.60:4747/cam/1/af`。

## 1. 一键顺序发弹

```bash
CONFIRM=1 bash research/fire_tonight.sh
```

脚本对每发执行：patch 2/2 → `xperable -I <img> -1 -c boot -1` →
等 COM17 → 读 45s → 相机截图 `%TEMP%/cam-<tag>.jpg` →
打印关键行 → 等 WDT 回 fastboot（最多 8 分钟）→ 下一发。
**中途任何一发 patch 未 2/2 或手机不回 fastboot，脚本会停**。

## 2. 每一发要记录的三件事（缺一不可）

1. `%TEMP%/<tag>-com.log`（脚本自动存；USB 在 GPU 上电后可能截断）
2. `%TEMP%/cam-<tag>.jpg`（屏幕最终信标；USB 截断时这是唯一证据）
3. 下面表格填一行结果

| tag | sha | COM 有/无 | 信标 5 位 | 关键结论 |
|---|---|---|---|---|
| v340 | 9e4a79de | | | |
| v341 | e7b81d46 | | | |
| v343 | 88f14067 | | | |
| v344 | a25855ab | | | |
| v342 | 1239a26c | | | |
| v338 | dce4f1b2 | | | |
| v345 | 4531b3b1 | | | |
| v346 | f1f28b23 | | | |
| v347 | 09ef919a | | | |

信标解码（y≈390，左→右；绿=1 红=0）：
b0 consumed / b1 trustoff / b2 ME==0x404 / b3 scratch 写入 / b4 页表。
`01011`（绿红绿红绿）即 **CP LIVE**。

关键日志行（COM 里 grep）：
`SHIM EL`、`SECVID cfg=... trust=... tsb_base=... size=...`、
`SCM pre-resume`、`CP split A rptr=... mi=`、`CP release ME=...`、
`CP consumed=... fence=...`、`CP micro2`、`CP ucode`。

## 3. 判定树（每发跑完立刻对照，决定是否跳过后续）

| 观察 | 结论 | 动作 |
|---|---|---|
| 任一发 `consumed=1` 或信标 b0=1 | CP 已取指 | 跳到 v347（真 DMA 探针）；其余顺序镜像可跳过 |
| v341 `mi=1` | ME_INIT 即可启动 CP | v343/v344 仍跑（顺序对照）；CP 活后另建 v347（DMA 探针，见 §4） |
| v341 `mi=0` 且 v343 也 0 | 与 0x66 位置/周期无关 | 跑 v344（IRAM 读回）；随后 v345（CB0 对齐）、v346（TSB 窗口） |
| v342 比 v341 好 | 我们的 ZAP 有害 | 后续镜像一律带 `sweetgpunozap=1` |
| `SHIM EL=2` | shim 在 EL2 | 记下；下次做 shim 冷启动生产者（可 RBBM reset） |
| SECVID tsb_base/size 非零且不含 0xFC000000 | TZ 限定安全窗口 | 准备 v346：ring/固件搬进 TSB 窗口 |

## 4. 出现 CP LIVE 之后的下一步（预留）

1. 立即拍照 + 存日志，更新 AGENTS「当前状态」。
2. 用 v341 的 B 周期已验证 MEM_WRITE+fence；下一步建 v347：
   split + 真实 DMA 探针（Fill32/Copy32 + 读回校验），再 GPMU ring 路径。
3. 之后才谈 40B handoff 与 IOGPU/Metal（移植清单见
   `GPU-HANDOFF-LILAC-PLAN.md` 末尾）。

## 5. 异常处理

- **没有 COM17**：等 10s 再试；仍无 → 只靠相机照片判定；不影响后续。
- **手机不回 fastboot（>8 min）**：长按电源 10s→等 5s→再按一次进蓝灯；
  仍不行 → 记录并停止（不要反复盲发）。
- **进了充电界面/绿灯**：立即停止本晚流程；等用户处理（零设备政策）。
- **patch 卡 1/2**：脚本会自动 `fastboot reboot bootloader` 重试一次；
  仍失败 → 停止，检查 unlock/xperable.exe 与 USB 线。

## 6. 收尾（必须做）

```bash
cd D:/Desktop/Git/Patch/Xperia
# 填完 §2 表格、AGENTS 当前状态后
git add AGENTS.md research/TONIGHT-MANIFEST.md
git commit -m "night: <日期> 六发实机结果"
```
并把结论同步到 Mac `~/Desktop/ios-xperia-xz1c/NOTES.md`。
