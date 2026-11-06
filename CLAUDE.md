# 这是一个 Tikpal 应用

给 Tikpal Voice 做的扩展固件，用 Tikpal 开发包做的，装在圆屏的位置 2、3，由 Yay 打开，双击侧键开应用环回到 Yay。

- 做任何事先读 `devkit/AGENTS.md`（七步、每步交什么、不能做的事、不收 PR）和 skill `.claude/skills/tikpal-app`（写代码的规矩、组件用法、怎么测）。
- `devkit/` 是开发包，只读：不改里面任何文件。组件缺能力就记下来，在开发包仓库开「需求」单。
- 用 `./kit` 编译、测试、写进圆屏；只写位置 2 或 3，绝不擦整片 Flash。
- 跟用户说话用中文。
