# AirMidi 使用说明

面向使用者的中文说明：接线、开关机、三种模式、按键与 LED、网页和手机设置、固件更新、烧录与排障。
英文总览见 [README](../README.md)，设计与实现细节见 [docs/design/](design/)；本文不重复那些内容。

## 1. 盒子能做什么

AirMidi 是一个 MIDI 路由器：把 TRS（DIN）MIDI、BLE MIDI、USB MIDI、Wi-Fi 网络 MIDI（Network MIDI 2.0 与
RTP-MIDI）和 ESP-NOW 连在一起，任何输入都可以送到任何输出。它由网页或手机 App 设置，固件可以在线更新。

## 2. 型号与硬件

固件按板子分成四个构建目标，选错固件会启动失败或外设不对：

| 构建目标 | 适用硬件 | TRS | USB MIDI | 电池 | 按键 / LED |
|---|---|---|---|---|---|
| `c6` | 接收板 rev A + XIAO ESP32-C6 | 有（Type A） | 无，USB 只作串口控制台 | 有 | D2 / D1 |
| `s3` | 接收板 rev A + XIAO ESP32-S3 | 有（Type A） | 有 | 有 | D2 / D1 |
| `s3kit` | ESP32-S3-DevKitC-1 开发板 | 无（可自己接到 GPIO17/18） | 有 | 无 | BOOT / 板载 RGB 灯未用 |
| `s3direct` | 裸 ESP32-S3（WROOM-1-N16R8）自制板 | 有（Type A） | 有 | 无 | GPIO0 / GPIO48 |

### 2.1 接收板（`c6` / `s3`）

- 3.5 mm TRS 插座：Type A 接法，OUT 在 TIP，IN 经 6N138 光耦。用 Type B 设备时加一根 A/B 交叉线。
- USB-C：供电、充电、烧录；S3 版同时提供 USB MIDI 和串口控制台，C6 版只有串口控制台。
- 电池：Li-ion 电芯，USB-C 充电，电源开关控制整机。电量低时 LED 三闪；电芯耗尽会自动 All Notes Off 后
  深度睡眠，需拨动一次电源开关才能唤醒。
- 按键与 LED 在顶面（LED 紧挨按键）。

### 2.2 s3direct 自制板

一根原生 USB 线同时承担供电、烧录、USB MIDI 和串口控制台（板子上没有 USB-UART 桥）。接线固定为：

| 功能 | GPIO | 说明 |
|---|---|---|
| TRS TIP | 14 | MIDI 数据输出（Type A） |
| TRS RING | 15 | 固件在启动时拉高，作电流源；Type B 尚未实现 |
| TRS IN | 21 | 经 6N137 光耦输入 |
| LED | 48 | 普通 LED，高电平点亮（若要换成 WS2812 之类可寻址灯珠，需要另写驱动） |
| 按键 | 0 | BOOT 键，接 GND，内部上拉 |

没有电池，电量永远显示“on USB”；Wi-Fi 发射功率限制在 15 dBm（裸板 USB 供电时 3.3 V 容易跌落）。

## 3. 开机与默认状态

上电或拨开电源开关后，盒子进入上次保存的模式（出厂是 Play）。名字默认是 `airmidi-` 加 MAC 地址后两个字节
（例如 `airmidi-7964`），这个名字同时用于 BLE 广播、mDNS 主机名 `<名字>.local`、USB MIDI 设备名和 Setup 热点。

空闲时 LED 每秒闪一下，表示 BLE 正在广播、还没有连接——这是正常待机状态。

S3 板子插上电脑会看到两个东西：一个 USB MIDI 设备（名字就是盒子名）和一个串口（控制台，115200，8N1；
USB CDC 实际上不看波特率）。

## 4. 三种模式

