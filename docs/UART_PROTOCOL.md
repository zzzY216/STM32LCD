# ESP32 ↔ STM32 串口协议规范

手机 App → ESP32 → STM32 → ST7789 屏幕的完整通讯协议。

本文档同时给三方使用：**手机 App**、**ESP32 固件**、**STM32 固件**。

---

## 1. 物理层

| 项目 | 参数 |
|---|---|
| 接口 | UART |
| STM32 引脚 | **PA9 = TX，PA10 = RX** |
| 波特率 | **921600**（默认，可改） |
| 数据位 | 8 |
| 校验 | 无 |
| 停止位 | 1 |
| 流控 | 无 |

**接线**：

```
ESP32                STM32
GPIO_TX  ────────►   PA10 (RX)
GPIO_RX  ◄────────   PA9  (TX)
GND      ─────────   GND          ← 必须共地
```

> ⚠️ 两块板子必须共地，否则电平无参考，通讯不稳定。
> 若 921600 下丢包严重，把 `app_link_init(921600)` 改成 `460800` 或 `115200`。

---

## 2. 帧格式

**所有字段小端序。**

```
+--------+--------+--------+--------+--------+--------+---------+--------+
| SYNC0  | SYNC1  |  CMD   |  SEQ   |  LEN_L |  LEN_H | PAYLOAD | CRC_L  | CRC_H |
| 0xA5   | 0x5A   | 1 byte | 1 byte | 2 bytes        | N bytes | 2 bytes        |
+--------+--------+--------+--------+--------+--------+---------+--------+
```

| 字段 | 长度 | 说明 |
|---|---|---|
| SYNC0/SYNC1 | 2 | 固定 `0xA5 0x5A`，用于帧对齐 |
| CMD | 1 | 命令码，见第 3 节 |
| SEQ | 1 | 帧序号，0~255 循环，用于丢包检测 |
| LEN | 2 | PAYLOAD 字节数，0~1024 |
| PAYLOAD | N | 命令参数或数据 |
| CRC16 | 2 | 见第 4 节 |

**整帧长度** = 6 + LEN + 2

**LEN 上限 1024**。超过会被判为错误并回 NAK。

---

## 3. 命令表

### 3.1 连接维护

| CMD | 名称 | 方向 | PAYLOAD | 说明 |
|---|---|---|---|---|
| `0x01` | PING | App→STM32 | 无 | 心跳，STM32 回 ACK |
| `0x02` | PONG | — | 无 | 保留 |

### 3.2 图像传输

| CMD | 名称 | PAYLOAD | 说明 |
|---|---|---|---|
| `0x10` | IMAGE_BEGIN | 9 字节，见下 | 开始传图 |
| `0x11` | IMAGE_DATA | 像素数据 | 紧跟 BEGIN 连续发送 |
| `0x12` | IMAGE_END | 无 | 结束传输 |

**IMAGE_BEGIN 参数（9 字节，全部小端）**：

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 2 | width | 图像宽（像素） |
| 2 | 2 | height | 图像高（像素） |
| 4 | 2 | x | 屏幕左上角 X |
| 6 | 2 | y | 屏幕左上角 Y |
| 8 | 1 | format | 0 = RGB565（目前仅支持） |

**约束**：
- `width × scale ≤ 240`，`height × scale ≤ 240`
- `x + width × scale ≤ 240`，`y + height × scale ≤ 240`
- 单张图最大 240×240

**IMAGE_DATA**：
- 每字节直接对应像素的高/低字节，**高字节在前**
- 一个像素 = 2 字节
- 数据总字节数必须等于 `width × height × 2`
- 建议每片 512 字节，太大容易触发接收缓冲溢出

### 3.3 播放控制

| CMD | 名称 | PAYLOAD | 说明 |
|---|---|---|---|
| `0x20` | PLAY | 无 | 开始播放 GIF |
| `0x21` | PAUSE | 无 | 暂停（停在当前帧） |
| `0x22` | STOP | 无 | 停止并清屏为黑色 |

### 3.4 显示参数

| CMD | 名称 | PAYLOAD | 说明 |
|---|---|---|---|
| `0x30` | SET_SCALE | 1 字节：1~4 | 缩放倍数，超出范围回 NAK |
| `0x31` | SET_POS | 4 字节：x(2) y(2) | 图片显示位置 |
| `0x32` | SET_BRIGHT | 1 字节：0~100 | 背光亮度百分比 |
| `0x33` | CLEAR | 2 字节：颜色 | 清屏为指定 RGB565 颜色；无参数则黑色 |

