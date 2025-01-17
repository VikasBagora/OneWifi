/************************************************************************************
  If not stated otherwise in this file or this component's LICENSE file the  
  following copyright and licenses apply:
  
  Copyright 2018 RDK Management

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at
  
  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 **************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include "stdlib.h"
#include <sys/time.h>
#include <assert.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "const.h"
#include <unistd.h>
#include "vap_svc.h"
#include "wifi_ctrl.h"
#include "wifi_mgr.h"
#include "wifi_hal_rdk_framework.h"
#include "wifi_supp.h"

#ifdef LOG_NOT_WORKING_SO_PRINT
#define wifi_util_dbg_print(module, format, ...)  printf(format, ##__VA_ARGS__)
#define wifi_util_info_print(module, format, ...) printf(format, ##__VA_ARGS__)
#endif

static int get_dwell_time()
{
    FILE *fp = NULL;
    int dwell_time = DEFAULT_DWELL_TIME_MS;
    if (access(DWELL_TIME_PATH, R_OK) == 0) {
        fp = fopen(DWELL_TIME_PATH, "r");
        if (fp == NULL) {
            return dwell_time;
        }
        fscanf(fp, "%d", &dwell_time);
    }
    return dwell_time;
}

bool vap_svc_is_sta(unsigned int vap_index)
{
    //printf("In vap_svc_is_sta(): isVapSTAMesh(vap_index):%d\n", isVapSTAMesh(vap_index) ? true : false);

    return isVapSTAMesh(vap_index) ? true : false;
}


void sta_cancel_all_timers(vap_svc_t *svc)
{
    vap_svc_sta_t *sta;
    wifi_ctrl_t *ctrl;

    ctrl = svc->ctrl;
    sta = &svc->u.sta;

    wifi_util_dbg_print(WIFI_SUPP, "%s:%d - cancel all started timer\r\n", __func__, __LINE__);
    if (sta->sta_connect_timer_id != 0) {
        wifi_util_dbg_print(WIFI_SUPP, "%s:%d - cancel sta_connect_timer_id\r\n", __func__, __LINE__);
        scheduler_cancel_timer_task(ctrl->sched, sta->sta_connect_timer_id);
        sta->sta_connect_timer_id = 0;
    }

    if (sta->periodic_scan_timer_id != 0) {
        wifi_util_dbg_print(WIFI_SUPP, "%s:%d - cancel periodic_scan_timer_id\r\n", __func__, __LINE__);
        scheduler_cancel_timer_task(ctrl->sched, sta->periodic_scan_timer_id);
        sta->periodic_scan_timer_id = 0;
    }
}

static char *sta_state_to_str(sta_state_t sta_state)
{
    switch (sta_state) {
    case sta_state_init:
        return "sta_state_init";
    case sta_state_init_done:
        return "sta_state_init_done";
    case sta_state_scan_done:
        return "sta_state_scan_done";
    case sta_state_connecting:
        return "sta_state_connecting";
    case sta_state_connected:
        return "sta_state_connected";
    case sta_state_stop:
        return "sta_state_stop";
    default:
        break;
    }

    return "udefined state";
}

static void sta_set_sta_state(vap_svc_sta_t *sta, sta_state_t state, const char *func, int line)
{
    wifi_util_info_print(WIFI_SUPP, "%s:%d change connection state: %s -> %s\r\n", func, line,
        sta_state_to_str(sta->conn_state), sta_state_to_str(state));

    sta->conn_state = state;

    switch (state) {
        case sta_state_init_done:
            wifi_util_info_print(WIFI_SUPP, "%s:%d STA service started by %s:%d\n", __func__, __LINE__, func, line);
	    break;
	case sta_state_stop:
            wifi_util_info_print(WIFI_SUPP, "%s:%d STA service stop by %s:%d\n", __func__, __LINE__, func, line);
	    break;
	case sta_state_scan_done:
            wifi_util_info_print(WIFI_SUPP, "%s:%d STA service scan:%d done by %s:%d\n", __func__,
                __LINE__, sta->scan_retry_cnt, func, line);
	    break;
	case sta_state_connecting:
            wifi_util_info_print(WIFI_SUPP, "%s:%d STA service sta connecting by %s:%d\n", __func__,
                __LINE__, func, line);
	    break;
	case sta_state_connected:
            wifi_util_info_print(WIFI_SUPP, "%s:%d STA service sta connect done by %s:%d\n", __func__,
                __LINE__, func, line);
	    break;
        default:
            wifi_util_error_print(WIFI_SUPP, "%s:%d invalid sta state by %s:%d\n", __func__, __LINE__, func, line);
	    break;
    }
}

int sta_connect(int vap_index, wifi_bss_info_t *p_external_ap)
{
    wifi_ctrl_t *ctrl = (wifi_ctrl_t *)get_wifictrl_obj();
    vap_svc_t *svc = get_svc_by_type(ctrl, vap_svc_type_sta);
    vap_svc_sta_t *sta = &svc->u.sta;
    mac_addr_str_t bssid_str;
    int ret;
    bss_candidate_t *bss_info = &sta->try_connect_with_bss;

    wifi_util_dbg_print(WIFI_SUPP, "%s:%d: wifi_hal_connect() for vap index:%d, count:%d, bssid:%s\n",
            __func__, __LINE__, vap_index, bss_info->conn_retry_attempt, to_mac_str(p_external_ap->bssid, bssid_str));

    if (bss_info->conn_retry_attempt >= STA_CONNECT_FAIL_THRESHOLD) {
        wifi_util_error_print(WIFI_SUPP, "%s:%d not received connection event, reset radios\n",
            __func__, __LINE__);
        //reset_wifi_radios();
        bss_info->conn_retry_attempt = 0;

	/* reset the state machine to accept connect all again */
        sta_set_sta_state(sta, sta_state_scan_done, __func__, __LINE__);
    }

    bss_info->conn_retry_attempt++;

    ret = wifi_hal_connect(vap_index, p_external_ap);
    if (ret == RETURN_ERR) {
        wifi_util_error_print(WIFI_SUPP, "%s:%d sta connect failed for vap index: %d\n",
            __func__, __LINE__, vap_index);
    }

    sta_set_sta_state(sta, sta_state_connecting, __func__, __LINE__);

    scheduler_add_timer_task(ctrl->sched, FALSE, &sta->sta_connect_timer_id,
                process_sta_connect_algorithm, svc,
                STA_CONN_HAL_EVENT_TIMEOUT, 1, FALSE);

    memcpy(&bss_info->external_ap, p_external_ap, sizeof(wifi_bss_info_t));
    bss_info->vap_index = vap_index;

    return ret;
}

