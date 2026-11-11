# T1 原生 DMA 缓冲区与缓存接口（未启用）

继 `HANDOVER-TOUCH-DMA-CORE-20261109.md` 的传输核心，新增设备仓
`drivers/EldenNvt36536/NativeDmaBuffer.h/.cpp`、`DmaCache.h/.c`、`TestDmaCache.c`。
下一发仍为 v24 只调查 GENI/SE SIDa3/GPI SIDb6，尚未把本轮实现接入 Runtime。

用户刚通知电量53%，可提出实机测试。已请其恢复长边PC/短边PD并报告Android/fastboot状态；
不能把53%或历史电压作为刷写准入。收到接线结果后按现有单轮流程重新读取。
本轮截至离线交付尚未访问平板，Goal active。

## 原生实现

- 沿用 sheng 的 cached contiguous allocation 和 `dc cvac/civac/ivac` + `dsb sy`。
  两路各16KiB DMA buffer、另有16KiB RX staging。32KiB连续内存以4KiB对齐，显式禁默认mapper；
  不对高地址RAM使用`kIOMapInhibitCache`。
- 原生 descriptor prepare、getBytesNoCopy、getPhysicalSegment、complete/release；
  记录物理地址但不据此赋 DMA 总线地址。export必须传入外部审核的bus addresses和SIDa3/epoch回调。
- 实读CTR_EL0获得cache line大小；纯C范围计算拒绝越界、溢出、跨buffer和非整行所有权。
  所有缓存指令之前先验范围。错误由包裹的route-check传回DMA事务，禁止发布RX。
- 导出buffer后，销毁必须由同一epoch owner证明DMA已停、映射已撤；失败保留descriptor和内存。
  `create(out)`出错时若cleanup失败，out仍保留handle，调用者不得丢弃。没有自动析构释放活动DMA的路径。
- 对象仅供单一串行owner，不处理其他线程修改/撤图；没有内置SMMU provider或随意bypass。

## ABI 与验证

基准仍原始21H461载体 `b6e349ed37705133d5e3ff7d9d09ec3a5fd89b4fb92cff0b2a1a4e83f5ae9247`。
Apple `-fapple-kext` 即使显式基类调用也生成基类虚表取函数，因此审核了编译后反汇编与真实原版指针：

| 原表 | 从表符号起的偏移 | 原函数 |
|---|---|---|
| IOGeneralMemoryDescriptor | e8 | prepare |
| IOGeneralMemoryDescriptor | f0 | complete |
| IOGeneralMemoryDescriptor | a8 | getPhysicalSegment |
| IOBufferMemoryDescriptor | 158 | getBytesNoCopy |
| IOBufferMemoryDescriptor | 38（live vptr +28） | OSObject::release |

所有未定义外部符号均在原版exports中，除本次一起构建的 `elden_dma_cache_span`。
检查了目标arm64/iOS17.0、没有自动initializer，以及实际cache/barrier指令。

纯C范围检查ASan/UBSan **408,018项断言**，含逐偏移/长度循环，不是独立场景数量。
覆盖32..4096byte cache line、高地址、边缘长度、非法范围、输出不变和溢出。
只证明范围算法，**没有在PC执行原生内存生命周期或ARM缓存指令**。

成功库96,376B：
`edl_work/touch-dma-core-20261109/touch-dma-native-20261109-v3/libEldenTouchDmaNative.a`

SHA256 `ec6eee935867d2ef52d01e04b1d081f813974d40b1b58159f7d29e32dd1635a5`。
含固定GeniSpi、DmaTransfer、DmaCache、NativeDmaBuffer，独立archive，没有XNU重链。
输入冻结 `research/touch-dma-native-20261109/source-compiled-v3/`；
同级 `native-build.json`、`native-disassembly.txt`、`cache-test.json` 为实际证据。

## 待接续

v24调查→具体route/共享bank/地址宽度/翻译配置审核→真正lease与mapping provider→
硬件缓冲区生命周期→NT36536 ID/PID→固件/点帧/HID。
若create错误后out非空，必须保留并处理；destroy proof不可使用恒成功占位。
下一发调查包SHA仍d285afd9…，boot1738713600@footer0/父链3，原stock恢复件仍8b8e72da…。
完整充电/真实触控都未完成，不清Goal。
