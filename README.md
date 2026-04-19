# xito(X input to output)

一个极简风格的 X11 文本编辑器。没有菜单、没有按钮、没有标题栏——只有一个输入框和纯粹的文字体验。灵感来自 dmenu，专为键盘操作和命令行工作流设计。

## 特性

- **极简界面** — dmenu 风格无边框窗口，无标题栏、无菜单、无按钮
- **中文输入** — 完整的 XIM/XIC 输入法支持，兼容 ibus、fcitx、scim 等主流框架
- **现代渲染** — 基于 Pango + Cairo 的文字排版引擎，支持 Xft2 字体、自动折行、复杂 Unicode 排版
- **UTF-8 原生** — 全链路 UTF-8 编码，完整支持中文、日文、韩文及各类 Unicode 字符
- **Markdown 高亮** — 标题、粗体、斜体、下划线、删除线、高亮、行内代码、链接、引用的实时语法着色
- **当前行高亮** — 焦点行清晰可见，非焦点行自动变暗
- **主题系统** — 4 个内置主题 + 自定义主题文件，一键切换视觉风格
- **管道友好** — 支持 stdin 管道输入，Ctrl+S 将编辑内容输出到 stdout
- **纯键盘操作** — 无鼠标依赖，所有功能通过快捷键完成

## 编译

### 依赖

| 库 | pkg-config 名称 | 作用 |
|----|-----------------|------|
| X11 | `x11` | X11 窗口系统 |
| Xft | `xft` | Xft2 字体渲染 |
| Pango Cairo | `pangocairo` | 文字排版与绘制 |
| Cairo Xlib | `cairo-xlib` | Cairo X11 后端 |

**安装依赖：**

```bash
# Debian / Ubuntu
sudo apt-get install libx11-dev libxft-dev libpango1.0-dev libcairo2-dev

# Fedora / RHEL
sudo dnf install libX11-devel libXft-devel pango-devel cairo-devel

# Arch Linux
sudo pacman -S libx11 libxft pango cairo
```

### 编译命令

```bash
make
```

手动编译：

```bash
gcc -Wall -Wextra -O2 -std=c11 \
    $(pkg-config --cflags x11 xft pangocairo cairo-xlib) \
    editor.c -o editor \
    $(pkg-config --libs x11 xft pangocairo cairo-xlib)
```

### 安装

```bash
make install          # 安装到 /usr/local/bin
make install DESTDIR=/usr  # 指定安装前缀
```

## 使用方法

### 基本启动

```bash
./editor                              # 默认 600x400，居中显示
./editor -w 800 -h 500                # 指定窗口大小
./editor -x 100 -y 200                # 指定窗口位置
./editor -w 1024 -h 600 -x 0 -y 0    # 全部自定义
```

### 管道输入输出

```bash
# 编辑文本，Ctrl+S 输出到 stdout
echo "Hello World" | ./editor

# 捕获输出到文件
./editor < input.txt > output.txt

# 与其他命令组合
cat config.yaml | ./editor -w 600 | tee config_new.yaml
```

### 字体设置

```bash
# 使用 Pango 格式的字体描述
./editor -fn "Monospace 14"

# 指定中文字体
./editor -fn "Noto Sans CJK SC 14"

# 指定衬线字体和大小
./editor -fn "Noto Serif 16"
```

### 颜色设置

```bash
# 设置背景色和前景色
./editor -bg "#1a1b26" -fg "#c0caf5"

# 使用主题颜色作为基础，只覆盖前景色
./editor -t tokyonight -fg "#ffffff"
```

### Markdown 模式

```bash
# 启用 Markdown 语法高亮 + 当前行高亮
./editor -m -z

# 使用 Dracula 主题编辑 Markdown
./editor -m -t dracula

# 组合所有选项
./editor -m -z -w 800 -h 600 -fn "Noto Sans Mono 14" -t nord
```

### 选择主题

```bash
# 内置主题
./editor -t tokyonight     # 默认，深邃蓝紫
./editor -t dracula         # 暗紫经典
./editor -t nord            # 北极冷色
./editor -t gruvbox         # 暖色调复古

# 自定义主题文件
./editor -t ./my-theme.conf
```

## 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-w <宽度>` | 窗口宽度（像素），最小 100 | `600` |
| `-h <高度>` | 窗口高度（像素），最小 50 | `400` |
| `-x <x>` | 窗口水平位置（像素） | 居中 |
| `-y <y>` | 窗口垂直位置（像素） | 居中 |
| `-bg <颜色>` | 背景色（十六进制，如 `#1a1b26`） | 主题背景色 |
| `-fg <颜色>` | 前景色（文字颜色） | 主题前景色 |
| `-fn <字体>` | 字体描述（Pango 格式） | `Monospace 13` |
| `-z` | 当前行高亮，其他行变暗 | 关闭 |
| `-m` | 启用 Markdown 语法高亮 | 关闭 |
| `-t <主题>` | 主题名称或主题文件路径 | `tokyonight` |
| `--help` | 显示帮助信息并退出 | — |

