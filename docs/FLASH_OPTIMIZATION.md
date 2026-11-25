# STM32 工程 Flash 优化记录

## 问题

Keil MDK 未激活时，链接器有 **32KB 代码大小限制**。原工程链接失败：

```
L6047U: The size of this image (51440 bytes) exceeds
        the maximum allowed for this version of the linker
```

超出 **18,672 字节**（57%）。

---

## 分析

用 `fromelf` 和源码统计定位空间占用：

| 项目 | 大小 | 说明 |
|---|---|---|
| **GIF 数据（20帧 × 2048字节）** | **40.0 KB** | 最大单项 |
| **ASCII 字体表（4 套）** | **13.0 KB** | 完全没用到 |
| 代码（驱动 + 业务逻辑） | 约 10 KB | 必要 |
| 库函数 | 已被链接器裁剪 | 无需处理 |

### 关键发现 1：22 个 StdPeriph 驱动全在工程里，但只用到 4 个

删掉 19 个未使用的（adc/can/cec/crc/dac/dma/tim/spi/i2c/…）后，
**镜像大小没有任何变化**。

原因：**ARM 链接器已经自动裁剪**未被引用的目标文件。
这部分优化无效，但保留了改动（工程更清晰）。

> 如果以后换 AC5 或关闭 `--remove`，精简驱动才会真正生效。

### 关键发现 2：4 套 ASCII 字体全部没人用

`lcdfont.h` 里有 4 套字体：

| 字体 | 大小 | 实际使用 |
|---|---|---|
| `ascii_1206`（6×12） | 1.1 KB | ❌ |
| `ascii_1608`（8×16） | 1.5 KB | ⚠️ 仅 LCD_ShowChar 内部 |
| `ascii_2412`（12×24） | 4.5 KB | ❌ |
| `ascii_3216`（16×32） | 5.9 KB | ❌ |

而 `LCD_ShowChar` / `LCD_ShowString` / `LCD_ShowChinese*` **在业务代码里一次都没被调用**。

主程序只用到：
- `LCD_Fill()` —— 纯色填充
- `LCD_ShowPicture_Scale()` —— 显示 GIF 帧

**这两个函数都不依赖字库。**

---

## 实施的优化

### 1. GIF 帧数：20 → 8

`Hardware/gif_data.h`：

```diff
-#define GIF_FRAMES  20
+#define GIF_FRAMES  8
```

同时**删除了源码里多余的 12 帧数据**（否则会有
`excess elements in array initializer` 警告，且白占源文件体积）。

源文件：240.8 KB → **96.5 KB**

**代价**：GIF 动画从 20 帧缩短到 8 帧。
20 帧 × 50ms = 1 秒动画，8 帧 = 0.4 秒。仍然能看出动画效果。

### 2. 移除整套字库

**`Hardware/lcdfont.h`**：删除 3 套未用字体（保留 1608）

77.6 KB → **16.1 KB**

**`Hardware/LCD.c`**：删除所有字符显示函数（共 416 行）

删掉的函数：
```
LCD_ShowChinese / 12x12 / 16x16 / 24x24 / 32x32
LCD_ShowChar
LCD_ShowString
LCD_ShowIntNum
LCD_ShowFloatNum1
mypow
```

保留的（都不依赖字体）：
```
LCD_Fill
LCD_DrawPoint / DrawLine / DrawRectangle / Draw_Circle
LCD_ShowPicture
LCD_ShowPicture_Scale
LCD_SetScale / LCD_GetScale / LCD_SetBrightness / LCD_BrightnessTick
```

**`Hardware/LCD.h`**：删除对应声明，加入说明注释。

### 3. 顺手完成的清理

- 启用 HEX 输出（`<CreateHexFile>1`），之前是关闭的
- `gif_data.h` 里的 `// Frame N` 注释改成 `/* Frame N */`（消除 C90 警告）
- 从工程移除 19 个未使用的 StdPeriph 驱动（虽然链接器已裁剪，但工程更清晰）

---

## 结果

| 指标 | 优化前 | 优化后 |
|---|---|---|
| **镜像大小** | 51,440 字节 | **26,864 字节** |
| 占 32KB 限额 | 157%（**超限，链接失败**） | **82%** |
| 剩余余量 | −18,672 字节 | **+5,904 字节** |
| 链接状态 | ❌ 失败 | **✅ 成功** |