| 模式 | 网页里的名字 | 行为 | 适合 |
|---|---|---|---|
| Play | `play` | Wi-Fi 关闭；BLE MIDI 开启（可同时连两台主机，也可主动去连一台设备）；USB MIDI（S3）与 TRS 正常 | 现场演奏，最省电 |
| Network | `network` | 连接已保存的 2.4 GHz Wi-Fi，网页在 `http://<名字>.local`；Network MIDI 2.0 与 RTP-MIDI 主机启动；默认关闭 BLE | 电脑通过 Wi-Fi 收发 MIDI、网页配置、在线升级 |
| Setup | `setup` | 开一个开放热点（SSID 就是盒子名，2.4 GHz 信道 1，最多 2 台设备），网页在 `http://192.168.4.1`；BLE 配置服务对任何手机开放 | 第一次连接家里的 Wi-Fi、临时改设置 |

Network 模式默认关闭 BLE：BLE 与 Wi-Fi 共用射频，开启后网络 MIDI 的延迟会明显变差（实测约 +16 ms）。
需要边联网边用 BLE 时，在设置页打开 `ble in network`。

**切换模式的方式**（效果相同）：

- 网页 status 页的 mode 选择器；
- 按键长按 2–8 秒后松开：Play 与 Network 互相切换；在 Setup 下长按回到 Play；
- 按键按住到 8 秒（不松手，到点即触发）：进入 Setup；
- 串口控制台 `P` / `N` / `S`。

切换会立即保存。若这次切换要开关 BLE，盒子会自动重启（这是 BLE 协议栈的要求）；不重启时 BLE 和网络连接也会
重建，正在响的音符会中断。怕现场出问题，切换前先双击按键发送 All Notes Off。

## 5. 按键

| 操作 | 动作 |
|---|---|
| 短按（不到 2 秒，且没有第二次） | 重新扫描 TRS 上的 MIDI-CI 设备（刷新网页 play 页的控件） |
| 双击（两次短按间隔 300 ms 内） | All Notes Off：对所有输出发送 panic，止住卡住的音 |
| 长按 2–8 秒后松开 | Play ↔ Network；Setup → Play |
| 按住满 8 秒 | 进入 Setup 模式 |
| 双击后第二下按住满 2 秒再松手 | ESP-NOW 无线 MIDI 线开 / 关：2 秒时 LED 常亮表示已就绪，松手才保存并重启 |

第二下不到 2 秒就松手，仍然是普通的双击（All Notes Off），不会切换；按住超过 8 秒也不会进 Setup。这个手势会
重启盒子（断 USB MIDI、BLE 和网络），适合上台前设置，演奏中不要用。

## 6. LED 灯语

LED 只有一盏，含义按优先级从高到低（低电量最优先，其次是 Setup，然后是连 Wi-Fi，最后才是 BLE 状态）：

| 灯语 | 状态 |
|---|---|
| 常灭 | 电芯耗尽、即将深度睡眠 |
| 每 2 秒三闪（60 ms 亮、120 ms 间隔） | 电量低（低于 3.5 V） |
| 每 1.5 秒双闪（100 ms 亮、150 ms 间隔） | Setup 模式，热点已开 |
| 2 Hz 方波闪（亮 250 ms、灭 250 ms） | 正在连接 Wi-Fi |
| 常亮，MIDI 经过时短暂变暗 | BLE 已连接 |
| 每秒短闪一次（亮 100 ms） | 待机：BLE 广播中，没有连接 |

按住第二下满 2 秒（准备切换 ESP-NOW）时 LED 也会常亮，松手后盒子重启。

注意它反映的是 **BLE 连接**，不是 Wi-Fi：Network 模式连上 Wi-Fi 后默认不开 BLE，所以仍然每秒闪一下，这是正常的。

## 7. 网页

### 7.1 怎么打开

- **Setup 模式**：手机或电脑连接名为盒子名的**开放热点**，通常会自动弹出配置页（盒子在里面跑了 captive portal：
  所有域名都解析到 192.168.4.1，未知路径返回 302）。没有自动弹出就手动打开 `http://192.168.4.1`。
- **Network 模式**：打开 `http://<名字>.local`。Windows 的 mDNS 有时不灵，打不开就去路由器里查盒子的 IP。

Play 模式下 Wi-Fi 是关闭的，所以打不开网页；要配置就先切到 Network 或 Setup。

### 7.2 页面

