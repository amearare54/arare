/*
 * arare — ジョイスティックの「はじく」操作をビヘイビアに変える入力プロセッサ
 *
 * 目的: トラックパッドの3本指スワイプ相当を、親指のスティックで行う。
 *
 * ZMK標準の zmk,input-processor-behaviors では実現できない。あちらは
 *   zmk_behavior_invoke_binding(..., event->value)
 * の第3引数が bool pressed なので、移動量が真偽値に潰れて左右の区別が消え、
 * さらにドライバが値0を報告しないため押しっぱなしのまま解放されない。
 *
 * ここでは相対移動を軸ごとに積算し、しきい値を超えた瞬間に「押して離す」を1回送る。
 * スティックを中央へ戻すとイベントが途切れるので、reset-ms 経過で積算を捨てて再武装する。
 * 倒し続けた場合はしきい値ごとに繰り返し発火する（スペースを連続で送れる）。
 */

#define DT_DRV_COMPAT arare_input_processor_gesture

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

#include <drivers/input_processor.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/virtual_key_position.h>

LOG_MODULE_REGISTER(arare_gesture, CONFIG_ZMK_LOG_LEVEL);

/* bindings の並び順 */
enum arare_dir { DIR_UP = 0, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_COUNT };

struct gesture_config {
    uint8_t index;
    int32_t threshold;
    int32_t reset_ms;
    int32_t tap_ms;
    const struct zmk_behavior_binding *bindings; /* 上・下・左・右 */
};

struct gesture_data {
    const struct device *dev;
    int32_t acc_x;
    int32_t acc_y;
    int64_t last_ms;
    struct k_work_delayable release_work;
    struct zmk_behavior_binding pending;
    struct zmk_behavior_binding_event pending_event;
    bool has_pending;
};

/* 押しっぱなしを避けるため、tap-ms 後に必ず離す */
static void release_pending(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct gesture_data *data = CONTAINER_OF(dwork, struct gesture_data, release_work);
    if (!data->has_pending) {
        return;
    }
    data->has_pending = false;
    zmk_behavior_invoke_binding(&data->pending, data->pending_event, false);
}

static void fire(const struct device *dev, enum arare_dir dir,
                 struct zmk_input_processor_state *state) {
    const struct gesture_config *cfg = dev->config;
    struct gesture_data *data = dev->data;

    /* 直前の発火がまだ離されていなければ、先に離してから次を出す */
    if (data->has_pending) {
        k_work_cancel_delayable(&data->release_work);
        data->has_pending = false;
        zmk_behavior_invoke_binding(&data->pending, data->pending_event, false);
    }

    struct zmk_behavior_binding_event ev = {
        .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
            state ? state->input_device_index : 0, cfg->index),
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    data->pending = cfg->bindings[dir];
    data->pending_event = ev;
    data->has_pending = true;

    LOG_DBG("arare gesture: dir=%d behavior=%s", dir, cfg->bindings[dir].behavior_dev);
    zmk_behavior_invoke_binding(&data->pending, ev, true);
    k_work_reschedule(&data->release_work, K_MSEC(cfg->tap_ms));
}

static int gesture_handle_event(const struct device *dev, struct input_event *event,
                                uint32_t param1, uint32_t param2,
                                struct zmk_input_processor_state *state) {
    const struct gesture_config *cfg = dev->config;
    struct gesture_data *data = dev->data;

    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* しばらく動きが無ければ中央へ戻したとみなして積算を捨てる */
    int64_t now = k_uptime_get();
    if (now - data->last_ms > cfg->reset_ms) {
        data->acc_x = 0;
        data->acc_y = 0;
    }
    data->last_ms = now;

    if (event->code == INPUT_REL_X) {
        data->acc_x += event->value;
    } else {
        data->acc_y += event->value;
    }

    /* 大きく傾いている軸を優先し、斜めで2方向が同時に出るのを防ぐ */
    if (abs(data->acc_x) >= cfg->threshold && abs(data->acc_x) >= abs(data->acc_y)) {
        fire(dev, data->acc_x > 0 ? DIR_RIGHT : DIR_LEFT, state);
        data->acc_x = 0;
        data->acc_y = 0;
    } else if (abs(data->acc_y) >= cfg->threshold) {
        fire(dev, data->acc_y > 0 ? DIR_DOWN : DIR_UP, state);
        data->acc_x = 0;
        data->acc_y = 0;
    }

    /* ポインタは動かさない（スティックはジェスチャ専用。ポインタはトラックボール） */
    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api gesture_driver_api = {
    .handle_event = gesture_handle_event,
};

static int gesture_init(const struct device *dev) {
    struct gesture_data *data = dev->data;
    data->dev = dev;
    data->last_ms = k_uptime_get();
    k_work_init_delayable(&data->release_work, release_pending);
    return 0;
}

#define GESTURE_INST(n)                                                                            \
    static const struct zmk_behavior_binding gesture_bindings_##n[] = {                            \
        LISTIFY(DT_INST_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))};  \
    BUILD_ASSERT(ARRAY_SIZE(gesture_bindings_##n) == DIR_COUNT,                                    \
                 "bindings は 上・下・左・右 の4つを指定してください");                            \
    static const struct gesture_config gesture_config_##n = {                                      \
        .index = n,                                                                                \
        .threshold = DT_INST_PROP(n, threshold),                                                   \
        .reset_ms = DT_INST_PROP(n, reset_ms),                                                     \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .bindings = gesture_bindings_##n,                                                          \
    };                                                                                             \
    static struct gesture_data gesture_data_##n;                                                   \
    DEVICE_DT_INST_DEFINE(n, &gesture_init, NULL, &gesture_data_##n, &gesture_config_##n,          \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &gesture_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GESTURE_INST)