### 细分

```
Code    = 10,152 字节   代码
RO-data = 16,704 字节   常量（GIF 8帧 = 16,384）
RW-data =      8 字节
ZI-data =  3,768 字节   RAM（16KB 中的 23%）
```

### 产物

| 文件 | 大小 |
|---|---|
| `Objects/Project.axf` | 93.4 KB（含调试信息） |
| `Objects/Project.hex` | 73.8 KB（烧录用） |
| 实际固件 | 26.2 KB |

### 固件验证

已确认以下符号和中断向量正确：

| 项目 | 状态 |
|---|---|
| `USART1_IRQHandler` 挂载 | ✓ `0x0800102D` |
| `SysTick_Handler` 挂载 | ✓ `0x08000FC1` |
| 向量表 | ✓ 正常 |
| `proto_feed_byte` | ✓ |
| `img_rx_push_pixel` | ✓ |
| `app_link_poll` | ✓ |
| `LCD_ShowPicture_Scale` | ✓ |
| `gif_frames` | ✓ |

---

## 剩余警告说明

构建后仍有 **67 个警告**，全部来自原工程，**不影响功能**：

| 类型 | 数量 | 说明 |
|---|---|---|
| `non-portable path` | 44 | 文件名大小写不一致（如 `Delay.h` vs `delay.h`） |
| `// comments are not allowed` | 23 | 原工程用 `//` 注释，AC6 按 C90 严格模式检查 |

**为什么不清理**：
这些是原工程遗留的注释和文件名问题，修改涉及几十个文件、
几百处中文注释（还有 GBK/UTF-8 编码风险），
但**对功能零影响**。保持原文件完整性更稳妥。

如果将来想清零警告：
1. `//` 注释批量改成 `/* */`
2. `#include "Delay.h"` 改成 `#include "delay.h"`（与实际文件名一致）

---

## 备份

所有改动前的文件都在 `_backup/`：

```
_backup/
├── gif_data.h.bak    （原始 20 帧版本，240.8 KB）
├── lcdfont.h.bak     （原始 4 套字体，77.6 KB）
└── LCD.c.bak         （原始含所有字符函数，18.4 KB）
```

**恢复方法**：把对应 `.bak` 覆盖回 `Hardware/` 目录，
并把 `Project.uvprojx` 里删掉的库条目加回来。

> 注意：`Project.uvprojx.bak` 也有备份（改动前的完整工程文件）。

---

## 如果还要更多空间

当前余量 5.9 KB。如果将来空间又不够：

### 进一步压缩 GIF

当前 8 帧 × 2048 字节 = 16,384 字节，是最大单项。

| 方案 | 节省 | 代价 |
|---|---|---|
| 减到 4 帧 | 8 KB | 动画更卡顿 |
| 改用更小尺寸（32×32） | 12 KB | 画面更糊 |
| **不用内置 GIF，改由手机下发** | **16 KB** | 需要连 ESP32 才有画面 |

> 最后一条很值得考虑：既然手机能下发图片，
> 内置 GIF 只是"开机没连手机时的占位动画"。
> 完全可以让它只在没有连接时显示一个简单图案（几百字节），
> 或者干脆黑屏。

### 换芯片

F103C8（64KB Flash / 20KB RAM）→ F103CB（128KB）或 F103RB（128KB），
引脚兼容，换个芯片就能随便用。

### 激活 Keil

正版授权后无 32KB 限制。

---

## 附录：Ac6 编译器大小检查原因

`excess elements in array initializer` 这个警告值得单独说明：

只把 `#define GIF_FRAMES 20` 改成 `8` 后，
数组 `gif_frames[8][2048]` 里仍然init了 20 帧数据，
后 12 帧被编译器**静默截断**。

虽然编译后的 Flash 占用是对的（只存 8 帧），
但：
1. 源文件白白多出 144 KB
2. 编译器会警告（可能掩盖其他问题）
3. 容易让人误解"数据还在"

所以必须把源码里多余的帧也删掉，保持数据与声明一致。
