# T1 SE-DMA 传输准备（离线，未接入设备）

用户当前拔掉长边电脑线，以短边 PD 补电。只有收到明确充好/可测试通知后才恢复设备操作。
本轮未运行 ADB、fastboot、EDL 或监听器；Goal active，完整触控与充电均未完成。

## 本轮交付

在设备仓 `drivers/EldenNvt36536/` 新增 `DmaTransfer.h/.c` 与 `TestDmaTransfer.c`。
它们包裹固定版本 QcomGeniSpi SE-DMA 核心，不修改共享核心，不注册驱动，不配置 SMMU，
不分配原生 DMA 内存，也没有接入 Runtime/build hook。下一发仍是已签 v24 调查件。

核心来源为 `research/qcom-geni-spi-20261107/`，revision
`b5399b5646c4d7415deec6d887fd51715a97a024`：

- `GeniSpi.c` SHA256 `2042cfc9647391e24d5a558202046425ac62a05adfc73afe769367b8374e3f91`
- `GeniSpi.h` SHA256 `e73891e4571c5323bca27aaf987bafa002856dfa1dcee156ff31b119e2d3ae3c`

采用 sheng 已有 SE-DMA 方法，时钟/引脚/SID 仍来自 Armada TB322FC。
SE SID 为 `a3`，GPI SID 为 `b6`；两者及 UFS 的资格互不替代。

## 这层负责什么

- 仅接受已初始化的 8-bit、mode0、CS0、最高10MHz SE-DMA；轮询有界。
- 要求外部独占 owner、非零 epoch，以及每轮前后的 SIDa3 双缓冲映射验证回调。
  回调必须确保整个调用期间的 clocks/maps/cache attributes/所有权有效，没有默认成功实现。
- DMA 地址按总线地址理解，不把 `_phys` 字段名称当作恒等映射证明。
- 必须提供 clean、clean+invalidate、invalidate；CPU DMA 缓冲区按实际 cache line 对齐、
  长度为整行倍数且不重叠，避免 cache 操作波及相邻数据。
- attach 时保存有效配置；缓冲区、地址、MMIO/cache 回调、SPI 配置或轮询参数变化后拒绝继续。
  比较字段，不比较结构 padding 或每轮诊断计数器。
- 传输前后核 engine idle、GSI off、FIFO empty；成功路径再检查迟到的 DMA/master 错误。
- RX 先留在独立 staging，只有本轮成功且映射仍有效才复制给调用者；调用者 tx==rx 可用。
- 已开始后的错误隔离整个 session，不把共享核心发出的 CANCEL/ABORT/FSM_RESET 当成复位完成。
  owner 必须保留内存/映射/时钟，直到另行证实硬件停止；此层不执行资源释放。

`EldenTouchDmaTransfer` 使用前清零，由单一串行执行者使用。保存的 engine/config/contract 属于 owner 私有状态，
不是并发安全 API；不能在另一个线程更改对象或解除映射。

## 验证结果

冻结输入：`research/touch-dma-core-20261109/source-reviewed-v1/`。
完整 SHA 在 `source-manifest.json`；构建脚本 SHA 也纳入该 manifest。

主机 GCC ASan/UBSan：**21,060 项断言通过**，包含逐字节循环，因此不是21,060个独立场景。
模型分开 CPU/DMA 内存视图，覆盖 cache 顺序、4GiB以上地址、1..15361字节、
TX/RX/双向、调用者原地传输、非法/重叠缓冲、缺回调、错SID、过期映射、
总线错误、超时、DONE后仍active、迟到错误、配置改变、失败不发布和隔离后不复用。
命令及完整输出：`research/touch-dma-core-20261109/host-test.json`。

Mac 使用既有 21H461 driver 的原生 C 编译配置生成两个 Mach-O object：
arm64 / iOS17.0 / freestanding / general registers only，检查无自动 initializer。
独立静态库 **9240B**：

`edl_work/touch-dma-core-20261109/touch-dma-core-20261109-v1/libEldenTouchDma.a`

SHA256：`4aa67e2e6dc4fdc2189d5bac79bb34d1a97d6e15d807d51f96cdb6f2cdbfd4e0`。
同目录 `build.json` 含命令、输入/输出 SHA、目标平台；`undefined-symbols.txt` 只有
wrapper 对原库 `geni_spi_transfer` 和内核 `memcpy` 的引用。Windows 拉回后已复核 archive SHA。

这是独立静态库编译，**没有 XNU 重链、镜像更新或实机 DMA 验收**。
v24 image/kernel/source-compiled 和 rollback index 均未修改。

## 仍须实机解决

1. 用户充好后先跑 `HANDOVER-TOUCH-DMA-SURVEY-20261109.md` 中 v24；
   以实际 SE SIDa3 route、共享关系与翻译状态决定实现，不填猜测 bank 或全局 bypass。
2. 完成真实内存描述符/缓存范围、lease 和 route 验证后端。仅有回调接口不等于 DMA 资格。
3. 此层沿用 canonical DMA_DONE 完成判据。`LEN_IN` 目前只保留原始诊断，
   上游 Linux/sheng 参考没有为本机证明稳定的精确长度语义；不要随意假定它等于请求长度。
   后续调查需记录 `last_tx_in/last_rx_in`，核真实传输；原协议的 ID、PID、checksum 等检查继续保留。
4. 芯片 identification 通过后再进行原厂 SRAM 上传和点帧/HID；保持现有 M3/USB/回程。

下次刷入 reference 仍是 stock `8b8e72da…`，现场重新读取 >=3500mV/native yes。
用户没通知前不访问设备。当前无未完成编译、刷写或监听进程。