### 3.5 应答

| CMD | 名称 | PAYLOAD | 说明 |
|---|---|---|---|
| `0x7E` | ACK | 3 字节 | 成功 |
| `0x7F` | NAK | 3 字节 | 失败 |

**应答 PAYLOAD 格式**：

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | error_code | 0 = 成功；其他见第 5 节 |
| 1 | 1 | orig_cmd | 回应哪个命令 |
| 2 | 1 | orig_seq | 回应哪个序号 |

---

## 4. CRC16

**算法**：CRC-16/CCITT-FALSE

| 参数 | 值 |
|---|---|
| 多项式 | `0x1021` |
| 初值 | `0xFFFF` |
| 输入反转 | 否 |
| 输出反转 | 否 |
| 结果异或 | `0x0000` |

**计算范围**：`CMD + SEQ + LEN_L + LEN_H + PAYLOAD`

**挪 C 实现**（STM32 端同款）：

```c
uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}
```

**验证值**：`crc16("123456789", 9)` = **`0x29B1`**。你的实现必须得到这个值。

**Python 实现**（手机端/ESP32 调试用）：

```python
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc

def build_frame(cmd: int, seq: int, payload: bytes = b'') -> bytes:
    body = bytes([cmd, seq]) + len(payload).to_bytes(2, 'little') + payload
    crc = crc16(body)
    return b'\xA5\x5A' + body + crc.to_bytes(2, 'little')
```

---

## 5. 错误码

| 值 | 名称 | 含义 | 发送方应怎么做 |
|---|---|---|---|
| `0x00` | NONE | 成功 | — |
| `0x01` | CRC | CRC 校验失败 | 重发该帧 |
| `0x02` | LEN | 长度超限 / 字节数不符 | 检查参数或重发 |
| `0x03` | CMD | 未知命令 | 检查命令码 |
| `0x04` | PARAM | 参数非法（越界、格式不支持） | 修正参数 |
| `0x05` | STATE | 当前状态不允许（如未 BEGIN 就发 DATA） | 补发 BEGIN |
| `0x06` | TIMEOUT | 传输超时（500ms 无新字节） | 重新 BEGIN 从头传 |
| `0x07` | OVERFLOW | 像素数超出声明尺寸 | 检查宽高与实际数据是否一致 |

---

## 6. 完整交互示例

### 6.1 传一张 240×240 全屏图

```
App → STM32:  IMAGE_BEGIN(seq=1) {w=240, h=240, x=0, y=0, fmt=0}
STM32 → App:  ACK(seq=1)

App → STM32:  IMAGE_DATA(seq=2)  [512 字节像素数据]
App → STM32:  IMAGE_DATA(seq=3)  [512 字节]
...（共 240×240×2 / 512 ≈ 113 帧）
App → STM32:  IMAGE_DATA(seq=114) [最后一帧]

STM32 → App:  ACK(seq=2) ...（每帧都回）

App → STM32:  IMAGE_END(seq=115)
STM32 → App:  ACK(seq=115)
```

> 实际实现中，为避免 STM32 的串口发送抢占带宽，
> **建议 App 只对 IMAGE_BEGIN 和 IMAGE_END 检查 ACK**，
> 中间的 IMAGE_DATA 用"发完不等应答"的流水线方式，
> 最后通过 END 的 ACK 判断整张图是否成功。

### 6.2 传一张 120×120 的图并放大 2 倍

```
IMAGE_BEGIN {w=120, h=120, x=0, y=0, fmt=0}
IMAGE_DATA × 29 帧（120*120*2 / 512 ≈ 57 帧，按 512 字节算）
IMAGE_END
SET_SCALE {2}
然后重新传图 → 显示为 240×240
```

> 注意：**缩放倍数在 IMAGE_BEGIN 时生效**。
> 想改变缩放，应先发 SET_SCALE，再发 IMAGE_BEGIN。

### 6.3 播放控制

```
App → STM32:  PLAY(seq=10)
STM32 → App:  ACK(seq=10)          → 屏幕开始播放内置 GIF

App → STM32:  PAUSE(seq=11)
STM32 → App:  ACK(seq=11)          → 停在当前帧

App → STM32:  STOP(seq=12)
STM32 → App:  ACK(seq=12)          → 清屏
```

### 6.4 调亮度

