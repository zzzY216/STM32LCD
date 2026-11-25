/**
 * @file    proto.c
 * @brief   帧协议解析状态机实现。
 * @details 全部是逐字节推进的状态机，没有任何阻塞等待，
 *          适合放在主循环里配合中断环形缓冲使用。
 */
#include "proto.h"
#include <string.h>

/* ==================== 解析状态 ==================== */

typedef enum {
    ST_WAIT_SYNC0 = 0,
    ST_WAIT_SYNC1,
    ST_CMD,
    ST_SEQ,
    ST_LEN_L,
    ST_LEN_H,
    ST_PAYLOAD,
    ST_CRC_L,
    ST_CRC_H
} parse_state_t;

/* 解析上下文 */
static struct {
    parse_state_t state;
    uint8_t       cmd;
    uint8_t       seq;
    uint16_t      len;
    uint16_t      idx;          /* payload 写入位置 */
    uint8_t       crc_l;        /* 收到的 CRC 低字节 */
    uint32_t      last_byte_ms; /* 用于超时判断 */
    proto_frame_t frame;        /* 当前正在拼装的帧 */
} s_parse;

static proto_cb_t   s_cb  = 0;
static proto_tx_fn  s_tx  = 0;

/* 统计，便于排查 */
static uint32_t s_stat_ok      = 0;   /* 成功解析的帧数 */
static uint32_t s_stat_crc_err = 0;   /* CRC 错误次数 */
static uint32_t s_stat_len_err = 0;   /* 长度错误次数 */
static uint32_t s_stat_resync  = 0;   /* 重新同步次数 */

/* ==================== CRC16 ==================== */

/**
 * @brief CRC-16/CCITT-FALSE：多项式 0x1021，初值 0xFFFF，不反转。
 * @note  用逐位实现，Flash 占用小；改为查表可提速但会增加 512 字节表。
 */
uint16_t proto_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t  bit;

    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0u; bit < 8u; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ==================== 内部工具 ==================== */

/** 计算"帧头 + 负载"里参与 CRC 的区间长度（CMD+SEQ+LEN+PAYLOAD） */
static uint16_t crc_span_len(const proto_frame_t *f)
{
    return (uint16_t)(4u + f->len);
}

/**
 * @brief 计算整帧的 CRC。
 * @details 把 CMD/SEQ/LEN/PAYLOAD 视作一段连续逻辑数据，
 *          但它们在结构体里并不连续，所以分段累加。
 */