## 快捷键

### 核心操作

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+S` | 保存：将编辑内容输出到 stdout 并退出 |
| `Esc` | 取消编辑并退出 |
| `Enter` | 插入换行 |
| `Backspace` | 删除光标前一个字符 |
| `Delete` | 删除光标后一个字符 |
| `Tab` | 插入 4 个空格 |

### 光标移动

| 快捷键 | 功能 |
|--------|------|
| `Left` / `Right` | 左右移动一个 UTF-8 字符 |
| `Up` / `Down` | 上下移动一行 |
| `Home` | 跳转到行首 |
| `End` | 跳转到行尾 |
| `Ctrl+Home` | 跳转到文档开头 |
| `Ctrl+End` | 跳转到文档末尾 |
| `Ctrl+Left` | 向左移动一个单词 |
| `Ctrl+Right` | 向右移动一个单词 |
| `Page Up` | 向上翻页 |
| `Page Down` | 向下翻页 |

### 选区操作

| 快捷键 | 功能 |
|--------|------|
| `Shift+Left/Right` | 逐字符扩展选区 |
| `Shift+Up/Down` | 逐行扩展选区 |
| `Shift+Home/End` | 选区扩展到行首/行尾 |
| `Shift+Ctrl+Left/Right` | 逐单词扩展选区 |
| `Shift+Ctrl+Home/End` | 选区扩展到文档首/尾 |
| `Shift+Page Up/Down` | 翻页并扩展选区 |
| `Ctrl+A` | 全选 |

### 编辑操作

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+K` | 删除从光标到行尾的内容 |
| `Ctrl+U` | 删除从行首到光标的内容（非 Markdown 模式） |
| `Ctrl+W` | 删除光前一个单词 |
| `Ctrl+D` | 删除光标后一个单词 |
| `Ctrl+F` | 向前移动一个单词（等同于 Ctrl+Right） |
| `Ctrl+B` | 向后移动一个单词（非 Markdown 模式） |
| `Ctrl+J` | 插入换行 |
| `Ctrl+L` | 重绘屏幕 |

### Markdown 快捷键（需 `-m` 参数）

| 快捷键 | 功能 | 插入语法 |
|--------|------|----------|
| `Ctrl+B` | 粗体 | `**文本**` |
| `Ctrl+I` | 斜体 | `*文本*` |
| `Ctrl+U` | 下划线 | `__文本__` |
| `Ctrl+`` | 删除线 | `~~文本~~` |
| `Ctrl+=` | 高亮 | `==文本==` |
| `Ctrl+1` | 一级标题 | `# 标题` |
| `Ctrl+2` | 二级标题 | `## 标题` |
| `Ctrl+3` | 三级标题 | `### 标题` |
| `Ctrl+4` | 四级标题 | `#### 标题` |
| `Ctrl+5` | 五级标题 | `##### 标题` |
| `Ctrl+6` | 六级标题 | `###### 标题` |

> 如果有选中文本，Markdown 快捷键会包裹选区内容；如果没有选中文本，则在光标处插入空的标记对并将光标放在中间。`Ctrl+1`~`Ctrl+6` 会替换当前行已有的标题级别。

## 内置主题

### tokyonight（默认）

基于 Tokyo Night 配色方案，深邃的蓝紫色调。

```
背景  #1a1b26    前景  #c0caf5    光标  #c0caf5
标题  #7aa2f7 → #f7768e（H1 到 H6 递进）
粗体  #ff9e64    斜体  #bb9af7    下划线  #73daca
删除线  #565f89    高亮  #292e42（背景色）
代码  #e0af68    链接  #7aa2f7    引用  #565f89
```

### dracula

经典 Dracula 暗色主题，紫粉色调。

```
背景  #282a36    前景  #f8f8f2    光标  #f8f8f2
标题  #bd93f9 → #ffb86c
粗体  #ffb86c    斜体  #bd93f9    下划线  #8be9fd
删除线  #6272a4    高亮  #44475a（背景色）
代码  #f1fa8c    链接  #8be9fd    引用  #6272a4
```

### nord

北极冷色调主题，清新柔和。

```
背景  #2e3440    前景  #d8dee9    光标  #d8dee9
标题  #88c0d0 → #bf616a
粗体  #d08770    斜体  #b48ead    下划线  #88c0d0
删除线  #4c566a    高亮  #3b4252（背景色）
代码  #ebcb8b    链接  #88c0d0    引用  #4c566a
```

