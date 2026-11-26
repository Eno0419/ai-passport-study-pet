# AI Passport 生字小宠 (AI Passport Study Pet)

面向小学生家庭书桌与随身学习的 **AI 语音识字/背单词小宠物固件**，基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（v2.4.2）深度适配 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 硬件平台（ESP32-C3 / 8 MB Flash / 无 PSRAM / 无 SD 卡 / 240×320 圆角竖屏 / 三键物理输入）。

> 孩子只需按下「确定」键张嘴问：“‘勇敢’的‘勇’怎么写？” 或 “‘蝴蝶’用英语怎么拼？”，设备一边温柔语音拆解，一边立刻在 240×320 护眼米黄纸色屏幕上弹出**规范楷体田字格**或**小学英语四线三格**并保持常亮供低头抄写，写完按一下「写好了」获得小星星奖励，并自动沉淀到掉电不丢的 NVS 专属生字本中。

---

## 一、核心功能特性

- **规范楷体田字格（单字模式）**：144×144 红色虚线田字格 + 76px 标准楷体 + 蓝色带声调拼音 + 部首/笔画/组词提示。
- **多字词语宫格 + 单字放大（词语模式）**：2 字并排双田字格（96×96）、3–4 字 `2×2` 田字格、5–6 字 `3×2` 田字格；**长按上/下键**可把词语里的任意单个字放大到 144×144 精看临摹。
- **小学英语四线三格（英文模式）**：红底基线 + 虚线中线标准四线三格，支持 36px 英文单词（超长自动缩放）、音标 IPA 与中文释义。
- **零 PSRAM / 零 SD 卡 离线字库（SFP2 格式）**：将 3,877 个常用楷体汉字（GB2312 一级 + 小学常用生字与偏旁部首）、带调拼音与 IPA 压缩为 `3.78 MB` 单个 Flash 分区镜像（2bpp 三模式 RLE 编码），运行时经 `esp_partition_read()` + 64 槽跳表直读解码，**零 PSRAM、零 MMU 页占用、4 套字库总静态内部 RAM < 4 KB**。
- **开机即玩（离线识字优先，联网随时增强）**：开机直接进入「生字小宠」识字卡，内置 12 张小学精选生字/词语/英语启蒙卡，无需配网或绑定即可立即体验全部田字格、单字放大、四线三格、星星激励与生字本；随时长按确定键可查看配网或 6 位绑定码，连网绑定小智后自动解锁全字库 AI 语音陪学问答。
- **三键盲操 + 3 分钟护眼留屏**：生字卡弹出后常亮供低头照抄；支持三键翻看卡片、打开 NVS 生字本（最多 32 条，含提问次数统计与星星激励，CRC32 校验防损坏），任何界面短按确定键均有即时响应，绝不卡死在配网或配对码页面。
- **AI Passport 硬件深度适配**：完整复刻 ST7789P3 厂商初始化序列、R=30px 四角圆角遮罩、GPIO0 三键 ADC 分压按键锁防误触、ES8311 全双工 `no_dac_ref` 录音校准与 CW2017 500mAh 电量计。

---

## 二、硬件说明（FoloToy AI Passport）

