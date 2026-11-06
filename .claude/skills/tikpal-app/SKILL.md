---
name: tikpal-app
description: 用 Tikpal 开发包做 Tikpal Voice 应用（扩展固件）的规矩和做法。在这个应用仓库里写代码、改界面、加功能、编译、写进测试圆屏、看日志和内存、自测、准备提交单时使用。Use for any work in a Tikpal app repository made by `kit new`: the round screen extension's code, UI, side key, launch parameters and results, cloud calls, building with ./kit, installing over USB, self-testing and submitting.
---

# Tikpal 应用

这个仓库是一个 Tikpal Voice 应用（扩展固件）：装在圆屏的位置 2 或 3，由 Yay 打开；用户双击侧键开应用环、点中间回到 Yay。同一时间圆屏上只跑一个固件，打开和回去都是重启。

## 先读（按需要）

| 要做的 | 读 |
|---|---|
| 任何开发 | `devkit/docs/app-guide.md`（资源归谁、启动顺序、启动参数和结果、侧键、组件速查、内存） |
| 界面 | `devkit/docs/round-screen-design.md`（颜色、字号、安全圆、提示、文案） |
| 应用清单 | `devkit/docs/app-json.md` |
| 流程、自测、提交 | `devkit/AGENTS.md`（先读）、`devkit/docs/checklist.md` |
| 某个组件怎么用 | `devkit/firmware/components/<组件>/include/*.h`（头文件是准的）和组件的 README |
| 照着改的例子 | 本仓库一开始就是模板：`main/main.c` 的启动顺序、侧键、收尾都是对的 |

## 流程和不能做的事

从立项到上架的七步、每步交什么、提交单怎么写，见 `devkit/AGENTS.md`，照它做。开发包仓库不收 PR。

### 不能做的事

从 `devkit/AGENTS.md` 第 2 节原样抄来，两边一字不差（导出开发包时会核）：

1. 不改 `devkit/` 下的任何文件，包括公共组件；缺什么开「需求」单。
2. 不碰签名：不找、不要、不生成发布密钥，不自己打签名包。签名只在 Tikpal 的发布机上做。
3. 不在公开单子、提交说明、代码里写账号、邮箱、手机号、设备编号、令牌。日志用 `./kit logs`、`./kit info` 出来的，设备编号已经抹掉了。
4. 版本号只往上加：每次提交都比上一次大；USB 写进测试机的版本也不能比应用市场上的低。不然圆屏查更新时会把它当成旧版，一点更新就换成应用市场上的版本。
5. 立项单回复以前，不开始要云能力的开发。不要云能力的可以先写。
6. 不给这个仓库开 PR。
7. 不擦整片 Flash，不写位置 1（主固件）和恢复器，只用 `./kit install --slot 2|3`。
8. 不写死服务器地址和密钥；云能力只走开发包的接口。

## 硬规则

1. **`devkit/` 只读。** 它是开发包（git 子模块），钉在一个版本上。不改、不复制一份改。组件缺什么，记下来告诉用户，在开发包仓库开「需求」单，由 Tikpal 做进下一版开发包。
2. **不擦圆屏。** 只用 `./kit install --slot 2|3` 写一个位置；不用 esptool 整片写、不擦 Flash，不碰位置 1（Yay）和恢复器。
3. **`yk_app_start()` 是 NVS 之后第一个调用**；画好主界面、控制台起来以后 `yk_app_ready()`。
4. **侧键**：用 `yk_app_on_key()`，`yk_key_init` 开 `delay_tap`；双击留给系统（应用环）；有自己的语音对话就设 `talks`。不轮询 GPIO0。
5. **资源各有其主**：屏幕先 `board_lock()`；录音只走 `yaykit_audio`，放音只走 `yaykit_speak`（不碰 GPIO43）；Wi-Fi 只 `yk_wifi_start()`（不配网）；令牌只用 `yk_account_token()`；回 Yay 用 `yk_return_home()`，不直接改启动分区。
6. **NVS**：只写自己的命名空间 `yk_<简称>`（≤ 15 个字符）；绝不 `nvs_flash_erase()`。
7. **名字**：项目名 `yaykit_<slug，连字符换下划线>`；记录的 kind、`yk cmd` 的命令名用去掉连字符的简称做前缀（`mytimer.session`），只用小写字母、数字和点。
8. **界面**：只用 `yk_round.h` 的主题色和字号（同一目录的 `yk_theme.h` 是生成它的底层令牌，数值和圆屏规范不完全一样，应用里别直接用），不写死颜色，不用蓝色；文字在半径 200 的安全圆里；正文 22–24（三行放不下可以用 20），说明不小于 16，不为塞字缩字；说人话，不显示错误码。
9. **内存**：内部 RAM 从 ready 起的最低（`./kit info` 的 `heap.internalMinSinceReady`）不低于 40,000 字节，低于 60,000 字节在提交单里写原因；大东西放 PSRAM。
10. **云**：调之前 `yk_cloud_has()`；限额从 `/me.capabilities` 读；提示词和模型不写进固件；忽略不认识的字段。
11. **安全**：代码、日志、提交单里不出现密钥、令牌、账号、设备编号（`kit logs`、`kit info` 已经把设备编号抹掉了）。
12. **版本**：`app.json` 的 `version` 和 `CMakeLists.txt` 的 `PROJECT_VER` 一起改；`minYay`、`apiLevel` 照开发包（`./kit version`）。

## 常用

```bash
./kit build              # 编译 + 主机测试 + 镜像不超过 3.5 MB
./kit test               # 只跑主机测试
./kit install --slot 2   # USB 写进位置 2 并打开
./kit logs 60            # 日志；YAY_APP 开头的是 yaykit_app 打的
./kit snap               # 截图 snap.png，对照设计规范
./kit heap               # 内存
./kit info               # 设备、三个位置、heap.internalMinSinceReady
./kit build --release    # 提交用的那份
```

## 做一个功能的顺序

1. 逻辑先写成不依赖 ESP-IDF 的纯 C（像 `main/counter.c`），在 `test/` 加主机测试。
2. 界面在 LVGL 任务里改（LVGL 定时器或回调里），或者拿着 `board_lock()`。侧键回调在侧键任务里跑，只设标志，别在里面画。
3. `./kit build`，`./kit install --slot 2`，用起来，`./kit snap` 对设计，`./kit heap` 看内存。
4. 双击开应用环回 Yay，再从 Yay 打开回来，确认收尾（`yk_app_on_leave`）写的结果 Yay 念得对。

## 汇报

用中文。说清改了什么、在哪测的（主机测试、真机、截图）、哪些没测。没上过真机就直说。