### gruvbox-dark

暖色调复古主题，灵感来自 Gruvbox。

```
背景  #282828    前景  #ebdbb2    光标  #ebdbb2
标题  #458588 → #cc241d
粗体  #fe8019    斜体  #b16286    下划线  #689d6a
删除线  #504945    高亮  #3c3836（背景色）
代码  #d79921    链接  #458588    引用  #665c54
```

## 自定义主题

创建一个文本文件，每行一个 `键=值` 对，以 `#` 开头的行为注释。未指定的颜色会回退到 tokyonight 的默认值。

### 支持的颜色键

| 键 | 说明 | 示例 |
|----|------|------|
| `name` | 主题名称（仅标识用） | `name=My Theme` |
| `bg` | 背景色 | `bg=#1a1b26` |
| `fg` | 前景色（正文颜色） | `fg=#c0caf5` |
| `cursor` | 光标颜色 | `cursor=#ffffff` |
| `dim` | 暗淡色（非当前行变暗、边框） | `dim=#3b4261` |
| `selection_bg` | 选区背景色 | `selection_bg=#283457` |
| `h1` | 一级标题颜色 | `h1=#7aa2f7` |
| `h2` | 二级标题颜色 | `h2=#7dcfff` |
| `h3` | 三级标题颜色 | `h3=#9ece6a` |
| `h4` | 四级标题颜色 | `h4=#e0af68` |
| `h5` | 五级标题颜色 | `h5=#bb9af7` |
| `h6` | 六级标题颜色 | `h6=#f7768e` |
| `bold` | 粗体文字颜色 | `bold=#ff9e64` |
| `italic` | 斜体文字颜色 | `italic=#bb9af7` |
| `underline` | 下划线文字颜色 | `underline=#73daca` |
| `strikethrough` | 删除线文字颜色 | `strikethrough=#565f89` |
| `highlight_bg` | 高亮背景色（==标记语法） | `highlight_bg=#292e42` |
| `code_bg` | 行内代码背景色 | `code_bg=#292e42` |
| `code_fg` | 行内代码前景色 | `code_fg=#e0af68` |
| `link` | 超链接颜色 | `link=#7aa2f7` |
| `blockquote` | 引用块颜色 | `blockquote=#565f89` |

### 示例主题文件

```ini
# my-theme.conf — Solarized Dark

name=solarized-dark
bg=#002b36
fg=#839496
cursor=#93a1a1
dim=#073642
selection_bg=#073642

h1=#268bd2
h2=#2aa198
h3=#859900
h4=#b58900
h5=#d33682
h6=#6c71c4

bold=#cb4b16
italic=#6c71c4
underline=#2aa198
strikethrough=#586e75
highlight_bg=#073642

code_bg=#073642
code_fg=#b58900
link=#268bd2
blockquote=#586e75
```

使用自定义主题：

```bash
./editor -t ./my-theme.conf -m
```

## 中文输入

编辑器通过 XIM（X Input Method）协议支持中文及其他 CJK 语言输入。确保系统已正确配置输入法：

```bash
# 确认 XMODIFIERS 环境变量已设置（通常由桌面环境自动配置）
echo $XMODIFIERS
# 输出示例：@im=fcitx 或 @im=ibus

# 启动编辑器
./editor
```

编辑器支持两种 XIM 预编辑模式：

- **PreeditCallbacks** — 应用内联显示正在输入的文字（需要输入法支持）
- **PreeditNothing** — 输入法自行管理候选窗口（最常见的模式，兼容 ibus/fcitx 的 XIM 桥接）

启动时，编辑器会在 stderr 输出输入法状态信息，便于排查问题：

```
editor: IM supports 4 styles:
  style[0] = 0x0408 (preedit=Nothing, status=Nothing)
editor: input method opened, style=0x0408, preedit=Nothing (IM-managed)
```

## 典型用法

### 作为脚本交互式输入器

```bash
# 弹出编辑框，用户输入后输出到文件
./editor -w 500 -h 40 -fn "Monospace 14" -t nord > user_input.txt
```

### 快速编辑 Markdown

```bash
./editor -m -z -w 800 -h 600 -fn "Noto Sans Mono 15" -t tokyonight
```

### 极简便签

```bash
# 小窗口、居中、暖色主题
./editor -w 400 -h 200 -t gruvbox
```

### 在脚本中使用

```bash
#!/bin/bash
NOTE=$(./editor -w 500 -h 100 -fn "Monospace 12" -bg "#2d2d2d" -fg "#cccccc")
echo "你写了: $NOTE"
```

## 许可证

MIT License
