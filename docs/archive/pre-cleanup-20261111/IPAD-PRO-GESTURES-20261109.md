# iPad Pro 无 Home 键界面候选

用户要求：底部导航条和上滑解锁。官方指南将机型/按钮信息放在 ADT，
无需修改已签名的用户态 plist。此前候选只有 home-button-type=2，仍保留 iPad7,1/J120；
本件补齐无 Home 键机型，作为独立可回退的界面实验。

## 已完成

- model=iPad8,1，target-type=J317，产品名 iPad Pro (11-inch)，pad 界面，素材 subtype=2388。
- /buttons 只列 hold/voldown/volup，不列 menu；home-button-type=2，thin-bezel=1。
- 硬件 compatible、SoC/CPU/内存、GOP/UFS 描述、根卷/信任缓存/512MiB RAM Data 保持原值。
- 原 Data seed 不含 MobileGestalt 缓存，已复核其 packed SHA；没有清理 Android 数据。
- 采用已实测的 v11 充电控制内核 dfc6b8db…；新包仅界面 ADT 和会话标识变化。

候选：`edl_work/basic-bundle-20261108/v11/final/power-control-focused-gestures-ipad-pro/boot-signed.img`

SHA256：`cfef13944464c89d629fb1a5a632297a8cf3d6031d34e076c12fec4375c98f00`

session：`82eaa003c6589bf9`；96MiB，boot1738713600@footer0，父链3。
独立 RSA/data/stock properties、30原始模块、HID ABI、完整资产与 ADT 差异检查通过。
对应 `packet.json`、`independent-audit.json` 与 `boot-signed.signing.json` 保留全部证据。

**尚未刷入。** 导航条、锁屏提示和真实上滑尚未验收；物理触控驱动也还需要独立实测。
本修改提供无 Home 键界面，不提供 Face ID 生物识别硬件。

## 来源与复用

- 官方 [MobileGestalt 指南](https://unixnu.com/zh/guide/mobilegestalt)：本地快照
  `research/UniXNU_Enc3l-night/docs/zh/guide/mobilegestalt.md`。
- iPad8,1 / J317AP 映射与原始响应：`research/ipad-gestures-20261109/`。
- `edl_work/adt_gestures.py` 的默认仍为继承机型；只有显式 `identity=ipad-pro-11`
  或 `package_basic_bundle.py --gestures --ipad-pro` 才选择新机型。
- 输出使用独立 `-ipad-pro` 后缀；旧 v11 手势包和 stock 恢复件保持。

准备命令：

```text
python edl_work/package_basic_bundle.py --version 11 --default power-control --focused --gestures --ipad-pro
python edl_work/sign_verified_candidate.py edl_work/basic-bundle-20261108/v11/final/power-control-focused-gestures-ipad-pro/boot.img edl_work/basic-bundle-20261108/v11/final/power-control-focused-gestures-ipad-pro/boot-signed.img
python edl_work/audit_basic_bundle.py --version 11 --variant power-control-focused-gestures-ipad-pro
```

本轮设备保持原厂 Android；最近只读值67%、4082mV、-483mA、Charging。
换线问题尚未回复，未重启/刷入。后续新写 reference 仍是 stock8b8e72da…，
实验准入仍为 fresh>=3500mV 且 native battery-soc-ok=yes。
