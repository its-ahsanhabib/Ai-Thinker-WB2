

#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <lwip/tcpip.h>

#include <bl602_glb.h>

#include <bl_sys.h>
#include <bl_uart.h>
#include <cli.h>

//////////////////////////////////
#include "wifi_interface.h"
#include <../wifi_mgmr.h>
#include "blufi.h"
#include "blufi_api.h"
#include "blufi_hal.h"
#include "blufi_init.h"
#include "axk_blufi.h"
#include "ble_interface.h"
//////////////////////////////////

static int scan_counter;
static bool ble_is_connected = false;
static bool gl_sta_connected = false;
blufi_config_t g_blufi_config = {0};

static void cb_scan_item_parse(wifi_mgmr_ap_item_t *env, uint32_t *param1, wifi_mgmr_ap_item_t *item)
{
    _blufi_ap_record_t *ap_list;

    ap_list = (_blufi_ap_record_t *)env;
    ap_list[scan_counter].rssi = item->rssi;
    memset(ap_list[scan_counter].ssid, 0, sizeof ap_list[scan_counter].ssid);
    memcpy(ap_list[scan_counter].ssid, item->ssid, item->ssid_len);

    scan_counter++;
}

static void cb_scan_complete(void *data, void *param)
{
    _blufi_ap_record_t *ap_list;

    ap_list = (_blufi_ap_record_t *)malloc(WIFI_MGMR_SCAN_ITEMS_MAX * sizeof(_blufi_ap_record_t));
    if (!ap_list)
    {
        printf("ap_list malloc fail");
        return;
    }

    scan_counter = 0;
    wifi_mgmr_scan_ap_all(ap_list, NULL, cb_scan_item_parse);
    printf("scan complete:%ld \r\n", scan_counter);

    if (ble_is_connected == true)
    {
        axk_blufi_send_wifi_list(scan_counter, ap_list);
    }
    else
    {
        printf("BLUFI BLE is not connected yet\n");
    }

    free(ap_list);
}

int wifi_scan_start(void)
{
    return wifi_mgmr_scan(NULL, cb_scan_complete);
}

static void blufi_wifi_event(int event, void *param)
{
    switch (event)
    {

    case BLUFI_STATION_CONNECTED:
        gl_sta_connected = true;
        break;
    case BLUFI_STATION_DISCONNECTED:
        gl_sta_connected = false;
        break;
    case BLUFI_STATION_GOT_IP:
    {
        axk_blufi_extra_info_t info;
        memset(&info, 0, sizeof(axk_blufi_extra_info_t));
        wifi_conn_ap_info_get(&g_blufi_config.wifi.sta);
        memcpy(info.sta_bssid, g_blufi_config.wifi.sta.cwjap_param.bssid, 6);
        info.sta_bssid_set = true;
        info.sta_ssid = (uint8_t *)g_blufi_config.wifi.sta.cwjap_param.ssid;
        info.sta_ssid_len = strlen(g_blufi_config.wifi.sta.cwjap_param.ssid);

        if (ble_is_connected == true)
        {
            axk_blufi_send_wifi_conn_report(g_blufi_config.wifi.cwmode, _BLUFI_STA_CONN_SUCCESS, 0, &info);
        }
        else
        {
            printf("BLUFI BLE is not connected yet\r\n");
        }

        printf("BLUFI save ssid&&pwd \r\n");
        g_blufi_config.wifi.cwmode = WIFIMODE_STA;
    }
    break;
    default:
        break;
    }
}

