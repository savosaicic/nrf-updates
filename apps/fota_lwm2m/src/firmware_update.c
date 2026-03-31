#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/lwm2m.h>
#include <zephyr/sys/reboot.h>

#include <net/lwm2m_client_utils.h>

LOG_MODULE_REGISTER(firmware_update);

static int fota_event_cb(struct lwm2m_fota_event *event)
{
  switch (event->id) {
  case LWM2M_FOTA_DOWNLOAD_START:
    LOG_INF("Download started, instance %d", event->download_start.obj_inst_id);
    break;
  case LWM2M_FOTA_DOWNLOAD_FINISHED:
    LOG_INF("Download done, DFU type: %d", event->download_ready.dfu_type);
    break;
  case LWM2M_FOTA_UPDATE_IMAGE_REQ:
    /* Return 0 to accept, negative to defer */
    LOG_INF("Update requested, instance %d", event->update_req.obj_inst_id);
    return 0;
  case LWM2M_FOTA_UPDATE_MODEM_RECONNECT_REQ:
    /* TODO: Modem update requires LTE reconnection
     * handle it here
     */
    break;
  case LWM2M_FOTA_UPDATE_ERROR:
    LOG_ERR("Update failed on instance %d, code %d", event->failure.obj_inst_id,
            event->failure.update_failure);
    break;
  }
  return 0;
}

int setup_firmware_object(void)
{
  int ret;

  lwm2m_init_firmware_cb(fota_event_cb);

  /* Call once at boot to confirm MCUboot image and sync result to server */
  ret = lwm2m_init_image();
  if (ret < 0) {
    LOG_ERR("Failed to setup image properties: %d", ret);
    return ret;
  }

  return 0;
}
