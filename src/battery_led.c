/*
 * arare — 電池残量をマイコン(XIAO nRF52840)内蔵のRGB LEDで知らせる
 *
 * 方針:
 *   常時点灯はしない（電池を食うため）。見たいとき・知らせるべきときだけ光らせる。
 *     - 起動して最初に残量が読めたとき … 残量色を短く点灯
 *     - スリープ/アイドルから復帰したとき … 残量色を短く点灯
 *     - 残量が少ないとき … 使用中に限り、定期的に赤く点滅して充電を促す
 *     - 閾値を下向きに跨いだ瞬間 … その場で点滅して知らせる
 *     - USB接続中 … 残量色を点灯したまま（充電の進みが分かる。電源はUSB側）
 *
 * LEDはボード定義で ACTIVE_LOW。論理値で扱えば極性はZephyrが吸収する。
 * alias の割り当ては led0=赤 / led1=青 / led2=緑 で、青と緑が直感と逆順なので注意。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/activity_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

LOG_MODULE_REGISTER(arare_battery_led, CONFIG_ZMK_LOG_LEVEL);

#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_BLUE_NODE  DT_ALIAS(led1)
#define LED_GREEN_NODE DT_ALIAS(led2)

#if !DT_NODE_EXISTS(LED_RED_NODE) || !DT_NODE_EXISTS(LED_GREEN_NODE)
#error "arare: このボードには led0 / led2 の alias がありません"
#endif

static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
#if DT_NODE_EXISTS(LED_BLUE_NODE)
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);
#endif

/* GPIOのON/OFFだけなので中間色は作れない。赤+緑=橙 を使う */
enum arare_led_color { COLOR_OFF, COLOR_GREEN, COLOR_AMBER, COLOR_RED };

#define BLINK_ON_MS 120
#define BLINK_OFF_MS 200

static uint8_t last_soc;
static bool have_soc;
static bool ready;

static struct k_work_delayable blink_work;
static struct k_work_delayable glance_work;
static struct k_work_delayable warn_work;

static enum arare_led_color blink_color;
static int blink_steps_left;
static bool blink_lit;

static void led_apply(enum arare_led_color c) {
    if (!ready) {
        return;
    }
    gpio_pin_set_dt(&led_red, c == COLOR_RED || c == COLOR_AMBER);
    gpio_pin_set_dt(&led_green, c == COLOR_GREEN || c == COLOR_AMBER);
#if DT_NODE_EXISTS(LED_BLUE_NODE)
    gpio_pin_set_dt(&led_blue, 0);
#endif
}

static enum arare_led_color color_for_level(uint8_t soc) {
    if (soc >= CONFIG_ARARE_BATTERY_LED_GOOD_PCT) {
        return COLOR_GREEN;
    }
    if (soc > CONFIG_ARARE_BATTERY_LED_LOW_PCT) {
        return COLOR_AMBER;
    }
    return COLOR_RED;
}

static bool usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

/* 何も知らせていないときの状態。USB接続中だけ点けたままにする */
static void apply_resting(void) {
    if (have_soc && usb_powered()) {
        led_apply(color_for_level(last_soc));
    } else {
        led_apply(COLOR_OFF);
    }
}

static void blink_step(struct k_work *work) {
    ARG_UNUSED(work);
    if (blink_steps_left <= 0) {
        apply_resting();
        return;
    }
    blink_lit = !blink_lit;
    led_apply(blink_lit ? blink_color : COLOR_OFF);
    blink_steps_left--;
    k_work_schedule(&blink_work, K_MSEC(blink_lit ? BLINK_ON_MS : BLINK_OFF_MS));
}

static void blink(enum arare_led_color color, int times) {
    k_work_cancel_delayable(&glance_work);
    blink_color = color;
    blink_steps_left = times * 2; /* 点灯と消灯で1往復 */
    blink_lit = false;
    k_work_reschedule(&blink_work, K_NO_WAIT);
}

static void glance_end(struct k_work *work) {
    ARG_UNUSED(work);
    apply_resting();
}

/* 現在の残量色を短く見せる */
static void glance(void) {
    if (!have_soc) {
        return;
    }
    k_work_cancel_delayable(&blink_work);
    blink_steps_left = 0;
    led_apply(color_for_level(last_soc));
    k_work_reschedule(&glance_work, K_MSEC(CONFIG_ARARE_BATTERY_LED_GLANCE_MS));
}

