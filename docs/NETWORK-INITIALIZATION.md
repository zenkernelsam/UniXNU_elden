# About初始化与原版网络组件

v106已修复managed RT surface上的DC ZVA异常，用户可以打开Settings和General，
但About起初停在_macAddress。两次线程采样显示SystemConfiguration的dispatch_once
等待IOKit quiescence，后两次退出等待，用户也看到About页面。

v107按sheng已有busyReport方式增加有界观察，六次完整遍历均busy0，AES存在，
IONetworkStack缺失。不能把这解释成某个永远busy的驱动，也不需要套用缺AES的修补。
官方aston路线提到缺IONetworkStack造成InterfaceNamer约300秒超时；这提供方向，
并不独立证明本机所有等待时间都由它造成。

同一21H461原kernelcache包含IONetworkingFamily3.4，原IONetworkStack personality
匹配IOResources/IOBSD。v108将其加入预链接，保留原始代码和依赖，实机六次均
providers3/busy0，证明服务已注册；没有造假接口、MAC或缩短等待期限。

v108无人工触控/软件About导航，About耗时改善仍待测；Wi-Fi真实电源、PCIe/MHI/DMA
和网络收发仍未接通。代码与构建入口在profiles/v107-iokit-busy及v108-network-family。
该次日志drop36，不能声称无丢日志。设备已经完整恢复stock Android/root/green并熄屏。
