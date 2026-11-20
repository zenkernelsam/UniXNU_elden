请继续 Y700 Elden 主线 Goal。离线分支 `offline-peripheral-fixes` 已交付五项外设源码，独立于主线 Wi-Fi/PCIe/WCN/QRTR 和 GPU 分支；不要 commit/merge 错目录。

当前实现提交 `f3329c9`，入口 `docs/peripherals/STATUS.md`、`INDEX.md` 和 `CAMERA-FRAMES.md`。相机原 Board/CCI/CAMSS/DMA 完成帧→可见性→CPU借用→缓存只读快照→明确归还/重排队，已接 OS17 IOKit 服务与两个签名 CMIO 提供者。软件 RAW10 全10bit预览采用显式 Bayer/旋转/黑电平/WB，不造帧，不写 sensor gain。kernel e6459d9c/native70e4c098、1069RAW10cases6062checks、正常30192/停止失败30358/可见性失败30027与全部11审计通过；所有原 archive/recovery5500ms/Apple30保留。服务/CMIO/job/cache/trust未自动安装、无实机出图/App验收；frames-validation.json记录完整指纹。

共享 SMMU 父提交 `d7571f2`：一次 EARLY 原子 batch，legacyWiFi1401保留，可含camera1c00及 ADSPsession3..8 的100N/104N/106N/108N，严格按6f97250e DT带mask的有效集合展开。不同group独立bank/ASID，slot>=8/bank>=7，全 backing 含保护页。每actualwrite前校验原physicalidle/IRQ/memory/ACTLRsecurity/完整slot-bank状态；写完再次核 TTBR/tablegraph后才发布。partialfailurepoison/retainreset，global97只注册一次，所有实际CB/TLB/IRQ/mastersidle后才退役；SMR/S2CR/maps保持到reset。173core1400032+11composed2395232、8scalarobjects0warnings及全审计通过。SharedNative.h/Peripheral.h为接口，不能安装第二个global/SMMUowner；主线原WiFi/currenttouch/input恢复应先审核，再port源码重建。

AP501U独立HID/tilt、原worker可逆pause/gesturewake、RTC读钟、ADSP/GLINK/QRTR/FastRPC/SSC/内存cache、双AW86937限增益catalog/RTP/Actuator也已离线链接，具体看各组件文档。真实同机 sensorPD/registry/calibration/mount/time/SUID/samples、J317SPU/CoreMotion/UI旋转、Pencil类型/force/tilt/palm、屏幕/systemPM唤醒、RTCpersist/alarm及马达机械电气调音/系统路由仍未验收。host/model不是硬件证明；没校准马达输出保持关闭，Apple效果强度不是硬件gain。

板级优先 ArmadaOS-Freedom 的TB322FC/SM8750精确源码/补丁，冻结Linux6f97250e，不搬三代/五代寄存器；OS17接口复用同版成熟UniXNU。真实启动仍须原sharedpower/clock/IOKitmapping/ACTLRsecurity/serializedtouch authority，不能no-op/固定bank/lateexception；旧整个kernel不可盖当前主线。按唯一owners/recoveryport/rebuild后做功能性真机验收，保持floor1738713600@0/parent3与自动回程。五项Goal继续active，组件成功不清全局flag。

源码仓 https://github.com/zenkernelsam/UniXNU_elden ，分支 offline-peripheral-fixes；Windows独立工作树 C:\Users\Cisco He\.codex\worktrees\offline-peripheral-fixes\UniXNU_elden 。Mac对应独立checkout；证据在工作台 lab/records 和 edl_work，research只放Git克隆。GPU已完成、继续保持在codex/elden-gpu-offline。当前主线先推进Wi-Fi，不自动merge这些离线外设。