static void example_event_callback(_blufi_cb_event_t event, _blufi_cb_param_t *param)
{
    /* actually, should post to blufi_task handle the procedure,
     * now, as a example, we do it more simply */
    switch (event)
    {
    case AXK_BLUFI_EVENT_INIT_FINISH:
        printf("BLUFI init finish\n");

        axk_blufi_adv_start();
        break;
    case AXK_BLUFI_EVENT_DEINIT_FINISH:
        printf("BLUFI deinit finish\n");
        break;
    case AXK_BLUFI_EVENT_BLE_CONNECT:
        printf("BLUFI ble connect\n");
        ble_is_connected = true;
        axk_blufi_adv_stop();
        // blufi_security_init();
        break;
    case AXK_BLUFI_EVENT_BLE_DISCONNECT:
        printf("BLUFI ble disconnect\n");
        ble_is_connected = false;
        // blufi_security_deinit();
        axk_blufi_adv_start();
        break;
    case AXK_BLUFI_EVENT_SET_WIFI_OPMODE:
        printf("BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
        // if (axk_hal_wifi_mode_set(WIFIMODE_STA, 0) != BLUFI_ERR_SUCCESS)
        // {
        //     printf("BLUFI axk_hal_wifi_mode_set fail\r\n");
        //     break;
        // }
        g_blufi_config.wifi.cwmode = WIFIMODE_STA;
        break;
    case AXK_BLUFI_EVENT_REQ_CONNECT_TO_AP:
    {
        cwjap_param_t cwjap_param = {0};
        printf("BLUFI requset wifi connect to AP\n");
        cwjap_param = g_blufi_config.wifi.sta.cwjap_param;
        if (axk_hal_conn_ap_info_set(&cwjap_param) != BLUFI_ERR_SUCCESS)
        {
            printf("BLUFI axk_hal_conn_ap_info_set fail\r\n");
            break;
        }
        g_blufi_config.wifi.sta.state = BLUFI_WIFI_STATE_CONNECTING;
    }

    break;
    case AXK_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        printf("BLUFI requset wifi disconnect from AP\n");
        axk_hal_disconn_ap();
        break;
    case AXK_BLUFI_EVENT_REPORT_ERROR:
        printf("BLUFI report error, error code %d\n", param->report_error.state);
        axk_blufi_send_error_info(param->report_error.state);
        break;
    case AXK_BLUFI_EVENT_GET_WIFI_STATUS:
    {
        wifi_mode_t mode;
        mode = g_blufi_config.wifi.cwmode;

        if (gl_sta_connected)
        {
            axk_blufi_extra_info_t info;
            memset(&info, 0, sizeof(axk_blufi_extra_info_t));
            wifi_conn_ap_info_get(&g_blufi_config.wifi.sta);
            memcpy(info.sta_bssid, g_blufi_config.wifi.sta.cwjap_param.bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = (uint8_t *)g_blufi_config.wifi.sta.cwjap_param.ssid;
            info.sta_ssid_len = strlen(g_blufi_config.wifi.sta.cwjap_param.ssid);
            axk_blufi_send_wifi_conn_report(mode, _BLUFI_STA_CONN_SUCCESS, 0, &info);
        }
        else
        {
            axk_blufi_send_wifi_conn_report(mode, _BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        printf("BLUFI get wifi status from AP\n");

        break;
    }
    case AXK_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        printf("blufi close a gatt connection\r\n");
        axk_blufi_disconnect();
        break;
    case AXK_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
    case AXK_BLUFI_EVENT_RECV_STA_BSSID:
        memset(g_blufi_config.wifi.sta.cwjap_param.bssid, 0, 6);
        memcpy(g_blufi_config.wifi.sta.cwjap_param.bssid, param->sta_bssid.bssid, 6);
        // sta_config.sta.bssid_set = 1;
        // esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        printf("Recv STA BSSID %s\r\n", param->sta_bssid.bssid);
        break;
    case AXK_BLUFI_EVENT_RECV_STA_SSID:
        memset(g_blufi_config.wifi.sta.cwjap_param.ssid, 0, 33);
        strncpy(g_blufi_config.wifi.sta.cwjap_param.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);

        printf("Recv STA SSID %s\r\n", (char *)g_blufi_config.wifi.sta.cwjap_param.ssid);
        break;
    case AXK_BLUFI_EVENT_RECV_STA_PASSWD:
        memset(g_blufi_config.wifi.sta.cwjap_param.pwd, 0, 64);
        strncpy(g_blufi_config.wifi.sta.cwjap_param.pwd, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        printf("Recv STA PASSWORD %s\r\n", (char *)g_blufi_config.wifi.sta.cwjap_param.pwd);
        break;
    case AXK_BLUFI_EVENT_RECV_SOFTAP_SSID:
        break;
    case AXK_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        break;
    case AXK_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        break;
    case AXK_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        break;
    case AXK_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        break;
    case AXK_BLUFI_EVENT_GET_WIFI_LIST:
        wifi_scan_start();
        break;
    case AXK_BLUFI_EVENT_RECV_CUSTOM_DATA:
        printf("Recv Custom Data len:%d\r\n", param->custom_data.data_len);
        printf("Custom Data:%.*s\n", param->custom_data.data_len, param->custom_data.data);
        // echo
        axk_blufi_send_custom_data(param->custom_data.data, param->custom_data.data_len);
        break;
    case AXK_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
    case AXK_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
    case AXK_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
    case AXK_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
    case AXK_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;
        ;
    case AXK_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}

static _blufi_callbacks_t example_callbacks = {
    .event_cb = example_event_callback,
    // .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    // .encrypt_func = blufi_aes_encrypt,
    // .decrypt_func = blufi_aes_decrypt,
    // .checksum_func = blufi_crc_checksum,
};

int at_blufi_start(void)
{
    int ret = -1;
    axk_hal_blufi_init();

    ret = _blufi_host_and_cb_init(&example_callbacks);
    if (ret)
    {
        printf("%s initialise failed: %d\n", __func__, ret);
    }
    return ret;
}

static void cmd_blufi_init(char *buf, int len, int argc, char **argv)
{
    at_blufi_start();
}

static void cmd_blufi_deinit(char *buf, int len, int argc, char **argv)
{
    axk_blufi_profile_deinit();
    axk_hal_blufi_deinit();
    axk_blufi_adv_stop();
    axk_hal_ble_role_set(BLE_ROLE_DEINIT);
}

const static struct cli_command cmds_user[] STATIC_CLI_CMD_ATTRIBUTE = {
    {"blufi_init", "blufi init", cmd_blufi_init},
    {"blufi_deinit", "blufi deinit", cmd_blufi_deinit},
};

void free_task(void *param)
{
    uint8_t pcWriteBuffer[500];
    while (1)
    {
        printf("=================================================\r\n");
        printf("\r\nremaining memory = %ld,minimum memory = %ld\r\n", xPortGetFreeHeapSize(), xPortGetMinimumEverFreeHeapSize());
        vTaskDelay(3 * 1000);
    }
    vTaskDelete(NULL);
}

static void proc_main_entry(void *pvParameters)
{
    wifi_interface_init(blufi_wifi_event);
    // at_blufi_start();
    vTaskDelete(NULL);
}

void main()
{
    static TaskHandle_t proc_main_task;
    bl_sys_init();
    xTaskCreate(proc_main_entry, (char *)"main_entry", 1024, NULL, 15, &proc_main_task);
    xTaskCreate(free_task, (char *)"free_task", 750, NULL, 1, NULL);
    tcpip_init(NULL, NULL);
    printf("blufi demo test\r\n");
}
