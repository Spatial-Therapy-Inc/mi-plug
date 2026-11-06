# mi-plug（米家开关）

Tikpal Voice 的扩展固件：在圆屏上开关家里的小米 Wi-Fi 设备（插座、台灯、风扇……）。走家里的局域网直连设备（miIO 协议，UDP 54321），不经过任何云。**自用原型，不上架。**

- 一台设备一页：点中间的大圆、或者短按侧键，开 / 关；左右滑换下一台。
- 对 Yay 说「关掉台灯」，Yay 打开这个应用时带 `{"action":"off","device":"台灯"}`，进来直接关。
- 设备换了 IP（路由器重新分配），会在局域网里按设备编号找回来，自动记住新地址。

## 编译、写进圆屏

```bash
./kit build              # 编译，再跑主机测试
./kit install --slot 2   # USB 写进测试圆屏的位置 2，写完直接打开
```

## 添加设备

每台设备要三样：**IP、型号、token**。用 Xiaomi-cloud-tokens-extractor 从自己的小米账号里取一次（米家 App 扫码登录），再在电脑上用 USB 加进圆屏：

```bash
pip install pyserial                 # 一次
python3 tools/devices.py add         # 问名字、IP、型号、siid/piid，token 不回显
python3 tools/devices.py list        # 看圆屏上有哪些（不显示 token）
python3 tools/devices.py on 台灯     # 在电脑上开关，试通用
python3 tools/devices.py remove 台灯
```

- **名字**照你会说的起（「台灯」「卧室的插座」）：Yay 按名字找设备，「台灯」也能找到「卧室的台灯」。
- **siid / piid** 是「开关」这个属性的编号，插座、台灯、风扇大多是 2 / 1。不对的话圆屏上会显示「设备不认这个开关」，到 home.miot-spec.com 查那个型号，找 `switch` 或 `light` 服务下面的 `on` 属性。
- 老款 miIO 插座（`chuangmi.plug.m1`、`v1`、`v3`，米家插线板）自动用老协议，siid / piid 不用管。
- 只支持连着 Wi-Fi 的设备。蓝牙、Zigbee 的设备没有局域网地址，控制不了。
- 插座重新配网或重置以后 token 会变，要重新取、重新 `add`（同名会覆盖）。
- 最多 8 台。

## 代码

| 文件 | 做什么 |
|---|---|
| `main/main.c` | 启动顺序（照模板）、界面、侧键、Yay 的启动参数、`yk cmd miplug.*`、存设备（NVS `yk_miplug`） |
| `main/miio_lan.c` | UDP 收发：hello、一次请求和回答；换了 IP 时广播 hello 找回来 |
| `main/miio_proto.c` | miIO 报文：32 字节头、MD5 校验、AES-128-CBC（纯 C） |
| `main/plug_logic.c` | 设备清单、每种型号怎么开关、回答怎么读（纯 C） |
| `main/mjson.c`、`main/mp_crypto.c` | 小 JSON 读取器、MD5 / AES（纯 C） |
| `test/test_plug.c` | 主机测试：加密对标准向量，miIO 报文和 python-miio 的算法逐字节对 |
| `tools/devices.py` | 在电脑上经 USB 加、删、列、开关设备 |

token 是密钥：日志里只打计数和错误码，不打 token、不打设备回的原文；`miplug.list` 不回 token；`devices.py` 只把它放在 `yk cmd … -` 那条不回显的参数行里。

`devkit/` 是开发包（git 子模块），钉在一个版本上，别改它。
