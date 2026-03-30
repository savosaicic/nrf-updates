#include <modem/lte_lc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/lwm2m.h>
#include <zephyr/sys/reboot.h>
#include <modem/nrf_modem_lib.h>

LOG_MODULE_REGISTER(fota_lwm2m);

#define SERVER_URL    "coap://42group.fr:5683"
#define ENDPOINT_NAME "test_ovas"
#define SERVER_ID     1
#define LIFETIME_S    (60 * 5)

static K_SEM_DEFINE(lte_connected, 0, 1);

static const char manufacturer[] = "Nordic Semiconductor";
static const char model[]        = "nRF9151-DK";
static const char serial[]       = "SN-001";
static const char fw_ver[]       = "1.0.0";

static struct lwm2m_ctx client_ctx;

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

static void observe_cb(enum lwm2m_observe_event event,
                       struct lwm2m_obj_path *path, void *user_data)
{
  char buf[LWM2M_MAX_PATH_STR_SIZE];

  switch (event) {
  case LWM2M_OBSERVE_EVENT_OBSERVER_ADDED:
    LOG_INF("Observer added for %s", lwm2m_path_log_buf(buf, path));
    break;
  case LWM2M_OBSERVE_EVENT_OBSERVER_REMOVED:
    LOG_INF("Observer removed for %s", lwm2m_path_log_buf(buf, path));
    break;
  case LWM2M_OBSERVE_EVENT_NOTIFY_ACK:
    LOG_INF("Notify ACK for %s", lwm2m_path_log_buf(buf, path));
    break;
  case LWM2M_OBSERVE_EVENT_NOTIFY_TIMEOUT:
    LOG_WRN("Notify timeout for %s — triggering reg update",
            lwm2m_path_log_buf(buf, path));
    lwm2m_rd_client_update();
    break;
  }
}

static void rd_client_event(struct lwm2m_ctx *client,
                            enum lwm2m_rd_client_event event)
{
  switch (event) {
  case LWM2M_RD_CLIENT_EVENT_NONE:
    break;
  case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
    LOG_ERR("Bootstrap registration failure");
    break;
  case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
    LOG_INF("Bootstrap registration complete");
    break;
  case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE:
    LOG_INF("Bootstrap transfer complete");
    break;
  case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
    LOG_ERR("Registration failure");
    break;
  case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
    LOG_INF("Registration complete");
    break;
  case LWM2M_RD_CLIENT_EVENT_REG_TIMEOUT:
    LOG_WRN("Registration timeout");
    break;
  case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
    LOG_INF("Registration update complete");
    break;
  case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
    LOG_ERR("Deregister failure");
    break;
  case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
    LOG_WRN("Disconnected");
    break;
  case LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF:
    LOG_DBG("Queue mode RX window closed");
    break;
  case LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED:
    LOG_DBG("Engine suspended");
    break;
  case LWM2M_RD_CLIENT_EVENT_SERVER_DISABLED:
    LOG_DBG("Server disabled");
    break;
  case LWM2M_RD_CLIENT_EVENT_REG_UPDATE:
    LOG_DBG("Registration update sent");
    break;
  case LWM2M_RD_CLIENT_EVENT_DEREGISTER:
    LOG_DBG("Deregister sent");
    break;

  /*
   * NETWORK_ERROR must call lwm2m_rd_client_stop()
   * Failing to do so leaves the engine in a broken state
   */
  case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
    LOG_ERR("Network error, stopping client");
    lwm2m_rd_client_stop(client, rd_client_event, true);
    break;
  }
}

static int device_reboot_cb(uint16_t obj_inst_id, uint8_t *args,
                            uint16_t args_len)
{
  LOG_INF("Reboot requested by server");
  sys_reboot(SYS_REBOOT_COLD);
  return 0;
}

static int firmware_update_cb(uint16_t obj_inst_id, uint8_t *args,
                              uint16_t args_len)
{
  LOG_INF("Firmware update requested, applying...");
  sys_reboot(SYS_REBOOT_COLD);
  return 0;
}

static void setup_firmware_object(void)
{
  lwm2m_firmware_set_update_cb(firmware_update_cb);
}

static void setup_security_object(void)
{
  /* Resource 0: Server URI */
  lwm2m_set_string(&LWM2M_OBJ(0, 0, 0), SERVER_URL);

  /* Resource 1: Bootstrap flag false = direct registration */
  lwm2m_set_bool(&LWM2M_OBJ(0, 0, 1), false);

  /* Resource 2: Security mode 3 = NoSec */
  lwm2m_set_u8(&LWM2M_OBJ(0, 0, 2), 3);

  /* Resource 10: Short Server ID */
  lwm2m_set_u16(&LWM2M_OBJ(0, 0, 10), SERVER_ID);
}

static void setup_server_object(void)
{
  /* Resource 0: Short Server ID */
  lwm2m_set_u16(&LWM2M_OBJ(1, 0, 0), SERVER_ID);

  /* Resource 1: Lifetime */
  lwm2m_set_u32(&LWM2M_OBJ(1, 0, 1), LIFETIME_S);

  /* Resource 6: Notification storing while disabled/offline */
  lwm2m_set_bool(&LWM2M_OBJ(1, 0, 6), false);

  /* Resource 7: Binding: "U" = UDP */
  lwm2m_set_string(&LWM2M_OBJ(1, 0, 7), "U");
}

static void setup_device_object(void)
{
  lwm2m_set_res_buf(&LWM2M_OBJ(3, 0, 0), (void *)manufacturer,
                    sizeof(manufacturer), sizeof(manufacturer),
                    LWM2M_RES_DATA_FLAG_RO);
  lwm2m_set_res_buf(&LWM2M_OBJ(3, 0, 1), (void *)model, sizeof(model),
                    sizeof(model), LWM2M_RES_DATA_FLAG_RO);
  lwm2m_set_res_buf(&LWM2M_OBJ(3, 0, 2), (void *)serial, sizeof(serial),
                    sizeof(serial), LWM2M_RES_DATA_FLAG_RO);
  lwm2m_set_res_buf(&LWM2M_OBJ(3, 0, 3), (void *)fw_ver, sizeof(fw_ver),
                    sizeof(fw_ver), LWM2M_RES_DATA_FLAG_RO);

  lwm2m_register_exec_callback(&LWM2M_OBJ(3, 0, 4), device_reboot_cb);
}

static int lwm2m_setup(void)
{
  setup_security_object();
  setup_server_object();
  setup_device_object();
  setup_firmware_object();

  return 0;
}

int main(void)
{
  int ret;

  ret = modem_configure();
  if (ret) {
    LOG_ERR("modem_configure failed: %d", ret);
    return ret;
  }

  LOG_INF("Starting LwM2M client");
  ret = lwm2m_setup();
  if (ret < 0) {
    LOG_ERR("LwM2M setup failed: %d", ret);
    return ret;
  }

  memset(&client_ctx, 0, sizeof(client_ctx));
  lwm2m_rd_client_start(&client_ctx, ENDPOINT_NAME, 0, rd_client_event,
                        observe_cb);

  while (1) {
    k_sleep(K_FOREVER);
  }

  return 0;
}