| 页面 | 内容 |
|---|---|
| status | 各端口流量表、电量、信号、模式选择、运行信息；Setup 模式下顶部还有一块门户卡片（盒子自己的 Wi-Fi 名、`192.168.4.1`、`<名字>.local` 和复制按钮，方便第二台手机加入后直接打开） |
| play | 网页控制面板：推子、打击垫，以及按 MIDI-CI 读到的合成器控件；手机也可以当演奏界面用（WebSocket 传输，实测约 3.9 ms 到 TRS） |
| settings | 盒子名、SysEx 超时、`ble in network`、要连接的 BLE 设备（可扫描选择）、ESP-NOW 开关与分组 |
| wifi | 已保存的网络（可删除）、扫描附近网络并填入名称、手动添加 SSID 和密码；连上以后顶部显示 `http://<名字>.local` 和拿到的 IP，可复制 |
| update | 当前固件信息、上传固件、检查 GitHub 上的新版本并安装 |

### 7.3 设置项

status 页的 mode 选择器、settings 页的六项、wifi 页和 update 页，就是全部可以在网页上改的东西：

| 位置 | 项目 | 取值 / 默认 | 作用 | 生效方式 |
|---|---|---|---|---|
| status | mode | `play` / `network` / `setup`，默认 `play` | 盒子工作模式 | 保存；要开关 BLE 时自动重启；网页上切到 setup 会先展开一段说明，点 `switch` 才真的切 |
| settings | 名称 name | 1–31 个字母、数字或 `-`（不能以 `-` 开头结尾），默认 `airmidi-` + MAC 末两字节 | BLE 广播名、`<名字>.local`、USB MIDI 名称、Setup 热点名 | 重启（网页地址随之改变） |
| settings | SysEx 超时 | 50–5000 ms，默认 500 | 一个 SysEx 消息最多占用合并输出的时间；多来源汇聚到同一输出时才需要调 | 重启 |
| settings | ble in network | 开 / 关，默认关 | Network 模式下是否仍开 BLE（开了会拖慢网络 MIDI 约 16 ms） | 下个 Network 会话；已在该模式则重启 |
| settings | ble device | `*`（任意）、广播名或 MAC 地址，默认空（不主动连） | 盒子主动去连的 BLE MIDI 键盘/控制器；右边按钮扫描 5 秒，点列表里的设备即可选定 | 立即 |
| settings | esp-now cable | 开 / 关，默认关 | 两台盒子无线直连，见第 10 节 | 重启 |
| settings | group | 1–15 个字母、数字或 `-`，默认 `airmidi` | ESP-NOW 分组；两台必须相同 | 重启 |
| wifi | 已保存网络 | 列表，可点 × 删除 | Network 模式依次尝试的网络 | 立即 |
| wifi | 切到 Network | 已保存网络下面一行 `→ switch to network mode`，Setup 模式下只要保存过网络就出现 | 让盒子重启进 Network 模式去连它 | 重启 |
| wifi | 扫描 | ↻ 按钮，列出附近的 2.4 GHz 网络（信号强的在前），点一行填入 SSID | 找网络，不改任何设置 | 立即 |
| wifi | SSID / 密码 | SSID 1–32 字节；密码 0–64 字节，开放网络留空 | 添加要加入的 2.4 GHz 网络 | 立即；Network 模式按最新添加优先尝试 |
| update | 固件 | 官方签名的 `.bin` | 上传、在线检查新版本、安装 | 安装后重启 |

设置页右下的 BLE 扫描只扫 5 秒；找不到设备就再点一次。受信任手机列表只存在于手机 App（和 `api/phones`
接口）里，网页上没有。

### 7.4 第一次连接家里的 Wi-Fi

1. 长按按键 8 秒进入 Setup（LED 双闪），手机连上盒子的开放热点；
2. 打开 `http://192.168.4.1`，进入 wifi 页：点 ↻ 扫描（几秒），从列表里点一行填好 SSID，输入密码后点 `+` 添加；
   也可以直接手动输入 SSID 和密码。网页只负责保存，切到 Network 模式才去连；手机 App 里的试连
   （`/api/wifi/try`）可以先验证密码再保存；
3. 切到 `network` 模式：点 wifi 页已保存网络下面的 `switch to network mode`，页面会先展开一段说明，再点 `switch`
   才真的重启；也可以回 status 页用 mode 选择器；
