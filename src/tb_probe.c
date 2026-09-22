/*
 * 診断用（diag-R-tb ブランチ限定・main には入れない）
 * トラックボール（PAW3222）の状態を1秒ごとにログへ出す。
 *   ready=0 → ドライバの初期化が失敗している（起動時に製品IDが読めなかった＝SPI/電源/FFC）
 *   ready=1 で motion がボールを転がしても 1 のまま → MOTION 線（P0.09、XIAO 裏パッド15）が来ていない
 *   ready=1 で motion が 0 に落ちる → センサーは生きている（カーソルが動かないなら listener/HID 側）
 * MOTION は active low。変化したときは即座にも出す。起動直後のログは取りこぼしやすいので、
 * 起動時の結果に頼らず、いつ読んでも分かるようにしている。
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(arare_tb_probe, LOG_LEVEL_INF);

static const struct device *const tb = DEVICE_DT_GET(DT_NODELABEL(trackball));
static const struct device *const gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static void probe(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(probe_work, probe);
static int last = -1, lows, ticks;

static void probe(struct k_work *work) {
    int m = gpio_pin_get_raw(gpio0, 9);
    if (m == 0) {
        lows++;
    }
    if (m != last || (++ticks % 50) == 0) {
        LOG_INF("TB ready=%d motion(P0.09)=%d lows=%d%s", device_is_ready(tb), m, lows,
                m != last ? "  <- changed" : "");
        last = m;
    }
    k_work_schedule(&probe_work, K_MSEC(20));
}

static int probe_init(void) {
    k_work_schedule(&probe_work, K_MSEC(3000));
    return 0;
}

SYS_INIT(probe_init, APPLICATION, 99);
