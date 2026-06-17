# CardPuter86

CardPuter86 是运行于 M5Stack Cardputer（ESP32-S3）上的 8086 PC 模拟器，基于 Fake86 与 ESP32TinyFake86，适配了 Cardputer 的键盘、240×135 屏幕、扬声器、内部 Flash 和 microSD。

| 上电自检 | BIOS 启动 |
| --- | --- |
| ![CardPuter86 上电自检](preview/cardputer86-post.png) | ![CardPuter86 BIOS](preview/cardputer86-bios.png) |
| DOS | IBM BASIC |
| ![CardPuter86 DOS](preview/cardputer86-dos.png) | ![CardPuter86 BASIC](preview/cardputer86-basic.png) |

## 功能

- 支持 Cardputer 内置键盘及 Fn 功能层
- DSx86 风格的 40×16 文字视口与全屏缩放模式
- 支持 PC Speaker、CGA 文本与图形输出
- 支持内部 Flash 和 microSD 中的可写 `.img` 磁盘镜像
- 支持 USB 磁盘模式、启动镜像选择和持久化 512KB 内存模式
- 提供 PlatformIO 构建、烧录和 M5Burner 发布打包脚本

## 构建

需要 Python 3 和 PlatformIO Core：

```sh
cd ESP32/CardPuter86
pio run
```

也可在项目根目录仅构建固件和内部 FAT 镜像：

```sh
./flash.sh --build-only
```

## 烧录

普通更新只替换固件，保留用户导入的镜像：

```sh
./flash.sh
```

首次安装或需要恢复默认内部镜像时：

```sh
./flash.sh --with-images
```

`--with-images` 会擦除整个设备，并重新写入分区表、固件和默认 `cardputer86.img`。

## M5Burner 发布包

生成完整的 M5Burner 发布产物，不会烧录设备：

```sh
./flash.sh --package
```

版本号默认读取根目录的 [`VERSION`](VERSION)，也可临时指定：

```sh
./flash.sh --package --version 0.3.5
```

产物位于 `release/M5Burner/`，包含：

- 可导入 M5Burner v3 User Custom 的完整 8MB 合并镜像
- 按烧录偏移命名的 bootloader、分区表、应用和 FAT 镜像
- `m5burner.json`、Flash 布局说明和 SHA-256 校验文件
- 可直接发布的 ZIP 压缩包

完整镜像包含默认内部磁盘，因此安装时会覆盖设备全部 Flash。

M5Burner 投稿封面提供 [SVG 矢量版](preview/cardputer86-cover.svg) 和 [PNG 上传版](preview/cardputer86-cover.png)，打包时会自动复制到发布目录。

## 键盘

- `Fn+1` 至 `Fn+0`：F1 至 F10
- `Fn+-`：F11
- `Fn+=`：F12
- `Fn+\``：Esc
- `Fn+退格`：Delete
- `Aa`：Shift，`Aa+\`1234567890-=` 输出 `~!@#$%^&*()_+`
- `Ctrl`、`Alt`：对应的 PC 修饰键
- `Opt`：切换文字模式与缩放模式
- `Fn+;`、`Fn+,`、`Fn+.`、`Fn+/`：全局方向键上、左、下、右；在文字模式中也用于滚动 40×16 视口
- `Fn+'`：FIXED 模式回到左上初始位置
- `Fn+空格`：暂停模拟器并打开 Settings
- `G0`：电源管理不再使用该键

默认文字模式使用 BSD 许可的 Adafruit Classic 5×7 字形，以 6×8 单元显示 40×16 个字符。缩放模式使用 CC0 许可的 Tom Thumb 3×5 字体显示完整文本屏幕，图形模式则缩放至全屏。

文字视口默认处于 AUTO 模式，自动跟随正文最后一行，并尝试识别和固定底部最多两行状态栏。使用任意 Fn 方向组合后进入 FIXED 模式，不再自动滚动。

## 音频

PC Speaker 音频由固定在 Core 0 的 FreeRTOS 任务生成，并以 128 个立体声帧为一批写入 I2S DMA。这样 PIT 2 方波不再依赖显示刷新节奏，8086 模拟主循环仍保留在 Arduino 核心。

默认 `cardputer86.img` 根目录包含 `SNDTEST.COM`。在 DOS 下运行它，会通过 PIT 通道 2 和端口 `61h` 播放一段短音阶，用于测试扬声器。

## 磁盘镜像与启动

内部 Flash 的独立 FAT 分区和 microSD 根目录均可存放普通可写 `.img` 文件，同时兼容旧 `.dsk` 文件。检测到多个镜像时，启动菜单可用 `W`/`S` 和回车选择，也可按 `1` 至 `9` 直接选择。

软盘尺寸镜像作为 `A:` 启动，大于 2.88MB 的镜像作为硬盘 `C:` 启动。默认 `cardputer86.img` 在无操作四秒后自动启动。

## SD 卡、USB 磁盘与设置

POST 阶段按住 `Alt` 才会探测并挂载 SD 卡；未插卡时不会阻塞启动。模拟器运行后按 `Fn+空格` 暂停并进入 Settings，标题栏会显示当前 RTC 时间和电池电量。运行时 Settings 不开放 USB 磁盘模式，因为模拟磁盘已经挂载；导入文件应使用专门的存储导入流程。

USB 磁盘模式可将内部 Flash 或已挂载的 SD 卡导出给电脑，用于复制 `.img` 镜像。完成后请在电脑端安全弹出，再重启 Cardputer。

512KB 模式使用 128KB SRAM 页缓存，并将不活跃的脏页写入带磨损均衡的专用 Flash 分区；默认和关闭后模拟机使用 128KB 内存。该选项保存在 NVS 中，断电后仍然有效。

Settings 还可持久化选择近似 8086 CPU 速度：4.77MHz、8MHz、10MHz、12MHz、16MHz、24MHz、33MHz 或 Unlimited。POST 自检声音默认禁用，可在 Settings 中持久化开启。固件会显式保持 ESP32-S3 主机 CPU 在标准最高 240MHz。限速器按平均每条指令四个 8086 时钟周期估算，因此不同指令组合下的实际速度会有差异。

电源设置可设为 30 秒、2 分钟、5 分钟、10 分钟或永不；默认 2 分钟。休眠使用 ESP32-S3 light sleep，并关闭 LCD 面板与背光；固件每 100ms 短暂唤醒扫描 Cardputer 键盘矩阵，按任意键恢复显示。G0 不再用于休眠或唤醒。Settings 还提供模拟 RTC 时钟设置，DOS 可通过 BIOS `INT 1Ah` 和标准 CMOS RTC 端口 `70h/71h` 读取。

## 许可与致谢

CardPuter86 使用 Mike Chambers 的 Fake86 模拟器核心，并基于 Ackerman 的 ESP32TinyFake86。各上游代码和内嵌软件继续遵循其原有许可证与署名要求。

CardPuter86 整体采用 [GNU GPL v3.0 或更高版本](LICENSE)。其中 BSD、CC0、LGPL 等第三方组件继续保留各自的许可证声明和条款。