4. 盒子重启后加入网络，网页地址变成 `http://<名字>.local`（wifi 页顶部会显示这个名字和它拿到的 IP，可复制）。

只支持 2.4 GHz 和开放/WPA2 类网络，企业认证（802.1X）不支持。Setup 模式下如果同时试连家里的网络，热点会跟着
切换信道，正在用热点的浏览器可能短暂断开——属正常现象，重新连上即可。

### 7.5 手机与网络 MIDI

手机用**网页**（status/play 页）即可，不需要装 App。手机上没有系统级的“网络 MIDI”面板，iOS 尤其如此，所以
Network MIDI 2.0 和 RTP-MIDI 只在 Mac（原生）和 Windows（rtpMIDI）上有意义；盒子只应答 AppleMIDI 邀请，不会
主动去连手机。

### 7.6 固件更新

三种方式：

1. **网页 update 页上传**：先把 release 里对应板子的 `airmidi-<板>.bin` 下载到本地，再上传；盒子把它写入另一个
   OTA 分区；
2. **在线检查**：盒子需要能上外网，检查 GitHub release 后一键安装；
3. **BLE 更新**：盒子不在 Wi-Fi 时，用手机 App 发固件。

固件必须是官方密钥签名的镜像，否则会被拒绝。安装后盒子重启，新固件先以“待验证”状态运行 10 秒：设置、BLE、
网页、总线都正常才确认；失败或崩溃会自动回到旧固件。

## 8. 让盒子连接 BLE 设备（central）

在设置页的 BLE 设备里填 `*`（任意 BLE MIDI 设备）或具体名字/地址，盒子会自己扫描并连上。适合没有 USB 口、
但支持 BLE MIDI 的键盘或控制器。连上后数据进入同一个路由器，可以转发到 TRS、USB、Wi-Fi 或 ESP-NOW。

## 9. 手机 App 与桌面 App

仓库里的 Flutter 工程（[app/](../app)）生成 Android 和 macOS 两个 App：

- **Android**：通过 BLE 配置盒子（模式、Wi-Fi、受信任手机），也可以把手机当 MPE 演奏界面使用。首次配对需要在
  Setup 模式（或上电/手动复位后的 2 分钟内）打开配对窗口；盒子最多记住 4 台受信任手机，已受信任的手机可以
  通过 API `POST /api/phones/window` 再开一次窗口。
- **macOS**：电脑端口路由，以及一个面向硬件合成器的 MIDI 工作站（DAW），音轨能按 MIDI-CI 读到的控件名和
  当前值工作。

详见 [docs/app.md](app.md)、[docs/desktop.md](desktop.md) 和 [docs/daw.md](daw.md)。

## 10. ESP-NOW：两台盒子当无线 MIDI 线

两台盒子打开设置页的 ESP-NOW 开关、使用同一个分组，即可不经路由器互发 MIDI。盒子会自己找到对方的 Wi-Fi
信道；距离和穿墙能力按 Wi-Fi 计。分组名只能用字母、数字和 `-`。

不用网页也能开关，适合盲操：双击按键，第二下按住不放，到 2 秒 LED 常亮后松手，盒子保存开关并重启。第二下提前
松手就只是普通的 All Notes Off，不会切换。

## 11. USB 烧录（首次安装或救砖）

OTA 更新不需要这些步骤；只有第一次烧录、换板子或盒子起不来时才用。每个 release 提供：

- `airmidi-<板>.bin`：OTA 上传用的镜像；
- `airmidi-<板>-usb-flash.zip`：USB 首次烧录用的整包，含 bootloader、分区表、`ota_data_initial.bin`、
  `airmidi.bin` 和 `flash_args`。

### 11.1 接收板与 s3kit

用板子的 USB 口连电脑，进入下载模式（按住 BOOT 再复位，或使用 esptool 的自动复位），解压 zip 后在该目录执行：

```sh
esptool.py --chip esp32c6 -p <端口> write_flash @flash_args   # 接收板 C6
esptool.py --chip esp32s3 -p <端口> write_flash @flash_args   # 接收板 S3 / s3kit / s3direct
```

`s3kit` 请用开发板上标着 USB 的原生口：只有它提供 USB MIDI 和控制台。

