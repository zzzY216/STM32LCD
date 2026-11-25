/**
 * @file    proto.h
 * @brief   ESP32 <-> STM32 串口帧协议（解析层）。
 * @details 逐字节喂入的流式解析器，不阻塞、不用大缓冲。
 *
 *  帧格式（小端）：
 *
 *    +-------+-------+-------+-------+--------+--------+----------+-------+
 *    | SYNC0 | SYNC1 |  CMD  |  SEQ  | LEN_L  | LEN_H  | PAYLOAD  | CRC16 |
 *    | 0xA5  | 0x5A  | 1字节 | 1字节 |        |        | N 字节   | 2字节 |
 *    +-------+-------+-------+-------+--------+--------+----------+-------+
 *
 *  CRC16 覆盖范围：CMD + SEQ + LEN_L + LEN_H + PAYLOAD
 *  算法：CRC-16/CCITT-FALSE（多项式 0x1021，初值 0xFFFF，不反转）
 *
 *  设计要点：
 *   - 帧头错位时能自动重新同步，不会吞掉下一帧的起始字节
 *   - LEN 超过上限立即判错，防止缓冲区溢出
 *   - CRC 错误丢弃整帧并上报，由上层决定是否请求重发
 *   - 任何状态下超时都会复位状态机，不会永久卡死
 */
#ifndef PROTO_H
#define PROTO_H

#include "sys.h"

/* ==================== 协议参数 ==================== */

#define PROTO_SYNC0             0xA5u
#define PROTO_SYNC1             0x5Au

/** 单帧 PAYLOAD 最大长度。通讯用缓冲只有 256 字节，这里限 1024 是给上层留余量，
 *  实际一帧图像数据建议 512 字节一片。 */
#define PROTO_MAX_PAYLOAD       1024u

/** 帧头开销：SYNC0 + SYNC1 + CMD + SEQ + LEN(2) */
#define PROTO_HEADER_SIZE       6u

/** 帧尾：CRC16 两字节 */
#define PROTO_TAIL_SIZE         2u

/** 整帧最大长度 */
#define PROTO_MAX_FRAME         (PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD + PROTO_TAIL_SIZE)

/** 收到半帧后多久没新字节就复位状态机（毫秒） */
#define PROTO_RX_TIMEOUT_MS     500u

/* ==================== 命令码 ==================== */

typedef enum {
    /* --- 连接与状态 --- */
    PROTO_CMD_PING          = 0x01u,   /**< 心跳，无负载 */
    PROTO_CMD_PONG          = 0x02u,   /**< 心跳应答 */

    /* --- 图像传输 --- */
    PROTO_CMD_IMAGE_BEGIN   = 0x10u,   /**< 开始传图 */
    PROTO_CMD_IMAGE_DATA    = 0x11u,   /**< 图像数据片 */
    PROTO_CMD_IMAGE_END     = 0x12u,   /**< 图像传输结束 */

    /* --- 播放控制 --- */
    PROTO_CMD_PLAY          = 0x20u,   /**< 播放 */
    PROTO_CMD_PAUSE         = 0x21u,   /**< 暂停 */
    PROTO_CMD_STOP          = 0x22u,   /**< 停止并清屏 */

    /* --- 显示参数 --- */
    PROTO_CMD_SET_SCALE     = 0x30u,   /**< 缩放倍数 1~4 */
    PROTO_CMD_SET_POS       = 0x31u,   /**< 显示位置 */
    PROTO_CMD_SET_BRIGHT    = 0x32u,   /**< 背光 0~100 */
    PROTO_CMD_CLEAR         = 0x33u,   /**< 清屏为指定颜色 */

    /* --- 应答 --- */
    PROTO_CMD_ACK           = 0x7Eu,   /**< 肯定应答 */
    PROTO_CMD_NAK           = 0x7Fu    /**< 否定应答 */
} proto_cmd_t;

/* ==================== 错误码 ==================== */

typedef enum {
    PROTO_ERR_NONE          = 0x00u,   /**< 无错误 */
    PROTO_ERR_CRC           = 0x01u,   /**< CRC 校验失败 */
    PROTO_ERR_LEN           = 0x02u,   /**< 长度超限或不合法 */
    PROTO_ERR_CMD           = 0x03u,   /**< 未知命令 */
    PROTO_ERR_PARAM         = 0x04u,   /**< 参数非法 */
    PROTO_ERR_STATE         = 0x05u,   /**< 当前状态不允许该操作 */
    PROTO_ERR_TIMEOUT       = 0x06u,   /**< 传输超时 */
    PROTO_ERR_OVERFLOW      = 0x07u    /**< 像素数超出声明尺寸 */
} proto_err_t;

/* ==================== 帧结构 ==================== */

/** @brief 从字节流里解析出来的一帧 */
typedef struct {
    uint8_t  cmd;                             /**< 命令码 */
    uint8_t  seq;                             /**< 帧序号 */
    uint16_t len;                             /**< PAYLOAD 实际长度 */
    uint8_t  payload[PROTO_MAX_PAYLOAD];      /**< 负载数据 */
} proto_frame_t;

/** @brief 帧回调。返回值填到 ACK/NAK 的错误码里（PROTO_ERR_NONE 表示成功） */
typedef proto_err_t (*proto_cb_t)(const proto_frame_t *frame);

/* ==================== 接口 ==================== */

/**
 * @brief 复位解析状态机。
 * @note  上电或检测到通讯异常时调用。
 */
void proto_reset(void);

/**
 * @brief 注册帧处理回调。
 * @param cb 回调函数；传 NULL 表示不处理（所有帧都会被静默接受）
 */
void proto_set_callback(proto_cb_t cb);

/**
 * @brief 喂入一个字节。
 * @details 这是协议层的核心入口。中断里只负责把字节存进环形缓冲，
 *          真正的解析在主循环里逐字节调用本函数完成。
 * @param b 收到的字节
 */
void proto_feed_byte(uint8_t b);

/**
 * @brief 在主循环里周期性调用，处理接收超时。
 * @param now_ms 当前毫秒时间
 */
void proto_poll_timeout(uint32_t now_ms);

/**
 * @brief 发送一个应答帧。
 * @param cmd 要回应答的命令码
 * @param seq 对应请求的序号
 * @param err 错误码（PROTO_ERR_NONE 表示 ACK，否则 NAK）
 * @note  实际发送由上层注入的发送函数完成。
 */
void proto_send_reply(uint8_t cmd, uint8_t seq, proto_err_t err);

/**
 * @brief 注册发送函数（由 uart_link 提供）。
 * @param fn 函数指针，签名同 uart_link_send
 */
typedef void (*proto_tx_fn)(const uint8_t *data, uint16_t len);
void proto_set_tx(proto_tx_fn fn);

/**
 * @brief 构造一帧到缓冲区（主要用于发送应答和 PC 端测试）。
 * @param out    输出缓冲
 * @param out_cap 缓冲容量
 * @param cmd    命令码
 * @param seq    序号
 * @param payload 负载
 * @param len    负载长度
 * @retval 整帧字节数；缓冲不足返回 0
 */
uint16_t proto_build_frame(uint8_t *out, uint16_t out_cap,
                           uint8_t cmd, uint8_t seq,
                           const uint8_t *payload, uint16_t len);

/* ==================== CRC16 ==================== */

/**
 * @brief 计算 CRC-16/CCITT-FALSE。
 * @param data 数据
 * @param len  长度
 * @retval 16 位校验值
 */
uint16_t proto_crc16(const uint8_t *data, uint16_t len);

#endif /* PROTO_H */