int periodic_scan_timer_handler(vap_svc_t *svc)
{
    vap_svc_sta_t *sta;
    wifi_ctrl_t *ctrl;

    ctrl = svc->ctrl;
    sta = &svc->u.sta;

    sta->periodic_scan_timer_id = 0;

    //wifi_util_dbg_print(WIFI_SUPP,"%s:%d SCAN_TIMER handler. conn_state:%d(%s), re-scan\n", __func__, __LINE__,
      //  sta->conn_state, sta_state_to_str(sta->conn_state));

    /* Timer has handled gracefully.
     * Hence, restart scan timer again as part of sta_start_scan
     */
    sta_start_scan(svc);

    return 0;
}

int sta_start_scan(vap_svc_t *svc)
{
    vap_svc_sta_t *sta;
    wifi_ctrl_t *ctrl;
    signed int radio_index;
    signed int dwell_time = get_dwell_time();
    signed int num_of_radio = getNumberRadios();
    signed int num_channels;
    signed int ret = SCAN_ERR;
    wifi_channels_list_t channels;
    wifi_radio_operationParam_t *radio_oper_param = NULL;
    wifi_mgr_t *mgr = (wifi_mgr_t *)get_wifimgr_obj();
    INT channels_list[MAX_CHANNELS];

    ctrl = svc->ctrl;
    sta = &svc->u.sta;

    if (sta == NULL || ctrl == NULL) {
        wifi_util_error_print(WIFI_SUPP, "%s:%d: CTRL:%p, STA:%p is NULL.\n", __func__, __LINE__, ctrl, sta);
        return ret;
    }

    if (sta->scan_retry_cnt >= STA_SCAN_FAIL_THRESHOLD) {
        printf("%s:%d START_SCAN: conn_state:%d(%s), re-scan\n", __func__, __LINE__, sta->conn_state, sta_state_to_str(sta->conn_state));

        /* flag the error, stop scanning as fail threashold limit reached */
        wifi_util_error_print(WIFI_SUPP, "%s:%d wifi_hal_startScan() failed for %d times. Stopping scan timer. conn_state:%d(%s)\n",
            __func__, __LINE__, sta->scan_retry_cnt, sta->conn_state, sta_state_to_str(sta->conn_state));

        scheduler_cancel_timer_task(ctrl->sched, sta->periodic_scan_timer_id);

        return ret;
    }

    for (radio_index = 0; radio_index < num_of_radio; radio_index++) {
        radio_oper_param = get_wifidb_radio_map(radio_index);
        if (get_allowed_channels(radio_oper_param->band, &mgr->hal_cap.wifi_prop.radiocap[radio_index],
                channels_list, &num_channels,
                radio_oper_param->DfsEnabled) != RETURN_OK) {
            continue;
        }
        (void)memcpy(channels.channels_list, channels_list, sizeof(*channels_list) * num_channels);
        channels.num_channels = num_channels;

        wifi_util_dbg_print(WIFI_SUPP,"%s:%d START_SCAN: radio:%d, num of channels:%d, scan_retry_cnt:%d\n",
            __func__, __LINE__, radio_index, channels.num_channels, sta->scan_retry_cnt);
	 
        ret = wifi_hal_startScan(radio_index, WIFI_RADIO_SCAN_MODE_OFFCHAN, dwell_time, channels.num_channels, channels.channels_list);
        if (ret != RETURN_OK) {
            /* Increment scan retry counter for every scan failure */
            sta->scan_retry_cnt++;
            wifi_util_error_print(WIFI_SUPP, "%s:%d wifi_hal_startScan() failed for radio:%d, scan_retry_cnt:%d,"
                " num of channels:%d\n", __func__, __LINE__, radio_index, sta->scan_retry_cnt, channels.num_channels);
	} else {
            /* wifi_hal_startScan() api is success, reset scan retry counter */
            sta->scan_retry_cnt = 0;
            ret = SCAN_SUCCESS;	
        }

        sta->periodic_scan_timer_id = 0;

	if (sta->conn_state == sta_state_connected) {
            /* If client is connected, double-up the scanning frequency */
            scheduler_add_timer_task(ctrl->sched, FALSE, &sta->periodic_scan_timer_id,
                periodic_scan_timer_handler, svc, STA_PERIODIC_SCAN_TIMER_CONNECTED, 1, FALSE);
	} else {
            /* If client is not connected, scan period is set to configured frequency */
            scheduler_add_timer_task(ctrl->sched, FALSE, &sta->periodic_scan_timer_id,
                periodic_scan_timer_handler, svc, STA_PERIODIC_SCAN_TIMER, 1, FALSE);
	}
    }
    return ret;
}

