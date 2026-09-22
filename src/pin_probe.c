/*
 * 診断用（diag-L-enc ブランチ限定・main には入れない）
 * 左ノブ（EC12）の A=D2=P0.28 / B=D3=P0.29 の電圧を 50ms ごとに直接読み、
 * 変化したとき（と1秒ごと）にログへ出す。EC11 ドライバが何も出さないとき、
 *   両方ずっと 0 → A/B が GND（真ん中の C）とつながっている（はんだブリッジ）
 *   両方ずっと 1 → C（GND）が浮いている／ノブの接点が開いたまま
 * を見分けるため。ピンの設定（入力・プルアップ）は EC11 ドライバが済ませている。
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(arare_probe, LOG_LEVEL_INF);

static const struct device *const gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static void probe(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(probe_work, probe);
static int last = -1;
static int ticks;

static void probe(struct k_work *work) {
    int a = gpio_pin_get_raw(gpio0, 28);
    int b = gpio_pin_get_raw(gpio0, 29);
    int v = (a << 1) | b;
    if (v != last || (++ticks % 20) == 0) {
        LOG_INF("ENC A(D2)=%d B(D3)=%d%s", a, b, v != last ? "  <- changed" : "");
        last = v;
    }
    k_work_schedule(&probe_work, K_MSEC(50));
}

static int probe_init(void) {
    k_work_schedule(&probe_work, K_MSEC(3000));
    return 0;
}

SYS_INIT(probe_init, APPLICATION, 99);