/* 充電を促すべき状況か。使っていないときや充電中は促さない */
static bool warning_needed(void) {
    if (!have_soc || last_soc > CONFIG_ARARE_BATTERY_LED_LOW_PCT) {
        return false;
    }
    if (usb_powered()) {
        return false;
    }
    return zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
}

static int warning_interval_sec(void) {
    return (last_soc <= CONFIG_ARARE_BATTERY_LED_CRITICAL_PCT)
               ? CONFIG_ARARE_BATTERY_LED_CRITICAL_INTERVAL_SEC
               : CONFIG_ARARE_BATTERY_LED_WARN_INTERVAL_SEC;
}

static void warn_fire(struct k_work *work) {
    ARG_UNUSED(work);
    if (!warning_needed()) {
        return; /* 再スケジュールしない＝警告停止 */
    }
    blink(COLOR_RED, (last_soc <= CONFIG_ARARE_BATTERY_LED_CRITICAL_PCT) ? 3 : 2);
    k_work_reschedule(&warn_work, K_SECONDS(warning_interval_sec()));
}

static void update_warning(void) {
    if (warning_needed()) {
        if (!k_work_delayable_is_pending(&warn_work)) {
            k_work_reschedule(&warn_work, K_SECONDS(warning_interval_sec()));
        }
    } else {
        k_work_cancel_delayable(&warn_work);
    }
}

static bool crossed_down(uint8_t before, uint8_t now) {
    const int low = CONFIG_ARARE_BATTERY_LED_LOW_PCT;
    const int crit = CONFIG_ARARE_BATTERY_LED_CRITICAL_PCT;
    return (before > low && now <= low) || (before > crit && now <= crit);
}

static void on_battery(uint8_t soc) {
    const bool first = !have_soc;
    const uint8_t before = last_soc;
    last_soc = soc;
    have_soc = true;

    if (first) {
        LOG_INF("arare: 電池 %u%% — 起動時の残量を表示", soc);
        glance(); /* 起動後、最初に残量が読めた時点で知らせる */
    } else if (crossed_down(before, soc)) {
        LOG_INF("arare: 電池 %u%% — 閾値を下回ったので警告", soc);
        blink(COLOR_RED, (soc <= CONFIG_ARARE_BATTERY_LED_CRITICAL_PCT) ? 3 : 2);
    } else if (blink_steps_left <= 0 && !k_work_delayable_is_pending(&glance_work)) {
        apply_resting(); /* USB接続中の色を新しい残量に追従させる */
    }
    update_warning();
}

static int arare_battery_led_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *bat = as_zmk_battery_state_changed(eh);
    if (bat != NULL) {
        on_battery(bat->state_of_charge);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_activity_state_changed(eh) != NULL) {
        if (zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE) {
            glance(); /* 復帰したら残量を一目で示す */
        } else {
            /* 眠っている間は消しておく（点けっぱなしで電池を減らさない） */
            k_work_cancel_delayable(&blink_work);
            k_work_cancel_delayable(&glance_work);
            blink_steps_left = 0;
            led_apply(COLOR_OFF);
        }
        update_warning();
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_USB)
    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        if (blink_steps_left <= 0 && !k_work_delayable_is_pending(&glance_work)) {
            apply_resting();
        }
        update_warning();
        return ZMK_EV_EVENT_BUBBLE;
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(arare_battery_led, arare_battery_led_listener);
ZMK_SUBSCRIPTION(arare_battery_led, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(arare_battery_led, zmk_activity_state_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(arare_battery_led, zmk_usb_conn_state_changed);
#endif

static int arare_battery_led_init(void) {
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green)) {
        LOG_ERR("arare: LEDのGPIOが使えません");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
#if DT_NODE_EXISTS(LED_BLUE_NODE)
    if (gpio_is_ready_dt(&led_blue)) {
        gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
    }
#endif
    ready = true;

    k_work_init_delayable(&blink_work, blink_step);
    k_work_init_delayable(&glance_work, glance_end);
    k_work_init_delayable(&warn_work, warn_fire);

    /* 起動直後は測定前なので、ここでは点けない。最初の電池イベントで知らせる */
    return 0;
}

SYS_INIT(arare_battery_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
