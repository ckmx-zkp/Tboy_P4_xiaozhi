"""修补 managed_components 里的 usb_host_uvc。

该目录不进 git。组件重新下载后，CMake 会再跑本脚本。
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "managed_components" / "espressif__usb_host_uvc"
PATH = ROOT / "uvc_host.c"
BULK_PATH = ROOT / "uvc_bulk.c"
MARKER = "uvc-open-fail-idempotent"
BULK_MARKER = "uvc-bulk-frame-id"
OLD = """static void uvc_transfers_free(uvc_stream_t *uvc_stream)
{
    assert(uvc_stream);
    for (unsigned i = 0; i < uvc_stream->constant.num_of_xfers; i++) {
        usb_host_transfer_free(uvc_stream->constant.xfers[i]);
    }
    free(uvc_stream->constant.xfers);
}"""
NEW = """static void uvc_transfers_free(uvc_stream_t *uvc_stream)
{
    assert(uvc_stream);
    /* uvc-open-fail-idempotent: allocate 失败时本函数会被调用两次 */
    if (uvc_stream->constant.xfers == NULL) {
        uvc_stream->constant.num_of_xfers = 0;
        return;
    }
    for (unsigned i = 0; i < uvc_stream->constant.num_of_xfers; i++) {
        usb_host_transfer_free(uvc_stream->constant.xfers[i]);
        uvc_stream->constant.xfers[i] = NULL;
    }
    free(uvc_stream->constant.xfers);
    uvc_stream->constant.xfers = NULL;
    uvc_stream->constant.num_of_xfers = 0;
}"""


BULK_OLD = """        if (payload_header->bmHeaderInfo.end_of_frame) {
            assert(payload_header->bmHeaderInfo.frame_id == uvc_stream->single_thread.current_frame_id);
            if (payload_header->bmHeaderInfo.error) {
                uvc_stream->single_thread.skip_current_frame = true;
            }"""
BULK_NEW = """        /* uvc-bulk-frame-id: 便宜摄像头会在丢同步后给出另一帧的 EOF，不能断言重启 */
        const bool header_ok = transfer->actual_num_bytes >= sizeof(uvc_payload_header_t);
        if (!header_ok) {
            uvc_stream->single_thread.skip_current_frame = true;
            break;
        }

        if (payload_header->bmHeaderInfo.end_of_frame) {
            if (payload_header->bmHeaderInfo.frame_id != uvc_stream->single_thread.current_frame_id ||
                payload_header->bmHeaderInfo.error) {
                ESP_LOGW(TAG, "bulk EOF drop len=%u frame_id=%u current=%u err=%u",
                         (unsigned)transfer->actual_num_bytes,
                         payload_header->bmHeaderInfo.frame_id,
                         uvc_stream->single_thread.current_frame_id,
                         payload_header->bmHeaderInfo.error);
                uvc_stream->single_thread.skip_current_frame = true;
            }"""


def patch_file(path: Path, marker: str, old: str, new: str) -> None:
    if not path.exists():
        return
    text = path.read_text(encoding="utf-8")
    if marker in text:
        return
    if old not in text:
        raise SystemExit(f"usb_host_uvc patch context missing: {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
    print(f"patched {path}")


# 控制传输超时不是完成：保留设备引用，等待迟到/拔线取消回调后再关闭。
CTRL_STATE_OLD = "static uvc_host_driver_t *p_uvc_host_driver = NULL;"
CTRL_STATE_NEW = CTRL_STATE_OLD + """
/* uvc-ctrl-timeout-lifetime: 不复用仍在飞行的 EP0 请求 */
static bool ctrl_pending = false;
static usb_device_handle_t deferred_ctrl_close = NULL;
"""
CTRL_CLOSE_OLD = "    usb_host_device_close(p_uvc_host_driver->usb_client_hdl, uvc_stream->constant.dev_hdl); // Gracefully continue on error"
CTRL_CLOSE_NEW = """    UVC_ENTER_CRITICAL();
    const bool defer_close = ctrl_pending &&
        p_uvc_host_driver->ctrl_transfer->device_handle == uvc_stream->constant.dev_hdl;
    if (defer_close) deferred_ctrl_close = uvc_stream->constant.dev_hdl;
    UVC_EXIT_CRITICAL();
    if (defer_close) {
        ESP_LOGW(TAG, "CTRL still pending: defer device close until completion/unplug");
    } else {
        usb_host_device_close(p_uvc_host_driver->usb_client_hdl, uvc_stream->constant.dev_hdl);
    }"""
CTRL_CB_OLD = "    xSemaphoreGive((SemaphoreHandle_t)transfer->context);"
CTRL_CB_NEW = """    /* uvc-ctrl-callback-signal-only: 回调只通知，关闭交给事件任务 */
    xSemaphoreGive((SemaphoreHandle_t)transfer->context);
    UVC_ENTER_CRITICAL();
    ctrl_pending = false;
    UVC_EXIT_CRITICAL();"""
CTRL_SETUP_OLD = "    usb_setup_packet_t *req = (usb_setup_packet_t *)(p_uvc_host_driver->ctrl_transfer->data_buffer);"
CTRL_SETUP_NEW = """    if (UVC_ATOMIC_LOAD(ctrl_pending) || UVC_ATOMIC_LOAD(deferred_ctrl_close) != NULL) {
        xSemaphoreGive(p_uvc_host_driver->ctrl_mutex);
        return ESP_ERR_TIMEOUT;
    }
    // 丢弃上一笔超时请求的迟到通知，禁止误认成下一笔完成。
    xSemaphoreTake((SemaphoreHandle_t)p_uvc_host_driver->ctrl_transfer->context, 0);
""" + CTRL_SETUP_OLD
CTRL_SUBMIT_OLD = """    ESP_GOTO_ON_ERROR(
        usb_host_transfer_submit_control(p_uvc_host_driver->usb_client_hdl, p_uvc_host_driver->ctrl_transfer),
        unblock, TAG, "CTRL transfer failed");"""
CTRL_SUBMIT_NEW = """    ESP_LOGI(TAG, "CTRL req=%02x value=%04x index=%u len=%u", bRequest, wValue, wIndex, wLength);
    UVC_ENTER_CRITICAL();
    ctrl_pending = true;
    UVC_EXIT_CRITICAL();
    ret = usb_host_transfer_submit_control(p_uvc_host_driver->usb_client_hdl, p_uvc_host_driver->ctrl_transfer);
    if (ret != ESP_OK) {
        UVC_ENTER_CRITICAL();
        ctrl_pending = false;
        UVC_EXIT_CRITICAL();
        goto unblock;
    }"""


PROBE_OLD = """            if (usb_host_device_open(p_uvc_host_driver->usb_client_hdl, dev_addr_list[i], &current_device) != ESP_OK) {
                continue; // In case we failed to open this device, continue with next one in the list
            }"""
PROBE_NEW = """            /* uvc-open-probe: 记录每次打开动作的首轮结果，避免静默掩盖设备占用 */
            esp_err_t probe_err = usb_host_device_open(p_uvc_host_driver->usb_client_hdl, dev_addr_list[i], &current_device);
            if (timeout == timeout_ticks) {
                ESP_LOGI(TAG, "probe USB addr=%u open=%s", dev_addr_list[i], esp_err_to_name(probe_err));
            }
            if (probe_err != ESP_OK) {
                continue;
            }"""


DEFERRED_OLD = "    esp_err_t ret = usb_host_client_handle_events(uvc_obj->usb_client_hdl, timeout);"
DEFERRED_NEW = DEFERRED_OLD + """
    /* uvc-ctrl-deferred-event: USBH 完成事件计数扣减后再释放最后一个引用 */
    if (!UVC_ATOMIC_LOAD(ctrl_pending) && UVC_ATOMIC_LOAD(deferred_ctrl_close) != NULL) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (xSemaphoreTake(uvc_obj->open_close_mutex, 0) == pdTRUE) {
            UVC_ENTER_CRITICAL();
            usb_device_handle_t deferred = deferred_ctrl_close;
            deferred_ctrl_close = NULL;
            UVC_EXIT_CRITICAL();
            if (deferred) usb_host_device_close(uvc_obj->usb_client_hdl, deferred);
            xSemaphoreGive(uvc_obj->open_close_mutex);
        }
    }
"""


def main() -> None:
    patch_file(PATH, MARKER, OLD, NEW)
    patch_file(BULK_PATH, BULK_MARKER, BULK_OLD, BULK_NEW)
    patch_file(PATH, "uvc-ctrl-timeout-lifetime", CTRL_STATE_OLD, CTRL_STATE_NEW)
    patch_file(PATH, "const bool defer_close", CTRL_CLOSE_OLD, CTRL_CLOSE_NEW)
    patch_file(PATH, "uvc-ctrl-callback-signal-only", CTRL_CB_OLD, CTRL_CB_NEW)
    patch_file(PATH, "迟到通知", CTRL_SETUP_OLD, CTRL_SETUP_NEW)
    patch_file(PATH, "ctrl_pending = true;", CTRL_SUBMIT_OLD, CTRL_SUBMIT_NEW)
    patch_file(PATH, "uvc-open-probe", PROBE_OLD, PROBE_NEW)
    patch_file(PATH, "uvc-ctrl-deferred-event", DEFERRED_OLD, DEFERRED_NEW)


if __name__ == "__main__":
    main()