static uint16_t frame_crc(const proto_frame_t *f)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t  buf[4];
    uint8_t  bit;

    /* 先算 4 字节头部：CMD, SEQ, LEN_L, LEN_H */
    buf[0] = f->cmd;
    buf[1] = f->seq;
    buf[2] = (uint8_t)(f->len & 0xFFu);
    buf[3] = (uint8_t)((f->len >> 8) & 0xFFu);

    for (i = 0u; i < 4u; i++) {
        crc ^= (uint16_t)((uint16_t)buf[i] << 8);
        for (bit = 0u; bit < 8u; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }

    /* 再算 PAYLOAD */
    for (i = 0u; i < f->len; i++) {
        crc ^= (uint16_t)((uint16_t)f->payload[i] << 8);
        for (bit = 0u; bit < 8u; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/** 状态机复位到等待帧头 */
static void reset_parser(void)
{
    s_parse.state = ST_WAIT_SYNC0;
    s_parse.cmd   = 0u;
    s_parse.seq   = 0u;
    s_parse.len   = 0u;
    s_parse.idx   = 0u;
    s_parse.crc_l = 0u;
}

/** 帧出错时上报（不改变接收缓冲，只记录） */
static void on_frame_error(uint8_t cmd, uint8_t seq, proto_err_t err)
{
    if (err == PROTO_ERR_CRC) {
        s_stat_crc_err++;
    } else if (err == PROTO_ERR_LEN) {
        s_stat_len_err++;
    }
    proto_send_reply(cmd, seq, err);
}

/* ==================== 对外接口 ==================== */

void proto_reset(void)
{
    reset_parser();
    memset(&s_parse.frame, 0, sizeof(s_parse.frame));
}

void proto_set_callback(proto_cb_t cb)
{
    s_cb = cb;
}

void proto_set_tx(proto_tx_fn fn)
{
    s_tx = fn;
}

/**
 * @brief 核心：逐字节喂入。
 */
void proto_feed_byte(uint8_t b)
{
    switch (s_parse.state) {
    case ST_WAIT_SYNC0:
        if (b == PROTO_SYNC0) {
            s_parse.state = ST_WAIT_SYNC1;
        }
        /* 其它字节直接丢弃（可能是上一帧的残渣） */
        break;

    case ST_WAIT_SYNC1:
        if (b == PROTO_SYNC1) {
            s_parse.state = ST_CMD;
        } else if (b == PROTO_SYNC0) {
            /* 连续两个 SYNC0：后一个可能是新帧头，停在 SYNC1 继续等 */
            s_parse.state = ST_WAIT_SYNC1;
            s_stat_resync++;
        } else {
            /* 帧头错位：回到起点重新找，不吞掉这个字节 */
            s_parse.state = ST_WAIT_SYNC0;
            s_stat_resync++;
        }
        break;

    case ST_CMD:
        s_parse.cmd = b;
        s_parse.state = ST_SEQ;
        break;

    case ST_SEQ:
        s_parse.seq = b;
        s_parse.state = ST_LEN_L;
        break;

    case ST_LEN_L:
        s_parse.len = b;
        s_parse.state = ST_LEN_H;
        break;

    case ST_LEN_H:
        s_parse.len = (uint16_t)(s_parse.len | ((uint16_t)b << 8));

        /* 长度超限：立即判错，绝不允许后续写入越界 */
        if (s_parse.len > PROTO_MAX_PAYLOAD) {
            on_frame_error(s_parse.cmd, s_parse.seq, PROTO_ERR_LEN);
            reset_parser();
            break;
        }

        s_parse.frame.cmd = s_parse.cmd;
        s_parse.frame.seq = s_parse.seq;
        s_parse.frame.len = s_parse.len;
        s_parse.idx = 0u;

        s_parse.state = (s_parse.len == 0u) ? ST_CRC_L : ST_PAYLOAD;
        break;

    case ST_PAYLOAD:
        s_parse.frame.payload[s_parse.idx] = b;
        s_parse.idx++;

        if (s_parse.idx >= s_parse.len) {
            s_parse.state = ST_CRC_L;
        }
        break;

    case ST_CRC_L:
        s_parse.crc_l = b;
        s_parse.state = ST_CRC_H;
        break;

    case ST_CRC_H:
    {
        uint16_t recv_crc = (uint16_t)((uint16_t)s_parse.crc_l | ((uint16_t)b << 8));
        uint16_t calc_crc = frame_crc(&s_parse.frame);

        if (recv_crc != calc_crc) {
            on_frame_error(s_parse.frame.cmd, s_parse.frame.seq, PROTO_ERR_CRC);
            reset_parser();
            break;
        }

        /* CRC 正确，交给上层处理 */
        s_stat_ok++;
        {
            proto_err_t err = PROTO_ERR_NONE;

            if (s_cb != 0) {
                err = s_cb(&s_parse.frame);
            }
            proto_send_reply(s_parse.frame.cmd, s_parse.frame.seq, err);
        }
        reset_parser();
        break;
    }

    default:
        reset_parser();
        break;
    }
}

void proto_poll_timeout(uint32_t now_ms)
{
    /* 只在"收到半帧"时才需要超时保护 */
    if (s_parse.state != ST_WAIT_SYNC0) {
        if ((uint32_t)(now_ms - s_parse.last_byte_ms) > PROTO_RX_TIMEOUT_MS) {
            on_frame_error(s_parse.cmd, s_parse.seq, PROTO_ERR_TIMEOUT);
            reset_parser();
        }
    }
    s_parse.last_byte_ms = now_ms;
}

/* ==================== 发送 ==================== */

uint16_t proto_build_frame(uint8_t *out, uint16_t out_cap,
                           uint8_t cmd, uint8_t seq,
                           const uint8_t *payload, uint16_t len)
{
    proto_frame_t tmp;
    uint16_t total;
    uint16_t i;
    uint16_t crc;

    if (out == 0 || len > PROTO_MAX_PAYLOAD) {
        return 0u;
    }

    total = (uint16_t)(PROTO_HEADER_SIZE + len + PROTO_TAIL_SIZE);
    if (out_cap < total) {
        return 0u;
    }

    /* 组装帧头 */
    out[0] = PROTO_SYNC0;
    out[1] = PROTO_SYNC1;
    out[2] = cmd;
    out[3] = seq;
    out[4] = (uint8_t)(len & 0xFFu);
    out[5] = (uint8_t)((len >> 8) & 0xFFu);

    /* 负载 */
    if (payload != 0 && len > 0u) {
        memcpy(&out[PROTO_HEADER_SIZE], payload, len);
    } else {
        for (i = 0u; i < len; i++) {
            out[PROTO_HEADER_SIZE + i] = 0u;
        }
    }

    /* CRC 覆盖 CMD..PAYLOAD，正好是 out[2] 开始、长度 4+len */
    crc = proto_crc16(&out[2], (uint16_t)(4u + len));

    out[PROTO_HEADER_SIZE + len]     = (uint8_t)(crc & 0xFFu);
    out[PROTO_HEADER_SIZE + len + 1] = (uint8_t)((crc >> 8) & 0xFFu);

    /* 避免未使用变量警告 */
    (void)tmp;
    (void)crc_span_len;

    return total;
}

void proto_send_reply(uint8_t cmd, uint8_t seq, proto_err_t err)
{
    uint8_t buf[PROTO_MAX_FRAME];
    uint8_t payload[3];
    uint16_t n;
    uint8_t reply_cmd;

    if (s_tx == 0) {
        return;
    }

    reply_cmd = (err == PROTO_ERR_NONE) ? PROTO_CMD_ACK : PROTO_CMD_NAK;
    payload[0] = (uint8_t)err;
    payload[1] = cmd;          /* 回应哪个命令 */
    payload[2] = seq;          /* 回应哪个序号 */

    n = proto_build_frame(buf, (uint16_t)sizeof(buf), reply_cmd, seq,
                          payload, 3u);
    if (n > 0u) {
        s_tx(buf, n);
    }
}

/* ==================== 统计查询（调试用） ==================== */

uint32_t proto_stat_ok(void)      { return s_stat_ok; }
uint32_t proto_stat_crc_err(void) { return s_stat_crc_err; }
uint32_t proto_stat_len_err(void) { return s_stat_len_err; }
uint32_t proto_stat_resync(void)  { return s_stat_resync; }
