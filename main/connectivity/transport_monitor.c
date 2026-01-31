#include "connectivity/transport_monitor.h"

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* ESP-Hosted stats headers - paths relative to the component */
#include "host/utils/stats.h"
#include "host/drivers/transport/transport_drv.h"

static const char *TAG = "transport_monitor";

/* Snapshot of raw counters from previous interval */
typedef struct {
  uint32_t sta_rx_in;
  uint32_t sta_rx_out;
  uint32_t sta_tx_in_pass;
  uint32_t sta_tx_trans_in;
  uint32_t sta_tx_flowctrl_drop;
  uint32_t sta_tx_out;
  uint32_t sta_tx_out_drop;
  uint32_t sta_flow_ctrl_on;
  uint32_t sta_flow_ctrl_off;
  int64_t timestamp_us;
  bool valid;
} counter_snapshot_t;

static struct {
  TaskHandle_t task;
  SemaphoreHandle_t sem;
  TimerHandle_t timer;
  counter_snapshot_t prev;
  transport_stats_cb_t cb;
  void *cb_ctx;
  transport_stats_t latest;
  bool latest_valid;
  bool running;
} s_monitor = {0};

static int clamp_delta(int64_t delta)
{
  if (delta < 0) {
    return 0;
  }
  if (delta > INT32_MAX) {
    return INT32_MAX;
  }
  return (int)delta;
}

static void timer_callback(TimerHandle_t xTimer)
{
  (void)xTimer;
  if (s_monitor.sem) {
    xSemaphoreGive(s_monitor.sem);
  }
}

