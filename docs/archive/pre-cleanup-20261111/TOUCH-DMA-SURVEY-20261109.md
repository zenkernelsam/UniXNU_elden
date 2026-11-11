# T1触控传输：FIFO禁用后的下一发

执行入口：`research/EXECUTION-PLAN-20261109.md`。继续集中处理触控，不另开外设功能线。
用户现已拔长边电脑线、保留短边PD快充；**在用户通知充好之前，只离线工作，不访问/重启/刷写平板。**
Goal仍active，设备操作暂缓。最后一次实际设备操作已恢复stock Android，见下节。

## 已有实机结果

T1候选v23/touch-identify-powered：SHA `0ee8f8bae7386cff1a9c0f4cfb53e09c68fa66a60ce320d979315ed42e303684`，
session `6eba1b394fdd6e8f`。实际读到：

```text
ELDEN_TOUCH platform=-95 stage=2 protocol=1 fifo-disabled=1 width=32 depth=16 mode=1
ELDEN_TOUCH stop error=-95 protocol-stage=0 platform-stage=2 released=0 quarantine=0 reports=0 invalid=0
```

SPI固件和FIFO几何读到，芯片命令未发送。Root/Data正常，日志无panic，
RPC完整sync后PC实际fastboot枚举通过。未确认NT36536 ID，因此T1仍未完成。
原日志和判读：`edl_work/live-touch-identify-20261109/`。

随后写回stock8b8e72da…，完整摘要一致、Android/root通过，后续熄屏电流-471mA。
恢复件：`signing/phase5_resign/boot_signed.img`，完整SHA：
`8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890`。
恢复记录：`edl_work/touch-first-stock-20261109/`。这些是已完成的历史读数，不能替代下次fresh电量准入。

## 路径选择的证据

- Armada四代SM8750 DTS：wrapper1/SE4@a90000，SE DMA使用SIDa3；GPI controller@a00000使用SIDb6。
- 两者不是同一个DMA主设备，不能把UFS SID60/CB0的通过结论移给触控。
- Linux遇到FIFO禁用优先用GPI。其FIFO fallback是源码路径，不代表Y700 XNU已经可以使用。
- sheng的TouchProbe有v53–v55实机记录：FIFO配置CSR2008访问异常，采用SE DMA路径。
  可复用方法和通用协议，不能直接复制sheng的SMMU/时钟地址，也不能现在就改本机SMMU映射。
- 新参考enij90/armada-tb321fu是SM8650/NT36523；0806调用Linux spi_sync，
  本身并未提供XNU GENI后端。审核见`ARMADA-TB321FU-REFERENCE-20261109.md`。

## 新调查实现

`elden-touch=4`：

1. 按原来的已知GCC流程供时钟，读取SPI固件、接口、FIFO、GENI/GSI/SE-DMA状态。
2. `survey_only`在任何SE初始化、引脚重配、频率修改或芯片指令前返回。
3. 只读取SMMUv2全局CR0/IDR和SMR/S2CR；遇相关bank时只读全局CBAR/CBA2R。
4. 遍历IDR0声明的全部槽，不截成128；记录重叠/重复匹配，不把“找到路由”当作DMA资格。
5. 没有context-bank MMIO、TTBR页表解引用、FIFO配置CSR2008、DMA指令或SMMU写。
6. 恢复原clock vote/root enable，按原USB/RPC回程结束。

不准确称整个候选零硬件写：为读取GENI会临时供时钟；原M3/UCSI初始化和ICL500mA仍保留。
调查不驱动触控，不上传固件，不启HID/温控GPIO/背光/无线。

## 构建与主机检查

- `TestPlatform.c`：302933项含重复边界的断言；新survey模式覆盖FIFO关闭、GENI/GSI/DMA活动现场，
  不发SE/引脚/速率写，供时钟可正确归还。
- `TestDmaSurvey.c`：槽180/181、重复及掩码重叠、扩展SID格式拒绝、全1 ID、无效bank、
  地址范围和无写回调检查。ASan/UBSan均通过；不是硬件成功证据。
- 新源码冻结/差异许可：`research/touch-dma-survey-20261109/touch-source-manifest.json`。
  `prepare_basic_bundle.py --touch-manifest`显式检查原manifest SHA和新文件SHA，保留原source-compiled。
- 原生实际构建：**basic v24 / power v20 / backlight v8**，127份源/构建输入冻结source-compiled-v24。
  没有更新UFS、GOP、HID ABI或固件输入；增加的survey头也纳入最终compiled-source核对。

## 待测件

路径：`edl_work/basic-bundle-20261108/v24/final/touch-dma-survey-focused-gestures/boot-signed.img`

- 镜像SHA：`d285afd934c83a23b5a7e2df39d822d32230d5e19d2d6b99bad355b2a57d127e`
- session：`219d57d3edfcf40c`
- kernel：52691048B，SHA `c794b1cca9306bebc1ecb554098312cdbddcf3ac64315811aac9c48270c0fb44`
- 96MiB，**1738713600@footer0/父链3**，独立RSA/data/stockProps/原30模块/HID207+220/37/资产检查通过。
- 配置仍6GiB、512MiB RAM Data、8秒默认入核、USB日志与fastboot回程。没有刷入。

复验：

```text
python edl_work/audit_basic_bundle.py --version 24 --variant touch-dma-survey-focused-gestures
```

待用户说充好后，fresh读取电量，至少3500mV/native yes；先启session绑定日志，再按boot_a-only工具刷入，
reference用stock8b8e72da…，最后完整回读/回程确认。
记录`ELDEN_TOUCH entry`、`ELDEN_TOUCH_DMA route/context/matches`和`ELDEN_TOUCH stop`。
无须用户当场操作菜单或拍屏，后续真正手指上滑验收除外。

拿到映射后，继续T1实际DMA后端：核cache维护、地址宽度、目标SID与bank共享、所有权及失败隔离。
若需要改变翻译，须针对这条route设计，不能做全局bypass。此调查结果到手前不填猜测bank或地址。

本轮无平板访问，Mac/主机编译均已结束。没有未完成设备写入或监听器。
