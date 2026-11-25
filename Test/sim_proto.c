/**
 * @file    sim_proto.c
 * @brief   协议层 PC 端验证（不接硬件）。
 * @details 用 TinyCC 编译运行，验证：
 *            1. CRC16 计算正确
 *            2. 合法帧能正确解析
 *            3. CRC 错误能被拒绝
 *            4. 长度超限能被拒绝
 *            5. 帧头错位能重新同步
 *            6. 建帧/解帧往返一致
 *            7. 图像数据流能正确拼成像素
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "proto.h"

/* ==================== 测试框架 ==================== */

static int g_run = 0;
static int g_fail = 0;

#define CHECK(cond, msg, ...)                                      \
    do {                                                           \
        g_run++;                                                   \
        if (!(cond)) {                                             \
            g_fail++;                                              \
            printf("  [FAIL] " msg "\n", ##__VA_ARGS__);           \
        } else {                                                   \
            printf("  [ OK ] " msg "\n", ##__VA_ARGS__);           \
        }                                                          \
    } while (0)

/* ==================== 回调捕获 ==================== */

static struct {
    int      count;
    uint8_t  last_cmd;
    uint8_t  last_seq;
    uint16_t last_len;
    uint8_t  payload[PROTO_MAX_PAYLOAD];
} g_rx;

static proto_err_t test_callback(const proto_frame_t *f)
{
    g_rx.count++;
    g_rx.last_cmd = f->cmd;
    g_rx.last_seq = f->seq;
    g_rx.last_len = f->len;
    if (f->len > 0 && f->len <= PROTO_MAX_PAYLOAD) {
        memcpy(g_rx.payload, f->payload, f->len);
    }
    return PROTO_ERR_NONE;
}

/* 发送捕获（用于检查 ACK/NAK） */
static uint8_t  g_tx_buf[PROTO_MAX_FRAME];
static uint16_t g_tx_len = 0;

static void test_tx(const uint8_t *d, uint16_t n)
{
    if (n <= sizeof(g_tx_buf)) {
        memcpy(g_tx_buf, d, n);
        g_tx_len = n;
    }
}

/* ==================== 辅助 ==================== */

/** 把一帧字节流喂给解析器 */
static void feed_frame(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        proto_feed_byte(buf[i]);
    }
}

/* ==================== 测试用例 ==================== */

static void test_crc_known_value(void)
{
    /* "123456789" 的 CRC-16/CCITT-FALSE 标准值是 0x29B1 */
    const uint8_t data[] = {'1','2','3','4','5','6','7','8','9'};
    uint16_t crc = proto_crc16(data, 9);

    printf("\n[CRC16]\n");
    CHECK(crc == 0x29B1u, "CRC16(\"123456789\") = 0x%04X (期望 0x29B1)", crc);
}

static void test_build_and_parse(void)
{
    uint8_t  frame[128];
    uint16_t n;
    uint8_t  payload[3] = {0x11, 0x22, 0x33};

    printf("\n[建帧 + 解析]\n");

    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_PING, 0x07,
                          payload, 3);
    CHECK(n == (uint16_t)(PROTO_HEADER_SIZE + 3 + PROTO_TAIL_SIZE),
          "帧长度 = %u（期望 %u）", n,
          (unsigned)(PROTO_HEADER_SIZE + 3 + PROTO_TAIL_SIZE));

    CHECK(frame[0] == PROTO_SYNC0 && frame[1] == PROTO_SYNC1,
          "帧头正确 0x%02X 0x%02X", frame[0], frame[1]);

    g_rx.count = 0;
    feed_frame(frame, n);
    CHECK(g_rx.count == 1, "解析出 1 帧（实际 %d）", g_rx.count);
    CHECK(g_rx.last_cmd == PROTO_CMD_PING, "命令码 = 0x%02X", g_rx.last_cmd);
    CHECK(g_rx.last_seq == 0x07, "序号 = %u", g_rx.last_seq);
    CHECK(g_rx.last_len == 3, "负载长度 = %u", g_rx.last_len);
    CHECK(memcmp(g_rx.payload, payload, 3) == 0, "负载内容一致");
}

