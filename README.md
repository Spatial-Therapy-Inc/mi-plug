# mi-plug

一个 Tikpal 应用（Tikpal Voice 的扩展固件），用 Tikpal 开发包做的。

```bash
./kit build              # 编译，再跑主机测试
./kit install --slot 2   # USB 写进测试圆屏的位置 2，写完直接打开
./kit logs               # 看日志；./kit snap 截图，./kit heap 看内存
./kit build --release    # 提交用的那份
```

- `devkit/` 是开发包（git 子模块），钉在一个版本上，别改它。`git clone` 这个仓库以后，`./kit` 会自己把它拉下来。
- 应用清单是 `app.json`；逻辑写在 `main/`，能不依赖 ESP-IDF 的部分写成纯 C，`test/run.sh` 在电脑上测。
- 做完、自测过，照 `devkit/AGENTS.md` 开提交单。