static void monitor_task(void *arg)
{
  (void)arg;

  while (1) {
    if (xSemaphoreTake(s_monitor.sem, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (!s_monitor.running) {
      continue;
    }

    /* Snapshot current counters from ESP-Hosted globals */
    struct pkt_stats_t current;
    memcpy(&current, &pkt_stats, sizeof(current));
    volatile uint8_t throttling = wifi_tx_throttling;
    int64_t now = esp_timer_get_time();

    if (!s_monitor.prev.valid) {
      /* First sample after connect: just prime the baseline */
      s_monitor.prev.sta_rx_in = current.sta_rx_in;
      s_monitor.prev.sta_rx_out = current.sta_rx_out;
      s_monitor.prev.sta_tx_in_pass = current.sta_tx_in_pass;
      s_monitor.prev.sta_tx_trans_in = current.sta_tx_trans_in;
      s_monitor.prev.sta_tx_flowctrl_drop = current.sta_tx_flowctrl_drop;
      s_monitor.prev.sta_tx_out = current.sta_tx_out;
      s_monitor.prev.sta_tx_out_drop = current.sta_tx_out_drop;
      s_monitor.prev.sta_flow_ctrl_on = current.sta_flow_ctrl_on;
      s_monitor.prev.sta_flow_ctrl_off = current.sta_flow_ctrl_off;
      s_monitor.prev.timestamp_us = now;
      s_monitor.prev.valid = true;
      continue;
    }

    /* Compute elapsed time in ms (convert us to ms) */
    int64_t elapsed_us = now - s_monitor.prev.timestamp_us;
    if (elapsed_us <= 0) {
      elapsed_us = 1; /* Protect against div-by-zero or negative */
    }
    int period_ms = (int)(elapsed_us / 1000);
    if (period_ms <= 0) {
      period_ms = 1;
    }

    /* Compute deltas and per-second rates */
    int64_t tx_delta = (int64_t)current.sta_tx_in_pass - (int64_t)s_monitor.prev.sta_tx_in_pass;
    int64_t rx_delta = (int64_t)current.sta_rx_in - (int64_t)s_monitor.prev.sta_rx_in;
    int64_t drop_fc_delta = (int64_t)current.sta_tx_flowctrl_drop - (int64_t)s_monitor.prev.sta_tx_flowctrl_drop;
    int64_t drop_out_delta = (int64_t)current.sta_tx_out_drop - (int64_t)s_monitor.prev.sta_tx_out_drop;

    int tx_pps = clamp_delta(tx_delta) * 1000 / period_ms;
    int rx_pps = clamp_delta(rx_delta) * 1000 / period_ms;
    int drop_pps = (clamp_delta(drop_fc_delta) + clamp_delta(drop_out_delta)) * 1000 / period_ms;

    int flowctl_on = clamp_delta((int64_t)current.sta_flow_ctrl_on - (int64_t)s_monitor.prev.sta_flow_ctrl_on);
    int flowctl_off = clamp_delta((int64_t)current.sta_flow_ctrl_off - (int64_t)s_monitor.prev.sta_flow_ctrl_off);

    /* Update latest stats */
    s_monitor.latest.tx_pps = tx_pps;
    s_monitor.latest.rx_pps = rx_pps;
    s_monitor.latest.drop_pps = drop_pps;
    s_monitor.latest.flowctl_on = flowctl_on;
    s_monitor.latest.flowctl_off = flowctl_off;
    s_monitor.latest.throttling = throttling != 0;
    s_monitor.latest.period_ms = period_ms;
    s_monitor.latest_valid = true;

    /* Log the stats */
    ESP_LOGI(TAG, "tx=%d p/s rx=%d p/s drop=%d p/s flowctl=%d/%d throttling=%s (period=%dms)",
             tx_pps, rx_pps, drop_pps, flowctl_on, flowctl_off,
             throttling ? "yes" : "no", period_ms);

    /* Notify callback if registered */
    if (s_monitor.cb) {
      s_monitor.cb(&s_monitor.latest, s_monitor.cb_ctx);
    }

    /* Store current as previous for next interval */
    s_monitor.prev.sta_rx_in = current.sta_rx_in;
    s_monitor.prev.sta_rx_out = current.sta_rx_out;
    s_monitor.prev.sta_tx_in_pass = current.sta_tx_in_pass;
    s_monitor.prev.sta_tx_trans_in = current.sta_tx_trans_in;
    s_monitor.prev.sta_tx_flowctrl_drop = current.sta_tx_flowctrl_drop;
    s_monitor.prev.sta_tx_out = current.sta_tx_out;
    s_monitor.prev.sta_tx_out_drop = current.sta_tx_out_drop;
    s_monitor.prev.sta_flow_ctrl_on = current.sta_flow_ctrl_on;
    s_monitor.prev.sta_flow_ctrl_off = current.sta_flow_ctrl_off;
    s_monitor.prev.timestamp_us = now;
  }
}

esp_err_t transport_monitor_start(void)
{
  if (s_monitor.running) {
    return ESP_OK;
  }

  /* Create semaphore if needed */
  if (s_monitor.sem == NULL) {
    s_monitor.sem = xSemaphoreCreateBinary();
    if (s_monitor.sem == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  /* Create task if needed */
  if (s_monitor.task == NULL) {
    BaseType_t ok = xTaskCreate(monitor_task, "trans_mon", 3072, NULL, 3, &s_monitor.task);
    if (ok != pdPASS) {
      return ESP_ERR_NO_MEM;
    }
  }

  /* Create timer if needed */
  if (s_monitor.timer == NULL) {
    s_monitor.timer = xTimerCreate("trans_tmr",
                                   pdMS_TO_TICKS(CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS),
                                   pdTRUE, /* auto-reload */
                                   NULL,
                                   timer_callback);
    if (s_monitor.timer == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  /* Reset snapshot state so next sample primes */
  memset(&s_monitor.prev, 0, sizeof(s_monitor.prev));
  s_monitor.latest_valid = false;
  s_monitor.running = true;

  /* Start timer */
  xTimerStart(s_monitor.timer, 0);

  ESP_LOGI(TAG, "Transport monitor started (period=%dms)", CONFIG_THEO_TRANSPORT_MONITOR_PERIOD_MS);
  return ESP_OK;
}

void transport_monitor_stop(void)
{
  if (!s_monitor.running) {
    return;
  }

  s_monitor.running = false;

  if (s_monitor.timer) {
    xTimerStop(s_monitor.timer, portMAX_DELAY);
  }

  /* Reset snapshot so next start primes fresh */
  memset(&s_monitor.prev, 0, sizeof(s_monitor.prev));
  s_monitor.latest_valid = false;

  ESP_LOGI(TAG, "Transport monitor stopped");
}

void transport_monitor_register_callback(transport_stats_cb_t cb, void *user_ctx)
{
  s_monitor.cb = cb;
  s_monitor.cb_ctx = user_ctx;
}

bool transport_monitor_get_latest(transport_stats_t *out_stats)
{
  if (!out_stats) {
    return false;
  }
  if (!s_monitor.latest_valid) {
    return false;
  }
  memcpy(out_stats, &s_monitor.latest, sizeof(*out_stats));
  return true;
}