static void test_crc_reject(void)
{
    uint8_t  frame[64];
    uint16_t n;
    uint8_t  p[2] = {0xAA, 0xBB};

    printf("\n[CRC 错误拒绝]\n");

    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_PLAY, 0x01, p, 2);

    /* 篡改一个负载字节，CRC 必然不匹配 */
    frame[PROTO_HEADER_SIZE] ^= 0xFF;

    g_rx.count = 0;
    g_tx_len = 0;
    feed_frame(frame, n);

    CHECK(g_rx.count == 0, "坏帧未被交付给上层（实际 %d 次）", g_rx.count);
    CHECK(g_tx_len > 0, "发出了 NAK 应答（长度 %u）", g_tx_len);

    /* NAK 的命令码应该是 PROTO_CMD_NAK */
    if (g_tx_len >= 3) {
        CHECK(g_tx_buf[2] == PROTO_CMD_NAK, "应答类型 = NAK (0x%02X)", g_tx_buf[2]);
    }
}

static void test_len_overflow_reject(void)
{
    uint8_t buf[16];

    printf("\n[长度超限拒绝]\n");

    /* 手工构一个 LEN 超限的帧头 */
    buf[0] = PROTO_SYNC0;
    buf[1] = PROTO_SYNC1;
    buf[2] = PROTO_CMD_IMAGE_DATA;
    buf[3] = 0x01;
    buf[4] = 0xFF;      /* LEN 低字节 */
    buf[5] = 0xFF;      /* LEN 高字节 -> 0xFFFF，远超上限 */

    g_rx.count = 0;
    g_tx_len = 0;
    feed_frame(buf, 6);

    CHECK(g_rx.count == 0, "超长帧未被交付");
    CHECK(g_tx_len > 0, "发出了 NAK");
}

static void test_resync_after_garbage(void)
{
    uint8_t  frame[64];
    uint16_t n;
    uint8_t  garbage[] = {0x00, 0xFF, 0x5A, 0x33};   /* 故意混入误导字节 */
    uint8_t  p[1] = {0x01};

    printf("\n[帧头错位重新同步]\n");

    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_PAUSE, 0x02, p, 1);

    g_rx.count = 0;
    proto_reset();

    /* 先喂垃圾，再喂正常帧 */
    feed_frame(garbage, sizeof(garbage));
    feed_frame(frame, n);

    CHECK(g_rx.count == 1, "垃圾数据后仍能解析出 1 帧（实际 %d）", g_rx.count);
    CHECK(g_rx.last_cmd == PROTO_CMD_PAUSE, "命令码正确 = 0x%02X", g_rx.last_cmd);
}

static void test_sync0_repeat(void)
{
    uint8_t  frame[64];
    uint16_t n;
    uint8_t  p[0];
    uint8_t  prefix[] = {0xA5, 0xA5, 0xA5, 0x5A};  /* 连续 SYNC0 干扰 */

    printf("\n[连续 SYNC0 处理]\n");

    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_STOP, 0x03, p, 0);

    g_rx.count = 0;
    proto_reset();

    feed_frame(prefix, sizeof(prefix) - 1);   /* 只喂 3 个 A5 */
    feed_frame(frame, n);

    CHECK(g_rx.count == 1, "连续 A5 后仍能解析（实际 %d）", g_rx.count);
}