int sta_try_connecting(vap_svc_t *svc)
{
    vap_svc_sta_t *sta = &svc->u.sta;
    bss_candidate_t *bss_info = &sta->try_connect_with_bss;

    return sta_connect(bss_info->vap_index, &bss_info->external_ap);
}

int process_sta_connect_algorithm(vap_svc_t *svc)
{
    vap_svc_sta_t   *sta;
    wifi_ctrl_t *ctrl = svc->ctrl;
    sta = &svc->u.sta;

    wifi_util_dbg_print(WIFI_SUPP, "%s:%d process connection state: %s\r\n", __func__, __LINE__,
        sta_state_to_str(sta->conn_state));

    sta->sta_connect_timer_id = 0;

    switch (sta->conn_state) {
        case sta_state_connecting: {
            sta_try_connecting(svc);
            break;
        }
        case sta_state_connected: {
            scheduler_cancel_timer_task(ctrl->sched, sta->periodic_scan_timer_id);
            wifi_util_info_print(WIFI_SUPP, "%s:%d sta connected with ap\r\n", __func__, __LINE__);
            break;
        }
    }

    return 0;
}

int vap_svc_sta_start(vap_svc_t *svc, unsigned int radio_index, wifi_vap_info_map_t *map)
{
    vap_svc_sta_t *sta = &svc->u.sta;

    if (!sta) {
        wifi_util_error_print(WIFI_SUPP, "%s:%d STA service failed to start as STA is NULL\n", __func__, __LINE__);
        return RETURN_ERR;
    }

    if (sta->conn_state >= sta_state_init_done) {
        wifi_util_info_print(WIFI_SUPP, "%s:%d STA service already started\n", __func__, __LINE__);
        return RETURN_OK;
    }

    /* set the initial 'sta' state to begin with */
    sta_set_sta_state(sta, sta_state_init, __func__, __LINE__);

    /* set to zero for 'sta' specific structures */
    memset(sta, 0, sizeof(vap_svc_sta_t));

    /* create STA vap's and install acl filters */
    vap_svc_start(svc);

    /* TEMP HACK: restart NM service to avoid trigger of manual scan */ 
    system("sudo systemctl restart NetworkManager");

    rdkb_wifi_dbus_init(&sta->p_wifi_supp_info);

    sta_set_sta_state(sta, sta_state_init_done, __func__, __LINE__);

    return RETURN_OK;
}

