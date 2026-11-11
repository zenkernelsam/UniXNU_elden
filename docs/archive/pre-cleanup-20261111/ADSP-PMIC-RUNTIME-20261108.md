# Elden ADSP／PMIC完整链（离线，2026-11-08）

复用sheng68940017的完整ADSP/PAS、QRTR、SMEM/SMP2P/GLINK与FAT分段固件读取，
由EldenPowerResources.h给本机IPCC/RSC/保留内存；不复用SM8550的硬编码地址。
接口和许可证保留；新增主UFS modem_a只读gateway、37项Lenovo原始轮询与真实GLINK客户端。

完整iOS17目标编译、XNU重链及prelink通过；13个新API在实际镜像存在，原Apple组件执行SHA保持。
原存储190槽、HID207槽与52个原始绑定通过。启动参数分为fw-check、ADSP survey和ADSP+PMIC。
全部是未上机的独立候选，boot1738713600/footer0/父链3、FD/shim/56MiB FAT/6GiB/原root/RAM Data保持。

源文件：`drivers/EldenPmicGlink/AdspProbe.cpp`、`AdspFirmware.c`、`ShengQrtr.c`、`PmicClient.cpp`、
`ChargePoll`、`EldenModemGpt`及原协议核心。默认不激活：`elden-adsp`、`elden-pmic`与`elden-modem-fw-check`均须明确给定。
fw-check不能与adsp同时启用；它只读原firmware并核整件SHA，不发PAS/charger命令。

RSC请求必须实际完成，超时保持TCS/DRV2隔离；已运行DSP在加载前被保留。连接epoch清空旧发送，
expected-epoch send阻止跨重连迁入请求。一个GET在途，整轮才提交，timeout须真实新链。
原`sheng_adsp_send`断链返回-3；未提供可靠电流方向/FG单位/端口对应前不发布伪电量或充电状态。

主机ASan/UBSan：PMIC420项、实际LUN4 GPT490项通过；实际modem_a FAT/MDT重读得到原完整ADSP/DTB，
与预备输入逐字节一致。实际充电、双口选择、IRQ、保留内存和净SOC趋势仍需硬件资格。
详细候选/SHA/session/接手顺序在Y700工作仓`research/HANDOVER-ADSP-PMIC-RUNTIME-20261108.md`。
