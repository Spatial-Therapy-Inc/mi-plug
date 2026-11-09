# home-switch（家电开关）

Tikpal Voice 的扩展固件：在圆屏上开关家里的小米 Wi-Fi 设备（插座、台灯、风扇……）。走家里的局域网直连设备（miIO 协议，UDP 54321），不经过任何云。

- 一台设备一页：点中间的大圆、或者短按侧键，开 / 关；左右滑换下一台。
- 按住侧键：屏幕上出二维码，用手机扫码打开配置页，添加、导入、改名、删除、试开关都在那里。
- 对 Yay 说「关掉台灯」，Yay 打开这个应用时带 `{"action":"off","device":"台灯"}`，进来直接关。
- 设备换了 IP（路由器重新分配），会在局域网里按设备编号找回来，自动记住新地址。

## 编译、写进圆屏

```bash
./kit build              # 编译，再跑主机测试
./kit install --slot 2   # USB 写进测试圆屏的位置 2，写完直接打开
```

## 添加设备

每台设备要三样：**IP、型号、token**。用 Xiaomi-cloud-tokens-extractor 从自己的小米账号里取一次，加上 `-o devices.json` 把结果存成文件：

```bash
~/.venvs/mi-token/bin/python token_extractor.py --server cn -o devices.json
```

然后用手机设置（推荐）：

1. 在 Tikpal Voice 上打开家电开关，**按住侧键**（没有设备时点「添加设备」），屏幕上出现二维码。
2. 手机连着家里的 Wi-Fi，扫码打开配置页。
3. 「从提取工具导入」选 `devices.json`：能直连的 Wi-Fi 设备会列出来，插座、灯、风扇默认勾上，改好名字点「导入勾选的」。也可以「手动添加一台」。
4. 在页面上点「开」「关」试一下，再点圆屏上的「完成」（或者再按住一下侧键）。

- 文件在电脑上的话，在配置页点「复制这一页的链接」，发到电脑上用浏览器打开，直接在电脑上选文件。
- 配置页只在家里局域网里，不连外网。链接里带一次性的钥匙：圆屏上点「完成」、或者 10 分钟没操作就失效，下次按住侧键是新的。
- 备用：USB 线连着时用 `python3 tools/devices.py add|list|rename|remove|on|off`（要 `pip install pyserial`），token 不回显。

几点：

- **名字**照你会说的起（「台灯」「卧室的插座」）：Yay 按名字找设备，「台灯」也能找到「卧室的台灯」。
- **siid / piid** 是「开关」这个属性的编号，插座、台灯、风扇大多是 2 / 1（导入时都用 2 / 1）。不对的话圆屏上会显示「设备不认这个开关」，到 home.miot-spec.com 查那个型号，找 `switch` 或 `light` 服务下面的 `on` 属性，用「手动添加」同名重加一次（会覆盖）。
- 老款 miIO 插座（`chuangmi.plug.m1`、`v1`、`v3`，米家插线板）自动用老协议，siid / piid 不用管。
- 只支持连着 Wi-Fi 的设备。蓝牙、Zigbee 的设备没有局域网地址，控制不了；导入时也不会列出来。
- 插座重新配网或重置以后 token 会变，要重新取、重新加（同名会覆盖）。
- 最多 8 台。

## 代码

| 文件 | 做什么 |
|---|---|
| `main/main.c` | 启动顺序（照模板）、界面和配置二维码、侧键、Yay 的启动参数、添加 / 删除 / 改名 / 开关（`mi_plug.h`，USB 和网页共用）、存设备（NVS `yk_homeswitch`） |
| `main/webcfg.c`、`main/page.html` | 局域网配置页：esp_http_server，`/api/*` 要钥匙（`X-Key`），页面导入提取工具的 `devices.json` |
| `main/miio_lan.c` | UDP 收发：hello、一次请求和回答；换了 IP 时广播 hello 找回来 |
| `main/miio_proto.c` | miIO 报文：32 字节头、MD5 校验、AES-128-CBC（纯 C） |
| `main/plug_logic.c` | 设备清单、每种型号怎么开关、回答怎么读（纯 C） |
| `main/mjson.c`、`main/mp_crypto.c` | 小 JSON 读取器、MD5 / AES（纯 C） |
| `test/test_plug.c` | 主机测试：加密对标准向量，miIO 报文和 python-miio 的算法逐字节对 |
| `test/test_page.mjs` | 配置页的导入：拿提取工具格式的文件测（有 Node.js 时 `test/run.sh` 跑它） |
| `tools/devices.py` | 备用：在电脑上经 USB 加、删、改名、列、开关设备 |

token 是密钥：日志里只打计数和错误码，不打 token、不打设备回的原文；列设备（USB 和网页）都不回 token；网页收到的请求体用完就清零；`devices.py` 只把它放在 `yk cmd … -` 那条不回显的参数行里。

`devkit/` 是开发包（git 子模块），钉在一个版本上，别改它。