int vap_svc_sta_stop(vap_svc_t *svc, unsigned int radio_index, wifi_vap_info_map_t *map)
{
    vap_svc_sta_t *sta = &svc->u.sta;

    wifi_util_info_print(WIFI_SUPP, "%s:%d sta service stop\n", __func__, __LINE__);

    sta_set_sta_state(sta, sta_state_stop, __func__, __LINE__);
    
    sta_cancel_all_timers(svc);

    vap_svc_stop(svc);

    return 0;
}

int process_sta_scan_results(vap_svc_t *svc, void *arg)
{
    wifi_bss_info_t *bss;
    wifi_bss_info_t *tmp_bss;
    signed int i, num = 0;
    static signed int scan_done = false;
    unsigned int band = 0;
    scan_results_t *results;
    mac_addr_str_t bssid_str;
    vap_svc_sta_t *sta;
    wifi_ctrl_t *ctrl;
    rdkb_wifi_bss_t bss_nm;

    ctrl = svc->ctrl;
    sta = &svc->u.sta;

    results = (scan_results_t *)arg;
    bss = results->bss;
    num = results->num;
    tmp_bss = bss;

    wifi_util_dbg_print(WIFI_SUPP,"%s:%d STA Mode radio:%u, num of scan results:%d, connection state:%s\n\n",
        __func__, __LINE__, results->radio_index, num, sta_state_to_str(sta->conn_state));

    for (i = 0; i < num; i++) {
	//printf("\n=========================>%s:%d: AP with ssid:%s, bssid:%s, rssi:%d, freq:%d\n",
          //  __func__, __LINE__, tmp_bss->ssid, to_mac_str(tmp_bss->bssid, bssid_str), tmp_bss->rssi, tmp_bss->freq);

        wifi_util_dbg_print(WIFI_SUPP, "%s:%d: AP with ssid:%s, bssid:%s, rssi:%d, freq:%d\n",
            __func__, __LINE__, tmp_bss->ssid, to_mac_str(tmp_bss->bssid, bssid_str), tmp_bss->rssi, tmp_bss->freq);

        copy_scan_result_to_bss_nm(&bss_nm, tmp_bss);
        if (sta->p_wifi_supp_info != NULL) {
            update_wifi_scan_results(&sta->p_wifi_supp_info->supp_info, &bss_nm);
        }

        tmp_bss++;
    }

    if (scan_done == false ) {
        /* Set once for sta as logical state */
        sta_set_sta_state(sta, sta_state_scan_done, __func__, __LINE__);
        scan_done = true;
    }
 
    /* Set true once scan result is updated at-least once */
    sta->last_scan_done = 1;
    sta->scan_retry_cnt = 0;

    return 0;
}

int process_sta_conn_status(vap_svc_t *svc, void *arg)
{
    uint32_t index;
    rdk_sta_data_t *sta_data = (rdk_sta_data_t *)arg;
    vap_svc_sta_t *sta;
    wifi_ctrl_t *ctrl;
    int radio_freq_band = 0;
    mac_addr_str_t bssid_str = { 0 };

    ctrl = svc->ctrl;
    sta = &svc->u.sta;

    printf("==>%s:%d: Station is connected. STA connection state:%d\n", __func__, __LINE__, sta_data->stats.connect_status);
    wifi_util_info_print(WIFI_SUPP, "%s:%d: Sta connection state:%d\n", __func__, __LINE__, sta_data->stats.connect_status);

    if (sta_data->stats.connect_status == wifi_connection_status_connected) {
        sta_set_sta_state(sta, sta_state_connected, __func__, __LINE__);

        if (sta->sta_connect_timer_id != 0) {
            wifi_util_dbg_print(WIFI_SUPP, "%s:%d - cancel sta_connect_timer_id timer\r\n", __func__, __LINE__);
            scheduler_cancel_timer_task(ctrl->sched, sta->sta_connect_timer_id);
            sta->sta_connect_timer_id = 0;
        }

        // copy the bss info to lcb
        memset(&sta->last_connected_bss, 0, sizeof(bss_candidate_t));
        memcpy(&sta->last_connected_bss.external_ap, &sta_data->bss_info, sizeof(wifi_bss_info_t));
        sta->last_connected_bss.vap_index = sta_data->stats.vap_index;
        sta->connected_vap_index = sta_data->stats.vap_index;

        index = get_radio_index_for_vap_index(svc->prop, sta_data->stats.vap_index);
        convert_radio_index_to_freq_band(svc->prop, index, &radio_freq_band);
        sta->last_connected_bss.radio_freq_band = (wifi_freq_bands_t)radio_freq_band;
        wifi_util_dbg_print(WIFI_SUPP,"%s:%d - [%s] connected radio_band:%d\r\n", __func__,
            __LINE__, to_mac_str(sta_data->bss_info.bssid, bssid_str), sta->last_connected_bss.radio_freq_band);
    }

    return 0;
}

