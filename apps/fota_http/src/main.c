#include <zephyr/kernel.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <net/fota_download.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(fota_http);

#define FOTA_HOST "42group.fr:4242"
#define FOTA_FILE "zephyr.signed.bin"

#define SLEEP_TIME_MS 500

#define LED0_NODE DT_ALIAS(led0)
#define BUTTON0_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON0_NODE, gpios);

static struct gpio_callback button_cb_data;
static K_SEM_DEFINE(lte_connected, 0, 1);
static K_SEM_DEFINE(fota_start_sem, 0, 1);

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
                           uint32_t pins)
{
  LOG_INF("Button pressed, triggering FOTA");
  k_sem_give(&fota_start_sem);
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
  switch (evt->type) {
  case LTE_LC_EVT_NW_REG_STATUS:
    if (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME &&
        evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING) {
      break;
    }
    LOG_INF("LTE registered: %s",
            evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "home network"
                                                                : "roaming");
    k_sem_give(&lte_connected);
    break;
  case LTE_LC_EVT_RRC_UPDATE:
    LOG_INF("RRC mode: %s",
            evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "connected" : "idle");
    break;
  default:
    break;
  }
}

static int modem_configure(void)
{
  int err;

  LOG_INF("Initializing modem library");
  err = nrf_modem_lib_init();
  if (err) {
    LOG_ERR("Failed to initialize the modem library, error: %d", err);
    return err;
  }

  LOG_INF("Connecting to LTE network");
  err = lte_lc_connect_async(lte_handler);
  if (err) {
    LOG_ERR("lte_lc_connect_async failed: %d", err);
    return err;
  }

  k_sem_take(&lte_connected, K_FOREVER);
  LOG_INF("Connected to LTE network");

  return 0;
}

static void fota_dl_handler(const struct fota_download_evt *evt)
{
  switch (evt->id) {
  case FOTA_DOWNLOAD_EVT_PROGRESS:
    LOG_INF("FOTA progress: %d%%", evt->progress);
    break;
  case FOTA_DOWNLOAD_EVT_FINISHED:
    LOG_INF("FOTA download complete, rebooting...");
    sys_reboot(SYS_REBOOT_COLD);
    break;
  case FOTA_DOWNLOAD_EVT_CANCELLED:
    LOG_WRN("FOTA cancelled");
    break;
  case FOTA_DOWNLOAD_EVT_ERROR:
    LOG_ERR("FOTA error: %d", evt->cause);
    break;
  default:
    break;
  }
}

int main(void)
{
  int ret;

  if (!gpio_is_ready_dt(&led)) {
    LOG_ERR("LED device not ready");
    return 0;
  }
  gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

  if (!gpio_is_ready_dt(&button)) {
    LOG_ERR("Button device not ready");
    return 0;
  }
  gpio_pin_configure_dt(&button, GPIO_INPUT);
  gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
  gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
  gpio_add_callback(button.port, &button_cb_data);

  ret = modem_configure();
  if (ret) {
    LOG_ERR("modem_configure failed: %d", ret);
    return ret;
  }

  ret = fota_download_init(fota_dl_handler);
  if (ret) {
    LOG_ERR("fota_download_init failed: %d", ret);
    return ret;
  }

  LOG_INF("Press button 1 to start FOTA");

  while (1) {
    gpio_pin_toggle_dt(&led);

    if (k_sem_take(&fota_start_sem, K_NO_WAIT) == 0) {
      LOG_INF("Starting FOTA download from %s/%s", FOTA_HOST, FOTA_FILE);
      ret = fota_download_start(FOTA_HOST, FOTA_FILE, -1, 0, 0);
      if (ret) {
        LOG_ERR("fota_download_start failed: %d", ret);
      }
    }

    k_msleep(SLEEP_TIME_MS);
  }

  return 0;
}
