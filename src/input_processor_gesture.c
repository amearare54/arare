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
 *
 * 1回はじく＝1回だけ発火する。次が出るのは、中央へ戻して（大きな動きが reset-ms 途切れて）から。
 *   倒したまま別の向きへ動かしても出さない。離したときにバネで反対側へ跳ね返る分で
 *   逆向き（奥→手前など）が誤って出るのを防ぐため。
 *   以前は倒し続けるとしきい値を超えるたびに発火していた。いっぱいまで倒すと
 *   1レポート（約30ms）で 36 積算されるため、しきい値 40 なら 1 秒に十数回 ⌃← などが出てしまう
 *   （Mission Control は開閉を繰り返す）。2026-09-22 に変更。
 * 倒し続けて繰り返したいときは repeat-ms を指定する（キーリピートと同じ考え方）。
 *
 * |値| が min-value 以下のイベントは「中央付近の揺れ・中点のずれ」として無視する。
 *   電池が減って 3V3 が下がると、スティックの出力（電源に比例）と ADC の基準（内部0.6V、絶対値）が
 *   ずれて中点が動く。そのとき小さな値が出続けても、発火も再武装の妨げもしないようにするため。
 *
 * 状態はスティック1本ぶんを全インスタンスで共有する。arare Studio はレイヤーごとに
 * joy_gesture_N を生成するので、インスタンスごとに持つと、倒したままレイヤーを切り替えた
 * 瞬間に新しいインスタンスが「中央から来た」とみなして2回目を出してしまう。
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
    int32_t repeat_ms; /* 0 = 倒したままでは繰り返さない */
    int32_t min_value; /* |値| がこれ以下のイベントは無視する */
    const struct zmk_behavior_binding *bindings; /* 上・下・左・右 */
};

/* スティック1本ぶんの状態（全インスタンスで共有。理由は冒頭のコメント） */
static struct {
    int32_t acc_x;
    int32_t acc_y;
    int64_t last_ms;      /* 最後に大きな動き（|値| > min-value）が来た時刻 */
    int held_dir;         /* 直前に発火した向き。-1 = 中央（再武装済み） */
    int64_t next_repeat_ms;
} stick = {.held_dir = -1};

struct gesture_data {
    const struct device *dev;
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

    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* 大きな動きが reset-ms 途切れたら、中央へ戻したとみなして積算を捨て、再武装する。
     * 倒している間は約30msごとにレポートが来るので、100ms なら取り違えない */
    int64_t now = k_uptime_get();
    if (now - stick.last_ms > cfg->reset_ms) {
        stick.acc_x = 0;
        stick.acc_y = 0;
        stick.held_dir = -1;
    }

    /* 小さな値は中央付近の揺れ・中点のずれとして無視する（積算もしない） */
    if (abs(event->value) > cfg->min_value) {
        stick.last_ms = now;
        if (event->code == INPUT_REL_X) {
            stick.acc_x += event->value;
        } else {
            stick.acc_y += event->value;
        }
    }

    /* 向きはレポートの区切り（sync）でだけ判定する。ドライバは1回のレポートを
     * AIN2（REL_Y）→ AIN3（REL_X）の順に送るので、途中で判定すると斜めが上下に寄る */
    if (event->sync) {
        /* 大きく傾いている軸を優先し、斜めで2方向が同時に出るのを防ぐ */
        int dir = -1;
        if (abs(stick.acc_x) >= cfg->threshold && abs(stick.acc_x) >= abs(stick.acc_y)) {
            dir = stick.acc_x > 0 ? DIR_RIGHT : DIR_LEFT;
        } else if (abs(stick.acc_y) >= cfg->threshold) {
            dir = stick.acc_y > 0 ? DIR_DOWN : DIR_UP;
        }

        if (dir >= 0) {
            stick.acc_x = 0;
            stick.acc_y = 0;
            if (stick.held_dir < 0) {
                /* 中央からはじいた → 1回だけ出す */
                stick.held_dir = dir;
                stick.next_repeat_ms = now + cfg->repeat_ms;
                fire(dev, (enum arare_dir)dir, state);
            } else if (dir == stick.held_dir && cfg->repeat_ms > 0 &&
                       now >= stick.next_repeat_ms) {
                /* 同じ向きに倒し続けている → repeat-ms ごとに繰り返す */
                stick.next_repeat_ms = now + cfg->repeat_ms;
                fire(dev, (enum arare_dir)dir, state);
            }
            /* それ以外（中央を通らない向きの切り替え・跳ね返り）は捨てる */
        }
    }

    /* ポインタは動かさない（スティックはジェスチャ専用。ポインタはトラックボール）。
     * レイヤー別の上書き（arare Studio の joy_ov_N。process-next なし）では、ZMK が
     * このプロセッサの STOP を CONTINUE に置き換えて後段へ流し、カーソルが動いてしまう
     * （zmk v0.3.0 app/src/pointing/input_listener.c filter_with_input_config）。
     * どちらの経路でも何も起きないよう、値そのものを消しておく。 */
    event->value = 0;
    event->sync = false;
    return ZMK_INPUT_PROC_STOP;
}

static struct zmk_input_processor_driver_api gesture_driver_api = {
    .handle_event = gesture_handle_event,
};

static int gesture_init(const struct device *dev) {
    struct gesture_data *data = dev->data;
    data->dev = dev;
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
        .repeat_ms = DT_INST_PROP(n, repeat_ms),                                                   \
        .min_value = DT_INST_PROP(n, min_value),                                                   \
        .bindings = gesture_bindings_##n,                                                          \
    };                                                                                             \
    static struct gesture_data gesture_data_##n;                                                   \
    DEVICE_DT_INST_DEFINE(n, &gesture_init, NULL, &gesture_data_##n, &gesture_config_##n,          \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &gesture_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GESTURE_INST)