本固件面向 **[FoloToy AI Passport](https://github.com/FoloToy/ai-passport)** 开发并已完成真机实测验证。

### 1. 硬件规格

| 模块 | 规格说明 |
| :--- | :--- |
| **主控芯片** | ESP32-C3（单核 RISC-V 160 MHz，**8 MB 内置 Flash，无 PSRAM**） |
| **显示屏** | ST7789P3 彩色 TFT，**240 × 320 竖屏**，RGB565，SPI2 80 MHz，Mode 0，四角 **R=30px** 物理圆角 |
| **输入按键** | 顶部/侧面 **UP / DOWN / OK 三枚功能键**（GPIO0 单路 ADC 电阻梯分压，不支持双键同按）+ 独立硬件电源键 |
| **音频** | ES8311 音频编解码器（I2C0 控制 + I2S0 16 kHz 全双工），内置麦克风与扬声器 |
| **无线** | 2.4 GHz Wi-Fi 802.11 b/g/n（内置 SoftAP 网页配网） |
| **电池与电量计** | 500 mAh 锂电池 + CW2017 I2C 电量计（内置万锂 500mAh 80 字节 Profile） |
| **调试/烧录口** | USB Type-C（ESP32-C3 原生 USB-Serial/JTAG，GPIO18/19，免驱） |

### 2. 引脚映射表（`main/boards/folotoy/ai-passport/config.h`）

| GPIO | 外设功能 | 说明 |
| ---: | :--- | :--- |
| `GPIO 0` | 三键 ADC 电阻梯 (`ADC1_CH0`) | 外部 10 kΩ 上拉：`UP` 0–150 mV、`DOWN` 150–447 mV、`OK` 447–1900 mV、松开 ≥1900 mV |
| `GPIO 1` | LCD CS | ST7789P3 片选 |
| `GPIO 2` | I2S DOUT | 播放数据（MCU → ES8311） |
| `GPIO 3` | I2S WS | 帧时钟 |
| `GPIO 4` | I2S DIN | 录音数据（ES8311 → MCU） |
| `GPIO 5` | I2S BCLK | 位时钟 |
| `GPIO 6` | I2S MCLK | 主时钟（256×fs） |
| `GPIO 7` | I2C0 SCL | ES8311 (`0x18`) 与 CW2017 (`0x63`) 共用总线 |
| `GPIO 8` | LCD SCLK | SPI2 时钟（80 MHz，Mode 0） |
| `GPIO 9` | LCD MOSI | SPI2 数据（无 MISO） |
| `GPIO 10` | I2C0 SDA | 共享 I2C0 数据线 |
| `GPIO 18/19` | USB-Serial/JTAG | 原生 USB 烧录与串口日志 |
| `GPIO 20` | LCD DC | 命令/数据选择（RST 硬件接 3.3V，走软件复位） |
| `GPIO 21` | LCD 背光 PWM | 高电平有效，LEDC 5 kHz 10-bit（**注意与 UART0 默认 TX 冲突，控制台必须走 USB-Serial/JTAG**） |

---

## 三、8 MB Flash 分区与存储规划

分区表位于 [`partitions/v2/8m_ai_passport_study.csv`](partitions/v2/8m_ai_passport_study.csv)，充分利用 8 MB Flash 空间，无需外接 SD 卡：

| 分区名 | 类型 | 起始偏移 | 分区容量 | 说明 |
| :--- | :--- | ---: | ---: | :--- |
| `nvs` | `data/nvs` | `0x009000` | 24 KB | Wi-Fi 凭据 + NVS 持久化生字本（32 条）与星星计数 |
| `phy_init` | `data/phy` | `0x00F000` | 4 KB | Wi-Fi PHY 校准数据 |
| `factory` | `app/factory` | `0x010000` | **2.75 MB** (`0x2C0000`) | 主固件 `xiaozhi.bin`（实测 ~2.34 MB，禁用上游自动 OTA 覆写） |
| `assets` | `data/spiffs` | `0x2D0000` | **1.06 MB** (`0x110000`) | 小智默认表情与界面资源 `generated_assets.bin`（~902 KB） |
| `study_fonts` | `data/0x40` | `0x3E0000` | **4.125 MB** (`0x420000`) | 离线 SFP2 打包字库 `study_fonts.bin`（`kai_76` + `kai_44` + `ui_16` + `latin_36`，~3.78 MB） |

> **说明**：ESP32-C3 无 PSRAM，本固件在 `sdkconfig.defaults.esp32c3` 中关闭了本地唤醒词（改由 `OK` 键短按对话）并精简了 Wi-Fi/LWIP 缓冲，实测连网待机 + MQTT 在线状态下剩余内部 SRAM 约 **57–70 KB**、最大连续块 **40 KB+**。

---

## 四、三键交互说明

所有 UI 顶/底栏均避让 `x ∈ [28, 212]` 圆角安全区；按键底层内置 `40ms` 确认 + `120ms` 锁定防抖，避免长按断触误触发邻键。

| 当前界面 | `UP` 短按 | `DOWN` 短按 | `OK` 短按 | `OK` 长按 (500ms) | `UP` / `DOWN` 长按 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **生字 / 单词卡（开机默认）** | 上一张卡片（放大态切换上一字） | 下一张卡片（放大态切换下一字） | **「写好了 ★+1」**（离线态自动切下一张，联网态回宠物主页；放大态先退回整词） | 联网态直接**语音追问**；离线/未绑定态切出配网/绑定码页 | 词语卡**单字放大**到 144×144；单字/英文卡调音量 +10% / −10% |
| **生字本列表（双击 `OK` 进入）** | 上移选中项 | 下移选中项 | 打开选中字词大卡 | 返回生字卡 / 宠物主页 | — |
| **宠物主页 / 配网与绑定页** | 打开生字卡 | 打开生字本列表 | 联网态**开始/停止语音提问**；离线/配对码页**立刻返回生字卡** | 手动进入 Wi-Fi 配网模式（`Xiaozhi-xxxx`） | 音量 +10% / −10% |

> 任意界面双击 `OK` 均可快速打开生字本；未配网或未绑定时随时可先离线识字，绝不卡在配对码界面。

---

## 五、环境搭建与部署指南

### 1. 前置环境

- **ESP-IDF**：**v5.5.2 及以上**（实测推荐 **ESP-IDF v5.5.x**，参见 [ESP-IDF 官方安装文档](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.5/esp32c3/get-started/)）
- **Python 3.10+** 及 `Pillow`（用于生成离线楷体字库镜像 `study_fonts.bin`）
- **支持数据传输的 USB-C 数据线**（切勿使用仅充电线）

### 2. 克隆仓库并生成离线楷体字库（SFP2）

> 由于版权限制，本仓库不直接分发操作系统或商业字体二进制文件，构建前请先运行脚本在本地生成 `study_assets/study_fonts.bin`。

```bash
git clone https://github.com/Eno0419/ai-passport-study-pet.git
cd ai-passport-study-pet
```

- **macOS**（自动使用系统内置楷体/黑体/Helvetica）：
  ```bash
  pip install pillow   # 或使用 uv run --with pillow python ...
  python3 tools/gen_passport_fonts.py
  ```
- **Linux / Windows 或使用开源字体**（强烈推荐开源免费的 [霞鹜文楷 LXGW WenKai](https://github.com/lxgw/LxgwWenKai) + [Noto Sans SC](https://fonts.google.com/noto/specimen/Noto+Sans+SC)）：
  ```bash
  python3 tools/gen_passport_fonts.py \
    --kaiti-font /path/to/LXGWWenKai-Regular.ttf \
    --ui-font /path/to/NotoSansSC-Regular.ttf \
    --latin-font /path/to/DejaVuSans.ttf \
    --ipa-font /path/to/DejaVuSans.ttf
  ```

运行成功后会输出类似信息并自动完成 CRC32 与逐字解码自检：
```text
Wrote study_assets/study_fonts.bin: 3,966,369 bytes (3.78 MB), CRC32=0x59CDA5F5
  [VERIFY OK] kai_76: 3877 glyphs ...
  [VERIFY OK] kai_44: 3877 glyphs ...
  [VERIFY OK] ui_16: 4091 glyphs ...
  [VERIFY OK] latin_36: 214 glyphs ...
```

### 3. 编译 ESP32-C3 固件与 8 MB 合并镜像

```bash
# 激活 ESP-IDF 5.5 环境（路径按你的实际安装位置调整）
source ~/esp/esp-idf-v5.5/export.sh

# 设定目标芯片为 esp32c3（自动应用 sdkconfig.defaults.esp32c3）
idf.py set-target esp32c3

# 编译固件与默认 assets
idf.py build

# 可选：生成单文件 8 MB 完整镜像 build/ai-passport-study-full.bin
idf.py merge-bin -o ai-passport-study-full.bin
```

### 4. 烧录到 FoloToy AI Passport

1. 按住设备独立电源键约 **0.5 秒开机**，用支持数据传输的 USB-C 线连接电脑；
2. 确认串口设备（macOS 通常为 `/dev/cu.usbmodem*`，Linux 为 `/dev/ttyACM0`，Windows 为 `COMx`）：

- **首次完整烧录**（写入 bootloader + 分区表 + 主固件 + assets + 离线字库，会清空原 NVS）：
  ```bash
  # 方式 1：使用合并镜像一次性刷入
  python -m esptool --chip esp32c3 -p /dev/cu.usbmodem* -b 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_size 8MB --flash_freq 80m \
    0x0 build/ai-passport-study-full.bin

  # 方式 2：使用 idf.py 分区烧录（同样会自动烧录 study_fonts）
  idf.py -p /dev/cu.usbmodem* flash
  ```

- **日常开发增量迭代**（仅刷新 App，**保留 NVS Wi-Fi 凭据、生字本和 Flash 字库**，约 8 秒）：
  ```bash
  idf.py -p /dev/cu.usbmodem* app-flash
  ```

- **若仅更新了字库与 App**（同样保留 NVS）：
  ```bash
  python -m esptool --chip esp32c3 -p /dev/cu.usbmodem* -b 460800 \
    --before default_reset --after hard_reset write_flash \
    --flash_mode dio --flash_size 8MB --flash_freq 80m \
    0x10000 build/xiaozhi.bin 0x3e0000 study_assets/study_fonts.bin
  ```

### 5. 首次配网与小智后台配置

1. **连接 Wi-Fi**：首次开机无 Wi-Fi 配置时会自动开启热点 **`Xiaozhi-xxxx`**，手机连上后浏览器访问 `http://192.168.4.1` 选择家里 **2.4 GHz Wi-Fi** 保存并重启。
2. **绑定小智后台**：联网后屏幕会播报/显示 6 位验证码，前往小智控制台（或你私有部署的 `xiaozhi-esp32-server`）添加设备。
3. **配置儿童陪学 System Prompt**（在小智后台角色设置中粘贴，让大模型在孩子问生字/单词时自动调用端侧 MCP 工具）：

```markdown
你是一只住在书桌上的可爱学习小宠物，正在陪伴一个小学生写作业和认字。

【说话风格】
- 温柔、活泼、有耐心，每次语音回复尽量简短口语化（2~4 句话），多夸奖鼓励。

【屏幕工具调用规则（非常重要）】
1. 只要小朋友问“XX字怎么写”、“XX的X怎么写”或不认识某个字/词，必须立即调用 `self.study.show`（单字用 mode="hanzi"，词语用 mode="phrase"，附带声调拼音 sub 和简短部首/组词 hint），并一边口语拆解字形。
2. 只要小朋友问“XX用英语怎么说”或“XX怎么拼”，必须立即调用 `self.study.show`（mode="english"，附音标 sub 和中文意思 hint），先标准读一遍再慢速拼读字母。
3. 当小朋友说“写好了”、“记住了”、“关掉卡片”时，调用 `self.study.close` 和 `self.pet.reward_star` 夸奖她。
```

### 6. 设备注册的 MCP 工具清单

| 工具名 | 参数 | 行为 |
| :--- | :--- | :--- |
| `self.study.show` | `mode` (`hanzi`\|`phrase`\|`english`), `text`, `sub`, `hint` | 弹出大字楷体田字格 / 词语宫格 / 英文四线三格并写入 NVS 生字本 |
| `self.study.close` | 无 | 关闭学习卡并返回小宠物表情主页 |
| `self.pet.reward_star` | `reason` | 累计星星数 +1（持久化至 NVS）并展示开心通知 |

---

## 六、目录结构与主机单元测试

新增与改动的核心文件：

```text
├── partitions/v2/8m_ai_passport_study.csv         # 8MB Flash 分区表（含 4.125MB study_fonts 分区）
├── sdkconfig.defaults.esp32c3                     # ESP32-C3 低内存配置与 USB-JTAG 控制台默认项
├── tools/gen_passport_fonts.py                    # 跨平台 SFP2 离线楷体字库打包与校验工具
├── tests/host_test.cc                             # 纯 C++ 主机单元测试（圆角/按键锁/状态机/字库解码）
└── main/boards/folotoy/ai-passport/
    ├── config.h / config.json                     # AI Passport 引脚与板级构建配置
    ├── ai_passport_board.cc                       # ST7789P3/ES8311/三键ADC/MCP 集成
    ├── cw2017_battery.h / .cc                     # CW2017 电量计驱动（万锂 500mAh Profile）
    ├── passport_display_rounding.h / .cc          # R=30px 圆角刷屏遮罩算法
    ├── passport_key_decoder.h                     # GPIO0 三键分压按键锁去抖状态机
    ├── study_logic.h / .cc                        # 三键界面状态机、UTF-8 分词、RLE2/RAW4 解码与 CRC32
    ├── study_flash_font.h / .cc                   # 零 PSRAM / 零 MMU Flash 直读 LVGL 字体引擎
    ├── study_book_nvs.h / .cc                     # NVS 持久化生字本（32 条 + 星星数 + CRC32）
    └── study_card_passport.h / .cc                # 240×320 竖屏田字格/四线三格/生字本 LVGL 页面
```

无需硬件即可在电脑上运行核心逻辑与字库解码单元测试：

```bash
clang++ -std=c++17 -Wall -Wextra -Werror \
  -I main/boards/folotoy/ai-passport \
  tests/host_test.cc \
  main/boards/folotoy/ai-passport/study_logic.cc \
  main/boards/folotoy/ai-passport/passport_display_rounding.cc \
  -o /tmp/ai_passport_host_test

/tmp/ai_passport_host_test study_assets/study_fonts.bin
```

---

## 七、常见问题排查

- **插上电脑找不到 `/dev/cu.usbmodem*`**：确认已按住电源键 0.5 秒开机，且使用的是**支持数据传输的 USB-C 数据线**。
- **屏幕文字边角被裁切**：请确保所有顶/底栏 UI 保持在 `x ∈ [28, 212]` 安全区内（避让 `R=30px` 物理圆角）。
- **串口无日志或背光异常**：GPIO21 是背光 PWM 引脚，与 ESP32-C3 默认 UART0 TX 冲突，切勿关闭 `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`。
- **缺字或生僻部首显示方框**：可在 `tools/gen_passport_fonts.py` 的 `extra` 字符串中补充对应汉字/部首，重新生成 `study_assets/study_fonts.bin` 并刷入 `0x3E0000` 即可。

---

## 八、致谢与开源协议

- 语音对话框架与底层架构基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT License）。
- 硬件引脚映射、ST7789P3 初始化序列、R30 圆角遮罩、三键 ADC 按键锁及 CW2017 电池 Profile 参考自 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)。
- 本项目沿用 **MIT License** 开源，详见 [LICENSE](LICENSE)。