```
App → STM32:  SET_BRIGHT(seq=20) {30}
STM32 → App:  ACK(seq=20)          → 背光降到 30%
```

---

## 7. ESP32 端参考实现

ESP32 只做**透明转发**，不做图像处理。手机 App 通过 WiFi/BLE 发来已经是协议帧的数据，ESP32 原样从 UART 透传即可。

如果手机端发的是自定义格式（如 HTTP 上传图片），ESP32 需要负责转成上述协议帧：

```c
// ESP32 侧：把 RGB565 数据打包成协议帧发给 STM32
#include <Arduino.h>

#define UART_TX 17
#define UART_RX 16

void send_frame(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t buf[6 + 1024 + 2];
    buf[0] = 0xA5;
    buf[1] = 0x5A;
    buf[2] = cmd;
    buf[3] = seq;
    buf[4] = len & 0xFF;
    buf[5] = (len >> 8) & 0xFF;
    if (len) memcpy(&buf[6], payload, len);

    uint16_t crc = 0xFFFF;
    for (uint16_t i = 2; i < 6 + len; i++) {
        crc ^= (uint16_t)(buf[i] << 8);
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    buf[6 + len]     = crc & 0xFF;
    buf[6 + len + 1] = (crc >> 8) & 0xFF;

    Serial2.write(buf, 6 + len + 2);
}

void send_image(const uint16_t *pixels, uint16_t w, uint16_t h)
{
    uint8_t seq = 1;
    uint8_t begin[9];

    begin[0] = w & 0xFF;  begin[1] = w >> 8;
    begin[2] = h & 0xFF;  begin[3] = h >> 8;
    begin[4] = 0; begin[5] = 0;   // x = 0
    begin[6] = 0; begin[7] = 0;   // y = 0
    begin[8] = 0;                 // RGB565

    send_frame(0x10, seq++, begin, 9);

    // 每次 512 字节
    uint32_t total = (uint32_t)w * h * 2;
    uint8_t chunk[512];
    uint32_t sent = 0;
    while (sent < total) {
        uint16_t n = (total - sent > 512) ? 512 : (total - sent);
        memcpy(chunk, ((uint8_t*)pixels) + sent, n);
        send_frame(0x11, seq++, chunk, n);
        sent += n;
        delay(2);   // 给 STM32 一点处理时间
    }

    send_frame(0x12, seq++, NULL, 0);
}
```

> `delay(2)` 很重要：STM32 是软件 SPI 写屏，速度有限，
> 连续高速灌数据会导致接收环形缓冲溢出。每片之间留 2ms 余量。

---

## 8. 手机 App 端要点

1. **图片必须转成 RGB565**，字节序为**小端**（低字节在前），
   但协议里是"高字节先发"——即 UART 上先看到 `color >> 8`。
2. **图片尺寸建议 ≤ 240×240**。若原图更大，先在 App 里缩放到 240×240 以内。
3. **每片数据建议 512 字节**，对应 256 个像素（一个像素 2 字节）。
4. **序号 SEQ 递增**，可用于检测丢帧。
5. **超时重传**：发完 IMAGE_END 后若 500ms 内没收到 ACK，重发整张图。
6. **不要依赖每片 DATA 的 ACK**——STM32 回 ACK 会占用串口带宽，拖慢传图。

---

## 9. 常见问题

**Q：屏幕完全没反应**
- 检查共地
- 检查波特率是否一致
- 用串口助手发 `A5 5A 01 00 00 00 <CRC>`，STM32 应回 ACK

**Q：图片显示错位/花屏**
- 检查宽高参数是否正确
- 检查数据字节数是否等于 `w*h*2`
- 检查字节序（应该高字节在前）

**Q：传一半就停了**
- 环形缓冲溢出。减小每片大小（512 → 256），或在 ESP32 端增加片间延时
- 检查主循环是否有阻塞操作

**Q：颜色不对（红蓝互换）**
- 这是 RGB565 的字节序问题。改成低字节在前试试
- 或在 STM32 端 `img_rx_push_pixel` 里交换 `hi`/`lo`

**Q：缩放后画面偏移**
- 检查 `x + w*scale ≤ 240`、`y + h*scale ≤ 240`
- 缩放是**左上角对齐**，不会居中

---

## 10. 协议版本

| 版本 | 日期 | 变更 |
|---|---|---|
| 1.0 | 2026-11-25 | 初版：图像传输、播放控制、缩放、亮度 |
