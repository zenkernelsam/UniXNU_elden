# Elden 持久 Data 前置

新Data pool布局/主备GPT验证/精确WRITE与SYNC保留/块同步和读回/freeze fence已iOS17编译、重链与预链接。
NativeUfsBlock保留只读根卷，Data store走原gHost owner。原RamBlock/DirectDisk组件ID、类名及协议保持。
`ELDEN_DATA_STORE=1`编译板级适配，默认`sheng-data-persist=0`不开放写；只读调查另用
`ELDEN_DATA_INSPECT_BUILD=1`与`elden-data-layout-inspect=1`。

本机LUN0/ipados固定范围/原root整头SHA重新门检，布局为两个2GiB stage槽、112GiB Data pool和独立日志。
初期512MiB seed的store首址不移动。旧Data seed/CRC/foreign不匹配明确拒绝，全零新头且review才允许初始化。
写完成要求精确CDB/边界、现有UPIU/SCSI/OCS检查、显式cache sync和完整读回，未知结果隔离不重试。

持久RPC走完整VFS shutdown，最后unmount后关闭/排空写并同步，最终reset须fence完成。
panic只观察无锁原子状态，在途或不明写停止自动复位；RAM控制件的原回程保持。
只读两档候选已签，未刷，无NAND写授权。新核d78fe346…/52265064B，stock1738713600/footer0。
40+56+16+16主机断言、原30执行模块身份（既有AMFI单字保持）及207槽/52HID绑定通过。

完整候选SHA/session、固定源码与复现入口在工作仓
`research/HANDOVER-PERSISTENT-FOUNDATION-20261108.md`，source-compiled-final75份源。
平板留在Android补电，未执行设备实验。真实落盘/崩溃恢复、扩大Data、Keychain/安全密钥与加密仍未验。
RAM KeyStore guard保留，这不是完整持久Data或日用iPad验收。