static void test_image_stream_parse(void)
{
    uint8_t  frame[256];
    uint16_t n;
    uint8_t  begin_p[9];
    uint16_t w = 4, h = 2;      /* 小图：4x2 = 8 像素 = 16 字节 */
    uint16_t i;

    printf("\n[图像流解析]\n");

    /* IMAGE_BEGIN: w, h, x, y, fmt（小端） */
    begin_p[0] = (uint8_t)(w & 0xFF); begin_p[1] = (uint8_t)(w >> 8);
    begin_p[2] = (uint8_t)(h & 0xFF); begin_p[3] = (uint8_t)(h >> 8);
    begin_p[4] = 0; begin_p[5] = 0;   /* x = 0 */
    begin_p[6] = 0; begin_p[7] = 0;   /* y = 0 */
    begin_p[8] = 0;                   /* fmt = RGB565 */

    g_rx.count = 0;
    proto_reset();

    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_IMAGE_BEGIN, 0x10,
                          begin_p, 9);
    feed_frame(frame, n);
    CHECK(g_rx.count == 1, "IMAGE_BEGIN 被解析");
    CHECK(g_rx.last_len == 9, "BEGIN 负载 9 字节");
    CHECK(g_rx.payload[0] == 4 && g_rx.payload[2] == 2,
          "宽=%u 高=%u", g_rx.payload[0], g_rx.payload[2]);

    /* IMAGE_DATA: 16 字节像素数据 */
    {
        uint8_t px[16];
        for (i = 0; i < 16; i++) {
            px[i] = (uint8_t)(i * 3);
        }
        n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_IMAGE_DATA, 0x11,
                              px, 16);
        g_rx.count = 0;
        feed_frame(frame, n);
        CHECK(g_rx.count == 1, "IMAGE_DATA 被解析");
        CHECK(g_rx.last_len == 16, "DATA 长度 = %u", g_rx.last_len);
        CHECK(memcmp(g_rx.payload, px, 16) == 0, "像素数据完整");
    }

    /* IMAGE_END */
    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_IMAGE_END, 0x12,
                          NULL, 0);
    g_rx.count = 0;
    feed_frame(frame, n);
    CHECK(g_rx.count == 1, "IMAGE_END 被解析");
    CHECK(g_rx.last_len == 0, "END 无负载");
}

static void test_zero_length_frame(void)
{
    uint8_t  frame[32];
    uint16_t n;

    printf("\n[零长度帧]\n");

    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_PING, 0x09, NULL, 0);
    CHECK(n == (uint16_t)(PROTO_HEADER_SIZE + PROTO_TAIL_SIZE),
          "零负载帧长度 = %u", n);

    g_rx.count = 0;
    proto_reset();
    feed_frame(frame, n);
    CHECK(g_rx.count == 1, "零负载帧能解析（实际 %d）", g_rx.count);
}

static void test_consecutive_frames(void)
{
    uint8_t  f1[32], f2[32], f3[32];
    uint16_t n1, n2, n3;

    printf("\n[连续多帧]\n");

    n1 = proto_build_frame(f1, sizeof(f1), PROTO_CMD_PING,  0x01, NULL, 0);
    n2 = proto_build_frame(f2, sizeof(f2), PROTO_CMD_PLAY,  0x02, NULL, 0);
    n3 = proto_build_frame(f3, sizeof(f3), PROTO_CMD_PAUSE, 0x03, NULL, 0);

    g_rx.count = 0;
    proto_reset();

    feed_frame(f1, n1);
    feed_frame(f2, n2);
    feed_frame(f3, n3);

    CHECK(g_rx.count == 3, "连续 3 帧全部解析（实际 %d）", g_rx.count);
    CHECK(g_rx.last_cmd == PROTO_CMD_PAUSE, "最后一帧命令正确");
}

static void test_ack_content(void)
{
    uint8_t  frame[32];
    uint16_t n;

    printf("\n[ACK 应答内容]\n");

    n = proto_build_frame(frame, sizeof(frame), PROTO_CMD_PING, 0x0A, NULL, 0);

    g_tx_len = 0;
    proto_reset();
    feed_frame(frame, n);

    CHECK(g_tx_len > 0, "发出了应答（%u 字节）", g_tx_len);
    if (g_tx_len >= PROTO_HEADER_SIZE + 3 + PROTO_TAIL_SIZE) {
        CHECK(g_tx_buf[2] == PROTO_CMD_ACK, "应答类型 = ACK (0x%02X)", g_tx_buf[2]);
        CHECK(g_tx_buf[PROTO_HEADER_SIZE] == PROTO_ERR_NONE,
              "错误码 = 0（成功）");
        CHECK(g_tx_buf[PROTO_HEADER_SIZE + 1] == PROTO_CMD_PING,
              "回应的原命令 = PING");
    }
}

/* ==================== 主函数 ==================== */

int main(void)
{
    printf("=== proto.c 协议层验证 ===\n");

    proto_set_callback(test_callback);
    proto_set_tx(test_tx);
    proto_reset();

    test_crc_known_value();
    test_build_and_parse();
    test_crc_reject();
    test_len_overflow_reject();
    test_resync_after_garbage();
    test_sync0_repeat();
    test_image_stream_parse();
    test_zero_length_frame();
    test_consecutive_frames();
    test_ack_content();

    printf("\n=== %d 项检查，%d 项失败 ===\n", g_run, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