int process_sta_channel_change(vap_svc_t *svc, void *arg)
{
    if ((svc == NULL) || (arg == NULL)) {
        wifi_util_dbg_print(WIFI_SUPP, "%s:%d: NULL pointers \n", __func__, __LINE__);
        return 0;
    }
    
    wifi_util_dbg_print(WIFI_SUPP, "%s:%d: Channel change event.\r\n", __func__, __LINE__);
    return 0;
}

int process_sta_hal_ind(vap_svc_t *svc, wifi_event_subtype_t sub_type, void *arg)
{
    switch (sub_type) {
    case wifi_event_scan_results:
        process_sta_scan_results(svc, arg);
        break;

    case wifi_event_hal_sta_conn_status:
        process_sta_conn_status(svc, arg);
        break;

    case wifi_event_hal_channel_change:
        process_sta_channel_change(svc, arg);
        break;

    default:
        wifi_util_dbg_print(WIFI_SUPP, "%s:%d: assert - sub_type:%s\r\n", __func__, __LINE__,
            wifi_event_subtype_to_string(sub_type));
        assert(sub_type >= wifi_event_hal_max);
        break;
    }

    return 0;
}

int vap_svc_sta_event(vap_svc_t *svc, wifi_event_type_t type, wifi_event_subtype_t sub_type,
    vap_svc_event_t event, void *arg)
{
    switch (type) {
    case wifi_event_type_hal_ind:
        process_sta_hal_ind(svc, sub_type, arg);
        break;

    default:
        wifi_util_dbg_print(WIFI_SUPP, "%s:%d: default - sub_type:%s\r\n", __func__, __LINE__,
            wifi_event_subtype_to_string(sub_type));
        break;
    }

    return 0;
}

int vap_svc_sta_update(vap_svc_t *svc, unsigned int radio_index, wifi_vap_info_map_t *map,
    rdk_wifi_vap_info_t *rdk_vap_info)
{
    unsigned int i;
    wifi_vap_info_map_t tgt_vap_map;

    for (i = 0; i < map->num_vaps; i++) {
        memset((unsigned char *)&tgt_vap_map, 0, sizeof(tgt_vap_map));
        memcpy((unsigned char *)&tgt_vap_map.vap_array[0], (unsigned char *)&map->vap_array[i],
                    sizeof(wifi_vap_info_t));
        tgt_vap_map.num_vaps = 1;

        // avoid disabling mesh sta in extender mode
        if (tgt_vap_map.vap_array[0].u.sta_info.enabled == false && is_sta_enabled()) {
            wifi_util_info_print(WIFI_SUPP, "%s:%d vap_index:%d skip disabling sta\n", __func__,
                __LINE__, tgt_vap_map.vap_array[0].vap_index);
            tgt_vap_map.vap_array[0].u.sta_info.enabled = true;
        }

        if (wifi_hal_createVAP(radio_index, &tgt_vap_map) != RETURN_OK) {
            wifi_util_error_print(WIFI_CTRL,"%s: wifi vap create failure: radio_index:%d vap_index:%d\n",__FUNCTION__,
                                                radio_index, map->vap_array[i].vap_index);
            continue;
        }
        wifi_util_info_print(WIFI_CTRL,"%s: wifi vap create success: radio_index:%d vap_index:%d\n",__FUNCTION__,
                                                radio_index, map->vap_array[i].vap_index);
        memcpy((unsigned char *)&map->vap_array[i], (unsigned char *)&tgt_vap_map.vap_array[0],
                    sizeof(wifi_vap_info_t));
        get_wifidb_obj()->desc.update_wifi_vap_info_fn(getVAPName(map->vap_array[i].vap_index), &map->vap_array[i],
            &rdk_vap_info[i]);
        get_wifidb_obj()->desc.update_wifi_security_config_fn(getVAPName(map->vap_array[i].vap_index),
            &map->vap_array[i].u.sta_info.security);
    }

    return 0;
}
