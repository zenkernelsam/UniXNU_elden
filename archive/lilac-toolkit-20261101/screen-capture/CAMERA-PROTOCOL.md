# 摄像头（DroidCam）使用协议 — 唯一正确流程

> 地址：`http://192.168.68.60:4747`（旧 .57 作废）
> 适用：一切屏幕取证（shim 页、内核 console、信标、panic）。

## 正确顺序（每次都一样，缺一步就会糊/黑/失败）

```bash
CAM=http://192.168.68.60:4747
curl -m 5 "$CAM/override"          # 1. 先救活/踢掉残留会话（关键！）
sleep 1
curl -m 5 "$CAM/cam/1/af"          # 2. 自动对焦
sleep 2                            # 3. 等镜头收敛
curl -m 45 -o out.mjpeg "$CAM/video?1280x720"   # 4. 录 MJPEG 视频流
# 5. 从流里抽 JPEG（SOI ff d8 / EOI ff d9），不要连拍单帧
```

抽帧（Python）：

```python
d=open('out.mjpeg','rb').read(); pos=0; n=0
while True:
    s=d.find(b'\xff\xd8',pos); e=d.find(b'\xff\xd9',s+2) if s>=0 else -1
    if s<0 or e<0: break
    n+=1
    if n%5==0: open(f'f{n:04d}.jpg','wb').write(d[s:e+2])
    pos=e+2
```

## 禁止 / 兜底

- **禁止连拍单帧**（`frame.jpg` 连续请求会占用会话、糊、丢帧）。
- `frame.jpg` 仅在视频流拿不到时兜底一次。
- **`grab_cam.py` 永久停用**（会打开视频流不放，造成 busy）。
- 出现 `DroidCam busy` → 先 `curl "$CAM/override"`，再重复上面 1-5。
- 照片是横的（手机横放）：读之前 `rotate(90, expand=True)`。
- 如果画面糊：再 override+af+2s 重录；必要时用锐度（拉普拉斯）挑帧。

## 为什么必须 override

DroidCam 只允许一个视频会话；上一次 curl 超时/中断会留下“僵尸占用”，
之后 AF 和 stream 都会返回 busy/黑屏。`/override` 是唯一官方解救入口
（不是万能重启，卡死过久才用）。
