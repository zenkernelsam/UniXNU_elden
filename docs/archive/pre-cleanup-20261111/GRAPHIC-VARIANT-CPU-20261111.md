# v89真实调用链已命中系统符号栅格化；CPU合成核心已测试

2026-11-11。完整Goal active。当前唯一主工单仍是Settings内容；输入、等待时长和CPU准入不再重复试。

## v89实机证明了什么

v89核`48d4c93c12222bb2581d331a4477ff754bb972e8db201c1ea8c2146995b1bc7b`，像`4ac8690e7d113221a179953b30b102804476d5352db118f9f84c3876110af038`，session`f8d8321a542c9209`，fresh4430mV/yes，boot_a全SHA与1738713600@0通过。205来源与原CPU/显示/电源输入核验，真实调用为故障回调→状态匹配→walker→copyin→原子输出。

日志`edl_work/live-icon-chain-v89-20261111/usb`共5057065B，SHA`0144c4bd57e624fc59dd4fe052d021fe9d806dcfbf85034c9084be260bee6266`，drop1400，无断段/重连。**只验收到一条同时有完整故障/slide信息的原子16层链**，PID314，reason5表示到上限截断，不当作全部用户栈或全日志无损。它已经包含所需的实际调用方。解析文件在`icon-fault-trace-20261111/live-v89/`，原SystemOS和symbols UUID比对成功，Mac只读挂载已卸载。

关键栈从内到外：

1. `CI::GLContext::init()+0xdc`。
2. GLContext构造→internalContextWithEAGLContext→CIContext init/contextWithOptions。
3. `+[CUIShapeEffectStack sharedCIContext]`。
4. `-[_CUIGraphicVariantVectorGlyph _createGraphicVariantImageAtScale:backgroundImage:symbolImage:destinationRect:disableColorFill:outputScale:]+0x224`。
5. 同类`rasterizeImageUsingScaleFactor:forTargetSize:`。
6. `IFSymbol imageForGraphicSymbolDescriptor:`→`ISGraphicSymbolResource imageForSize:scale:`。
7. `ISCompositor scaledImageFromContent:...`→drawLayer→drawElement。

资源的imageForSize发生在ISCompositor的canUseCoreImage判断之前，因此已安装的renderingMode=0不覆盖这条路径。不是补丁没加载，也不是需要继续猜触控坐标。不要只改canUseCoreImage再次碰同一故障。

## 精确边界与ABI

原CoreUI UUID `d62914c6c4dc3c00a175b515a6b901a3`。目标IMP `0x1a318679c`，入口16字节`ff4302d1ef3b016ded33026deb2b036d`。

原method-list类型为：

```text
^{CGImage=}84@0:8d16^{CGImage=}24^{CGImage=}32{CGRect={CGPoint=dd}{CGSize=dd}}40B72d76
```

共8个参数（含self/_cmd）：CGFloat sourceScale、CGImageRef background、CGImageRef symbol、CGRect destination、BOOL disableColorFill、CGFloat outputScale。**outputScale是double值，不是输出指针**；实际arm64代码用d5。返回为拥有的CGImageRef，父调用随后释放两个输入CGImage。

`options` getter IMP1a3187744，类型`@16@0:8`。`CUIVectorGlyphGraphicVariantOptions monochromeForegroundColor` IMP1a3187d04也为`@16@0:8`，**返回对象，不能直接当CGColorRef**。后续wrapper需确认对象类型及有效CGColor访问器；CoreUI有`-[CUIColor CGColor]`@1a31ac518，但尚未完成其完整类型/运行时资格。

ABI记录`graphic-variant-cpu-20261111/abi.json`来自原只读缓存。方法表低两位为flags，条目长度掩码fffc；共享缓存direct-selector采用单独base，提取器按原方法符号/IMP定位类型，**运行时仍必须同时验证selector、IMP、encoding、UUID与机器码**。

不要转调同类`drawInContext:`来“用Quartz”：它又调用self.image，会回到栅格化路径。目标函数本身无纯CPU分支，会构造CIImage、效果、offset/scale/composite/crop并无条件请求sharedCIContext。背景已通过原CoreGraphics路径创建；符号CGImage由原父类光栅化提供。

## 已完成的CPU核心（未接入方法/镜像）

canonical `package/EldenGraphicVariantCpu.c/h` 与 `TestGraphicVariantCpu.c`：

- 使用实际CGBitmapContext合成输入的背景和符号，不返回占位图或假成功。
- 对齐原offset后scale公式：位置为destination.origin×outputScale，符号尺寸为输入像素×outputScale/sourceScale，裁切到背景尺寸。
- 可通过经校验的CGColorRef按符号alpha着色；disableColorFill保持原符号色。
- 保留背景RGB颜色空间，输出8位RGBA；尺寸、有限值、缩放后范围和最大64MiB输出有界。
- 新图拥有独立生命周期；不借用将被父调用释放的输入像素。

真实Mac CoreGraphics ASan/UBSan测试通过：placement/scale、tint/bypass、alpha、非法值/极端比例、释放输入后结果仍有效。iPhoneOS SDK编译出arm64/iOS17对象，导入无直接CoreImage/GL/Metal符号。`core-build.json`、`native-audit.json`及源/对象在`graphic-variant-cpu-20261111/`，Mac同相对目录。

**边界不能隐去：尚未实现额外CI content/shape阴影等效果，8位输出也不是完整宽色深等价；还没有实机证明所有输入provider都不依赖CI。** 这是GPU-last阶段的基础CPU合成核心，不是完整图形效果验收。完整Goal保留这些缺项，不能用它宣布最终parity。

## 下一步具体工作

1. 为上述目标写仅iconservicesagent/显式开启的typed wrapper；复用现有ISCompositor mode0，与原CoreUI UUID/IMP/方法encoding和实例类别一起核验。正确取得前景色对象的CGColor，不能乱转指针。
2. 接入真实CPU核心，准备有限且可辨识的安装/渲染结果日志；不要借“拒绝协议”打印数据，也不要仅发送旧installed标记冒称新方法已安装。
3. 更新实际用户态库、payload/trustcache、源/ABI资格并链接候选；当前没有新wrapper、dylib、v90整核或签名件。保留v89故障链与四帧观察，验真实符号图、图标服务不再重复GL崩溃、Settings内容和回程。
4. 无触点实指资格时不宣称全部触控通过；基础页面通后再集中真实手指验收。不改显示缓存、不提前GPU、不分散到新的充电探针。

## 当前设备

v89的240秒hold正常到期（240031ms）后PC ACK/sync/SDAM回实际fastboot。已stock`8b8e72da…`完整SHA恢复，`edl_work/icon-chain-v89-stock-20261111`确认root/green/boot_completed1，100%/4469mV/0mA/25.7°C/Full，已熄屏Dozing。所有logger/watcher/构建结束；next reference=stock/fresh。一处首次恢复命令重定向路径拼错，进程未启动；随后正确目录恢复成功，未发生错误镜像写入。

所有恢复、floor、电气/温控、声音先讨论、GPU最后约束保持。