### 11.2 s3direct

1. 用控制台命令 `B` 让盒子重启进 ROM 下载模式：应用串口会消失，出现一个新的串口（USB 号 `303A:1001`）。
   也可以用 esptool 的默认复位（`--before default-reset`）进入；
2. 在解压后的目录里执行上面对应的 `write_flash @flash_args`；
3. 烧完复位，应用串口重新出现（USB 号 `303A:4009`），COM 号可能又变了一个。

几个已知的坑：

- **在这块 Windows 机器上用 pyserial 手工拉 DTR/RTS 没有成功触发下载模式**，建议用 `B` 或 esptool 自带复位；
- `B` 依赖应用正在运行而且控制台可用；如果应用没起来，按住 BOOT 键再上电/复位；
- **CI 构建的 artifact 里没有 `ota_data_initial.bin`**（release 的 zip 里有）。用 CI artifact 烧录时，先擦掉
  otadata 再烧其余三个文件，引导器就会回到 `ota_0`：

  ```sh
  esptool.py --chip esp32s3 -p <端口> erase-region 0xf000 0x2000
  esptool.py --chip esp32s3 -p <端口> write-flash 0x0 bootloader/bootloader.bin \
      0x8000 partition_table/partition-table.bin 0x20000 airmidi.bin
  ```

- 不要给 C6 烧 S3 的镜像（反之亦然）。

## 12. 串口控制台（维护用）

S3 板子的控制台与 USB MIDI 共用同一根 USB 线；C6 板子的控制台是 USB Serial/JTAG。打开串口后直接按键：

| 键 | 作用 |
|---|---|
| `s` | 打印各端口计数、BLE 状态、电量、模式、Wi-Fi 状态、堆内存 |
| `i` | 当前运行的分区、OTA 状态、最近一次崩溃 |
| `P` / `N` / `S` | 切换到 Play / Network / Setup |
| `d` / `D` | 重新扫描 TRS 设备（`D` 强制重新读全部 MIDI-CI 信息） |
| `E` | 打开/关闭 ESP-NOW（会重启） |
| `B` | 重启进 ROM 下载模式（仅 S3，用于烧录） |
| `0`–`2` | 电源/频率档位，用于测量功耗（`2` 之后需要手动复位） |
| `m` | 测试钩子：向总线注入一串音符，供硬件在环测试用 |

## 13. 常见问题

**LED 每秒闪一下，是不是没连上？**
不是故障：BLE 广播中的待机灯语。手机或电脑通过 BLE 连上后会变常亮；Network 模式默认不开 BLE，所以联网后仍是
每秒一闪。

**Play 模式打不开网页？**
Play 模式 Wi-Fi 关闭。切到 Network 或 Setup。

**连上 Setup 热点没有自动弹出配置页？**
手动打开 `http://192.168.4.1`。iOS 对无法验证的门户有时不自动弹。

**家里 Wi-Fi 连不上？**
确认是 2.4 GHz、密码正确、不是企业认证网络；5 GHz 的 SSID 盒子收不到。改完 Wi-Fi 后模式要切到 Network。

**改了名字以后网页打不开了？**
地址跟着名字变：`http://<新名字>.local`；Setup 热点名也变了。

**s3direct 电量显示“on USB”？**
这块没有电池，属正常。

**烧录后找不到串口？**
烧录会让 USB 重新枚举，COM 号会变；重新插拔或看设备管理器。应用端口 `303A:4009`，ROM 下载端口 `303A:1001`。

**升级失败或盒子反复重启？**
新固件自检不通过会自动回滚到旧版本。若两个分区都坏了，用第 11 节的 USB 烧录恢复。

## 14. 相关文档

- [README](../README.md)：项目总览、构建、测试
- [docs/design/board-and-settings.md](design/board-and-settings.md)：板级引脚表与设置存储
- [docs/design/webui.md](design/webui.md)：网页与 Setup 门户
- [docs/design/ota.md](design/ota.md)：分区、签名、自检与回滚
- [docs/design/transports.md](design/transports.md)：TRS/BLE/按键与 LED 的实现
- [docs/testing.md](design/testing.md)：硬件在环测试与工具
