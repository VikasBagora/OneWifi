#include <stdio.h>
#include "stdlib.h"
#include <arpa/inet.h>
#include <ctype.h>
#include "wifi_ctrl.h"
#include "vap_svc.h"
#include "wifi_hal_rdk_framework.h"
#include "wifi_supp_dbus.h"
#include "wifi_supp.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_common.h"
#include "common/defs.h"

#if 0
#define wifi_util_dbg_print(module, format, ...)  printf(format, ##__VA_ARGS__)
#define wifi_util_info_print(module, format, ...) printf(format, ##__VA_ARGS__)
#define wifi_util_error_print(module, format, ...) printf(format, ##__VA_ARGS__)
#endif

static const char * const dont_quote[] = {
        "key_mgmt", "proto", "pairwise", "auth_alg", "group", "eap",
        "bssid", "scan_freq", "freq_list", "scan_ssid", "bssid_hint",
        "bssid_ignore", "bssid_accept", /* deprecated aliases */
        "bssid_blacklist", "bssid_whitelist",
        "group_mgmt",
        "ignore_broadcast_ssid",
#ifdef CONFIG_MESH
        "mesh_basic_rates",
#endif /* CONFIG_MESH */
#ifdef CONFIG_P2P
        "go_p2p_dev_addr", "p2p_client_list", "psk_list",
#endif /* CONFIG_P2P */
#ifdef CONFIG_INTERWORKING
        "roaming_consortium", "required_roaming_consortium",
#endif /* CONFIG_INTERWORKING */
        NULL
};

static const rdkb_dbus_wifi_prop_desc_t rdkb_dbus_global_wifi_prop[] = {
        { NULL, NULL, NULL, NULL, NULL, NULL }
};

static const rdkb_dbus_wifi_method_desc_t rdkb_dbus_global_wifi_methods[] = {
        { "CreateInterface", RDKB_DBUS_INTERFACE_NAME,
          (rdkb_dBus_method_handler) rdkb_dbus_handler_create_interface,
          {
                  { "args", "a{sv}", ARG_IN },
                  { "path", "o", ARG_OUT },
                  END_ARGS
          }
        },
        { NULL, NULL, NULL, { END_ARGS } }
};

static const rdkb_dbus_wifi_prop_desc_t rdkb_dbus_network_wifi_prop[] = {
        { NULL, NULL, NULL, NULL, NULL, NULL }
};


static const rdkb_dbus_wifi_signal_desc_t rdkb_dbus_network_wifi_signals[] = {
        { "PropertiesChanged", RDKB_DBUS_NEW_IFACE_NETWORK,
          {
                  { "properties", "a{sv}", ARG_OUT },
                  END_ARGS
          }
        },
        { NULL, NULL, { END_ARGS } }
};


static const rdkb_dbus_wifi_signal_desc_t rdkb_dbus_global_wifi_signals[] = {
        { "InterfaceAdded", RDKB_DBUS_INTERFACE_NAME,
          {
                  { "path", "o", ARG_OUT },
                  { "properties", "a{sv}", ARG_OUT },
                  END_ARGS
          }
        },
        { NULL, NULL, { END_ARGS } }
};

static const rdkb_dbus_wifi_method_desc_t rdkb_dbus_interface_wifi_methods[] = {
        { "Scan", RDKB_DBUS_NEW_INTERFACE,
          (rdkb_dBus_method_handler) rdkb_dbus_handler_scan,
          {
                  { "args", "a{sv}", ARG_IN },
                  END_ARGS
          }
        },
        { "AddNetwork", RDKB_DBUS_NEW_INTERFACE,
          (rdkb_dBus_method_handler) rdkb_dbus_handler_add_network,
          {    
                  { "args", "a{sv}", ARG_IN },
                  { "path", "o", ARG_OUT },
                  END_ARGS
          }    
        },
        { "SelectNetwork", RDKB_DBUS_NEW_INTERFACE,
          (rdkb_dBus_method_handler) rdkb_dbus_handler_select_network,
          {
                  { "path", "o", ARG_IN },
                  END_ARGS
          }
        },
	{ NULL, NULL, NULL, { END_ARGS } }
};

static const rdkb_dbus_wifi_signal_desc_t rdkb_dbus_interface_wifi_signals[] = {
        { "ScanDone", RDKB_DBUS_NEW_INTERFACE,
          {
                  { "success", "b", ARG_OUT },
                  END_ARGS
          }
        },
        { NULL, NULL, { END_ARGS } }
};


static const rdkb_dbus_wifi_prop_desc_t rdkb_dbus_interface_wifi_prop[] = {
        { "Capabilities", RDKB_DBUS_NEW_INTERFACE, "a{sv}",
          rdkb_dbus_getter_wifi_cap,
          NULL,
          NULL
        },
	{ "State", RDKB_DBUS_NEW_INTERFACE, "s",
          rdkb_dbus_getter_state,
          NULL,
          NULL
        },
        { "ApScan", RDKB_DBUS_NEW_INTERFACE, "u",
          rdkb_dbus_getter_ap_scan,
          rdkb_dbus_setter_ap_scan,
          NULL
        },
        { "Dot11RSNAConfigPMKLifetime", RDKB_DBUS_NEW_INTERFACE, "s",
          NULL,
          rdkb_dbus_setter_iface_global,
          "dot11RSNAConfigPMKLifetime"
        },		
        { "ApIsolate", RDKB_DBUS_NEW_INTERFACE, "s",
          NULL,
          rdkb_dbus_setter_iface_global,
          "ap_isolate"
        },		
        { NULL, NULL, NULL, NULL, NULL, NULL }
};

static const rdkb_dbus_wifi_prop_desc_t rdkb_dbus_wifi_bss_prop[] = {
	{ "SSID", RDKB_DBUS_NEW_IFACE_BSS, "ay",
          rdkb_dbus_getter_bss_ssid,
          NULL,
          NULL
        },
        { "BSSID", RDKB_DBUS_NEW_IFACE_BSS, "ay",
          rdkb_dbus_getter_bss_bssid,
          NULL,
          NULL
        },
        { "Privacy", RDKB_DBUS_NEW_IFACE_BSS, "b",
          rdkb_dbus_getter_bss_privacy,
          NULL,
          NULL
        },
        { "Mode", RDKB_DBUS_NEW_IFACE_BSS, "s",
          rdkb_dbus_getter_bss_mode,
          NULL,
          NULL
        },
	{ "Signal", RDKB_DBUS_NEW_IFACE_BSS, "n",
          rdkb_dbus_getter_bss_signal,
          NULL,
          NULL
        },
        { "Frequency", RDKB_DBUS_NEW_IFACE_BSS, "q",
          rdkb_dbus_getter_bss_freq,
          NULL,
          NULL
        },
        { "Rates", RDKB_DBUS_NEW_IFACE_BSS, "au",
          rdkb_dbus_getter_bss_rates,
          NULL,
          NULL
        },
        { "RSN", RDKB_DBUS_NEW_IFACE_BSS, "a{sv}",
          rdkb_dbus_getter_bss_rsn,
          NULL,
          NULL
        },
        { NULL, NULL, NULL, NULL, NULL, NULL }
};

static const rdkb_dbus_wifi_signal_desc_t rdkb_dbus_wifi_bss_signals[] = {
        { "PropertiesChanged", RDKB_DBUS_NEW_IFACE_BSS,
          {
                  { "properties", "a{sv}", ARG_OUT },
                  END_ARGS
          }
        },
        { NULL, NULL, { END_ARGS } }
};

void generate_bss_dbus_obj_name(char *name, uint32_t str_size, uint32_t bss_id)
{
    snprintf(name, str_size, SUPP_BSS_DBUS_OBJ_NAME"_%d", bss_id);
}

rdkb_dbus_wifi_obj_desc_t *get_wifi_supp_dbus_obj(char *name)
{
    wifi_ctrl_t *ctrl = (wifi_ctrl_t *)get_wifictrl_obj();
    vap_svc_t *sta_svc;
    vap_svc_sta_t   *sta;

    sta_svc = get_svc_by_type(ctrl, vap_svc_type_sta);
    sta = &sta_svc->u.sta;

    if (sta->p_wifi_supp_info == NULL) {
        wifi_util_error_print(WIFI_SUPP,"%s:%d Error: memory alloc is failed"
            " for wifi supp info obj\n", __func__, __LINE__);
        return NULL;
    }
    rdkb_dbus_wifi_obj_desc_t *dbus_obj_desc;
    hash_map_t *supp_dbus_obj_map = sta->p_wifi_supp_info->supp_dbus_obj_desc_map;

    dbus_obj_desc = hash_map_get(supp_dbus_obj_map, name);
    if (dbus_obj_desc != NULL) {
        wifi_util_info_print(WIFI_SUPP,"%s:%d wifi supp already exist\n", __func__, __LINE__);
        return dbus_obj_desc;
    }

    dbus_obj_desc = malloc(sizeof(rdkb_dbus_wifi_obj_desc_t));
    if (dbus_obj_desc == NULL) {
        wifi_util_error_print(WIFI_SUPP,"%s:%d Error: memory alloc is failed"
            " for wifi dbus obj desc\n", __func__, __LINE__);
        return NULL;
    }

    dbus_obj_desc->connection = sta->p_wifi_supp_info->supp_info.dbus_connection;
    hash_map_put(supp_dbus_obj_map, strdup(name), dbus_obj_desc);

    return dbus_obj_desc;
}

int snprintf_error(size_t size, int res)
{               
        return res < 0 || (unsigned int) res >= size;
}


int copy_scan_result_to_bss_nm(rdkb_wifi_bss_t *bss_nm, wifi_bss_info_t *wifi_bss)
{

   memset(bss_nm, 0, sizeof(rdkb_wifi_bss_t));

   memcpy(bss_nm->bssid, wifi_bss->bssid, sizeof(wifi_bss->bssid));
   strcpy(bss_nm->ssid, wifi_bss->ssid);
   bss_nm->ssid_len = strlen(wifi_bss->ssid);
   bss_nm->freq = wifi_bss->freq;
   wifi_util_dbg_print(WIFI_SUPP,"wifi_bss->ie_len:%d ssid:%s\r\n", wifi_bss->ie_len, wifi_bss->ssid);

   memcpy(bss_nm->ies, wifi_bss->ie, wifi_bss->ie_len);
   bss_nm->ie_len = wifi_bss->ie_len;
   bss_nm->level = wifi_bss->rssi;
   bss_nm->caps = wifi_bss->caps;

   memcpy(&bss_nm->scan_bss_info.external_ap, wifi_bss, sizeof(bss_nm->scan_bss_info.external_ap));
   bss_nm->scan_bss_info.vap_index = 0;
   bss_nm->scan_bss_info.radio_index = 0;
   return TRUE;
}

rdkb_dbus_wifi_obj_desc_t *rdkb_dbus_obj_init_desc(char *name, const char *path,
                                void *user_data, rdkb_dbus_arg_free_func free_func,
                                const rdkb_dbus_wifi_method_desc_t *methods,
                                const rdkb_dbus_wifi_prop_desc_t *properties,
                                const rdkb_dbus_wifi_signal_desc_t *signals)
{
    rdkb_dbus_wifi_obj_desc_t *obj_desc = get_wifi_supp_dbus_obj(name);
    uint32_t n = 0;

    obj_desc->user_data = user_data;
    obj_desc->user_data_free_func = free_func;
    obj_desc->methods = methods;
    obj_desc->properties = properties;
    obj_desc->signals = signals;
    obj_desc->path = path;

    for (n = 0; properties && properties->dbus_property; properties++) {
        n++;
    }

    if (n != 0) {
        obj_desc->prop_changed_flags = malloc(n);
        if (!obj_desc->prop_changed_flags) {
            wifi_util_error_print(WIFI_SUPP,"dbus: %s: can't register handlers\r\n", __func__);
        }
        memset(obj_desc->prop_changed_flags, 0, n);
    } else {
        obj_desc->prop_changed_flags = NULL;
    }

    return obj_desc;
}

static void rdkb_dbus_signal_process(char *obj_path, const char *obj_interface,
					const char *sig_path,  const char *sig_interface, const char *sig_name,
                                       dbus_bool_t properties, DBusConnection *con, const char *bss_path)
{
        DBusMessage *msg;
        DBusMessageIter iter;
	char tmp_path[100] = { 0 };

        if (!obj_path || !obj_interface || !sig_path || !sig_interface || !sig_name) {
		wifi_util_error_print(WIFI_SUPP,"%s():%d: NULL: obj_path:%s, obj_interface:%s, sig_path:%s, sig_interface:%s, sig_name:%s\n", __func__, __LINE__, 
			obj_path, obj_interface, sig_path, sig_interface, sig_name);
                return;
	}


	wifi_util_dbg_print(WIFI_SUPP,"%s()%d: NEW_SIGNAL: bss_obj_path:%s, wpa_s->dbus_new_path:%s, RDKB_DBUS_NEW_INTERFACE:%s"
		"  RDKB_DBUS_NEW_IFACE_BSS:%s, sig_name:%s\n",
		__func__, __LINE__, bss_path, sig_path, sig_interface, obj_interface, sig_name);

	if (bss_path) {
        	msg = dbus_message_new_signal(sig_path, obj_path, sig_name);
	        if (msg == NULL) {
			wifi_util_error_print(WIFI_SUPP,"%s():%d: dbus_message_new_signal() failed\n", __func__, __LINE__);	
                	return;
		}

        	dbus_message_iter_init_append(msg, &iter);
	        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &bss_path) ||
		   (properties && !rdkb_dbus_get_obj_prop(con, bss_path, obj_interface ,&iter))) {
			wifi_util_error_print(WIFI_SUPP,"%s():%d: dbus: Failed to construct signal\n", __func__, __LINE__);
        	} else {
			wifi_util_dbg_print(WIFI_SUPP,"%s():%d: dbus: signal sent\n", __func__, __LINE__);
	                dbus_connection_send(con, msg, NULL);
		}
	} else {
        	msg = dbus_message_new_signal(sig_path, sig_interface, sig_name);
	        if (msg == NULL) {
			wifi_util_error_print(WIFI_SUPP,"%s():%d: dbus_message_new_signal() failed\n", __func__, __LINE__);	
                	return;
		}

	        dbus_message_iter_init_append(msg, &iter);
	        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &obj_path) ||
		   (properties && !rdkb_dbus_get_obj_prop(con, obj_path, obj_interface ,&iter))) {
			wifi_util_error_print(WIFI_SUPP,"%s():%d: dbus: Failed to construct signal\n", __func__, __LINE__);
        	} else {
			wifi_util_dbg_print(WIFI_SUPP,"%s():%d: dbus: signal sent\n", __func__, __LINE__);
        	        dbus_connection_send(con, msg, NULL);
		}
	}

        dbus_message_unref(msg);
}

DBusMessage * rdkb_dbus_handler_create_interface(DBusMessage *message, void *global)
{
    DBusMessage *reply = NULL;
    DBusMessageIter iter;
    char *new_path = RDKB_DBUS_NEW_INTERFACE_PATH;
    rdkb_dbus_wifi_obj_desc_t *obj_desc = NULL;

    dbus_message_iter_init(message, &iter);

    obj_desc = rdkb_dbus_obj_init_desc(SUPP_INTERFACE_DBUS_OBJ_NAME, RDKB_DBUS_NEW_INTERFACE_PATH, global, NULL,
    	rdkb_dbus_interface_wifi_methods, rdkb_dbus_interface_wifi_prop, rdkb_dbus_interface_wifi_signals);

    rdkb_dbus_reg_obj_per_iface(RDKB_DBUS_NEW_INTERFACE_PATH, obj_desc);
    rdkb_dbus_signal_process(RDKB_DBUS_NEW_INTERFACE_PATH, RDKB_DBUS_NEW_INTERFACE,
    	RDKB_DBUS_OBJ_PATH, RDKB_DBUS_SERVICE_NAME, "InterfaceAdded", TRUE, obj_desc->connection, NULL);

    reply = dbus_message_new_method_return(message);
    dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH,
                                                   &obj_desc->path, DBUS_TYPE_INVALID);

    return reply;
}

char * rdkb_dbus_new_decompose_obj_path(const char *path, const char *sep,
                                           char **item)
{
        const unsigned int dev_path_prefix_len = strlen(RDKB_DBUS_NEW_PATH_INTERFACES "/");
        char *obj_path_only;
        char *pos;
        size_t sep_len;

        *item = NULL;

        if (strncmp(path, RDKB_DBUS_NEW_PATH_INTERFACES "/",
                       dev_path_prefix_len) != 0) {
                return NULL;
	}

        if ((path + dev_path_prefix_len)[0] == '\0') {
                return NULL;
	}

        obj_path_only = strdup(path);
        if (obj_path_only == NULL) {
                return NULL;
	}

        pos = obj_path_only + dev_path_prefix_len;
        pos = strchr(pos, '/');
        if (pos == NULL) {
                return obj_path_only;
	}

        *pos++ = '\0';

        sep_len = strlen(sep);
        if (strncmp(pos, sep, sep_len) != 0 || pos[sep_len] != '/') {
                return obj_path_only;
	}

        *item = pos + sep_len + 1;
        return obj_path_only;
}

void rdkb_dbus_signal_network_selected(rdkb_wifi_supp_param_t *wpa_s, int id)
{
    rdkb_dbus_signal_network(wpa_s, id, "NetworkSelected", FALSE);
}

DBusMessage * rdkb_dbus_handler_select_network(DBusMessage *message,
                                               rdkb_wifi_supp_param_t *wpa_s)
{
        DBusMessage *reply = NULL;
        const char *op;
        char *iface, *net_id;
        int id;
        struct wpa_ssid *ssid;

        dbus_message_get_args(message, NULL, DBUS_TYPE_OBJECT_PATH, &op,
                              DBUS_TYPE_INVALID);

        iface = rdkb_dbus_new_decompose_obj_path(op,
                                                    RDKB_DBUS_NEW_NETWORKS_PART,
                                                    &net_id);
        if (iface == NULL || net_id == NULL || !wpa_s->dbus_new_path ||
            strcmp(iface, wpa_s->dbus_new_path) != 0) {
                reply = rdkb_dbus_error_invalid_args(message, op);
                goto out;
        }

        errno = 0;
        id = strtoul(net_id, NULL, 10);
        if (errno != 0) {
                reply = rdkb_dbus_error_invalid_args(message, op);
                goto out;
        }

        uint32_t vap_index = wpa_s->scan_bss_info.vap_index;
        wifi_bss_info_t *p_external_ap = &wpa_s->scan_bss_info.external_ap;
        printf("%s:%d sta connect for vap index: %d\n", __func__, __LINE__, vap_index);
        printf("1  =====>wpa_s:%p, net_id:%s, id:%d, p_external_ap: ssid:%s, sec_mode:%d, rssi:%d, caps:%x, password:%s!\n",
            wpa_s, net_id, id, p_external_ap->ssid, p_external_ap->sec_mode, p_external_ap->rssi, p_external_ap->caps, wpa_s->scan_bss_info.password);
        wifi_vap_security_t l_recv_security = { 0 };

        strcpy(l_recv_security.u.key.key, wpa_s->scan_bss_info.password);

        set_sta_wifi_security_cfg(vap_index, &l_recv_security);

        sta_connect(vap_index, p_external_ap);
        
	uint32_t old_network_ssid_id = 0;
        if (wpa_s->scan_bss_info.network_ssid_id > 0) {
            old_network_ssid_id = wpa_s->scan_bss_info.network_ssid_id - 1;
	}

        rdkb_dbus_signal_network_selected(wpa_s, old_network_ssid_id);

out:
        free(iface);
        return reply;
}

static void rdkb_dbus_signal_network(rdkb_wifi_supp_param_t *wpa_s,
                                     int id, const char *sig_name,
                                     dbus_bool_t properties)
{
        DBusMessage *msg;
        DBusMessageIter iter;
        char net_obj_path[RDKB_DBUS_OBJ_PATH_MAX], *path;


	if (!wpa_s->dbus_new_path) {
                return;
	}

        snprintf(net_obj_path, RDKB_DBUS_OBJ_PATH_MAX,
                    "%s/" RDKB_DBUS_NEW_NETWORKS_PART "/%u",
                    wpa_s->dbus_new_path, id);

        msg = dbus_message_new_signal(wpa_s->dbus_new_path,
                                      RDKB_DBUS_NEW_INTERFACE,
                                      sig_name);
        if (msg == NULL) {
                return;
	}

	
        printf("%s:%d Network signal: path:%s, sig_name:%s\r\n", __func__, __LINE__, net_obj_path, sig_name);

        dbus_message_iter_init_append(msg, &iter);
        path = net_obj_path;
        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                            &path) ||
            (properties &&
             !rdkb_dbus_get_obj_prop(wpa_s->dbus_connection, net_obj_path, RDKB_DBUS_NEW_IFACE_NETWORK, &iter)))
                wifi_util_error_print(WIFI_SUPP, "dbus: Failed to construct signal");
        else
                dbus_connection_send(wpa_s->dbus_connection, msg, NULL);
        dbus_message_unref(msg);
}

static void rdkb_dbus_signal_network_added(rdkb_wifi_supp_param_t *wpa_s, int id)
{
        rdkb_dbus_signal_network(wpa_s, id, "NetworkAdded", TRUE);
}

int rdkb_dbus_reg_network(rdkb_wifi_supp_param_t *wpa_s,
                               scan_list_bss_info_t *scan_bss_info)
{       
        struct wpas_dbus_priv *ctrl_iface;
        struct network_handler_args *arg;
        char net_obj_path[RDKB_DBUS_OBJ_PATH_MAX];
        char key_buff[32] = { 0 };

        if (wpa_s == NULL || !wpa_s->dbus_new_path) {
                return 0;
	}
        
        snprintf(net_obj_path, RDKB_DBUS_OBJ_PATH_MAX,
                    "%s/" RDKB_DBUS_NEW_NETWORKS_PART "/%u",
                    wpa_s->dbus_new_path, scan_bss_info->network_ssid_id);
        
        wifi_util_info_print(WIFI_SUPP,"dbus: Register network object '%s'", net_obj_path);

        arg = malloc(sizeof(struct network_handler_args));
        if (!arg) {
                wifi_util_error_print(WIFI_SUPP,
                           "Not enough memory to create arguments for method\r\n");
                goto err;
        }
        memset(arg, 0, sizeof(struct network_handler_args));

        arg->wpa_s = wpa_s;
        scan_bss_info->network_ssid_id = wpa_s->scan_bss_info.network_ssid_id;

	memcpy(&arg->scan_bss_info, scan_bss_info, sizeof(scan_list_bss_info_t));

        memcpy(&wpa_s->scan_bss_info, scan_bss_info, sizeof(scan_list_bss_info_t));
      
        rdkb_dbus_wifi_obj_desc_t *wpa_obj_desc = rdkb_dbus_obj_init_desc(SUPP_NETWORK_DBUS_OBJ_NAME, net_obj_path, arg, NULL, NULL, rdkb_dbus_network_wifi_prop, rdkb_dbus_network_wifi_signals);

        rdkb_dbus_reg_obj_per_iface(net_obj_path, wpa_obj_desc);
 
        rdkb_dbus_signal_network_added(wpa_s, scan_bss_info->network_ssid_id);

        if (wpa_s->network_handler_args_map != NULL) {
            snprintf(key_buff, sizeof(key_buff), "network_%d_reg", scan_bss_info->network_ssid_id);
            hash_map_put(wpa_s->network_handler_args_map, strdup(key_buff), arg);
        } else {
            wifi_util_error_print(WIFI_SUPP,"%s:%d network handler arg map is NULL\r\n", __func__, __LINE__);
        }

        wpa_s->scan_bss_info.network_ssid_id++;
        return 0;

err:
        //free_dbus_object_desc(obj_desc);
	free(wpa_obj_desc);
        return -1;
}

int rdkb_notify_network_added(rdkb_wifi_supp_param_t *wpa_s,
                               scan_list_bss_info_t *scan_bss_info)
{
    return rdkb_dbus_reg_network(wpa_s, scan_bss_info);
}

int rdkb_supp_add_network(rdkb_wifi_supp_param_t *wpa_s, scan_list_bss_info_t *scan_bss_info)
{
    return rdkb_notify_network_added(wpa_s, scan_bss_info);
}

int hex_to_string(const char *hex_str, char *str) {
    int len = strlen(hex_str);
    if (len % 2 != 0) {
        return RETURN_ERR;
    }

    int i, j;
    for (i = 0, j = 0; i < len; i += 2) {
        char hex_byte[3] = {hex_str[i], hex_str[i+1], '\0'}; 
        char byte = (char)strtol(hex_byte, NULL, 16);
        str[j++] = byte;
    }
    str[j] = '\0';

    return RETURN_OK;
}

static dbus_bool_t should_quote_opt(const char *key)
{
        int i = 0;

        while (dont_quote[i] != NULL) {
                if (os_strcmp(key, dont_quote[i]) == 0)
                        return FALSE;
                i++;
        }
        return TRUE;
}

dbus_bool_t set_wifi_network_prop(rdkb_wifi_supp_param_t *wpa_s,
                                   network_mgr_cfg_t *scan_ssid_info,
                                   DBusMessageIter *iter,
                                   DBusError *error)
{
        rdkb_dbus_dict_entry_t entry = { .type = DBUS_TYPE_STRING };
        DBusMessageIter iter_dict;
        char *value = NULL;

        if (!rdkb_dbus_dict_open_read(iter, &iter_dict, error))
                return FALSE;

        while (rdkb_dbus_dict_has_dict_entry(&iter_dict)) {
                size_t size = 50;
                int ret;

                if (!rdkb_dbus_dict_get_entry(&iter_dict, &entry))
                        goto error;

                value = NULL;
                if (entry.type == DBUS_TYPE_ARRAY &&
                    entry.array_type == DBUS_TYPE_BYTE) {
                        if (entry.array_len <= 0)
                                goto error;

                        size = entry.array_len * 2 + 1;
                        value = os_zalloc(size);
                        if (value == NULL)
                                goto error;

                        ret = wpa_snprintf_hex(value, size,
                                               (uint8_t *) entry.bytearray_value,
                                               entry.array_len);
                        if (ret <= 0)
                                goto error;
                } else if (entry.type == DBUS_TYPE_STRING) {
                        if (should_quote_opt(entry.key)) {
                                size = os_strlen(entry.str_value);

                                size += 3;
                                value = os_zalloc(size);
                                if (value == NULL)
                                        goto error;

                                ret = os_snprintf(value, size, "%s",
                                                  entry.str_value);
                                if (snprintf_error(size, ret))
                                        goto error;
                        } else {
                                value = os_strdup(entry.str_value);
                                if (value == NULL)
                                        goto error;
                        }
                } else if (entry.type == DBUS_TYPE_UINT32) {
                        value = os_zalloc(size);
                        if (value == NULL)
                                goto error;

                        ret = os_snprintf(value, size, "%u",
                                          entry.uint32_value);
                        if (snprintf_error(size, ret))
                                goto error;
                } else if (entry.type == DBUS_TYPE_INT32) {
                        value = os_zalloc(size);
                        if (value == NULL)
                                goto error;

                        ret = os_snprintf(value, size, "%d",
                                          entry.int32_value);
                        if (snprintf_error(size, ret))
                                goto error;
                } else
                        goto error;

		if (entry.type == DBUS_TYPE_ARRAY) {
                    char buff[64] = { 0 };
			hex_to_string(value, buff);

                     if (!strcmp(entry.key, "ssid")) {
                        printf("recived SSID:%s\r\n", value);
                         strcpy(scan_ssid_info->ssid , buff);
		     } else if (!strcmp(entry.key, "bgscan")) {
                         strcpy(scan_ssid_info->bgscan , buff);
		     }
                } else if (entry.type == DBUS_TYPE_STRING) {
                    if (!strcmp(entry.key, "key_mgmt")) {
                        strcpy(scan_ssid_info->security_type, value);
		    } else if (!strcmp(entry.key, "psk")) {
                        printf("recived key:%s\r\n", value);
			strcpy(scan_ssid_info->password, value);
		    }
		} else if (entry.type == DBUS_TYPE_INT32) {
                    if (!strcmp(entry.key, "scan_ssid")) {
                        scan_ssid_info->scan_ssid = atoi(value);
		    }
		} else {
                    wifi_util_error_print(WIFI_SUPP,"unknown event type:%d\r\n", entry.type);
		}
	
        skip_update:
                os_free(value);
                value = NULL;
                rdkb_dbus_dict_entry_clear(&entry);
        }

        return TRUE;

error:
        os_free(value);
        rdkb_dbus_dict_entry_clear(&entry);
        dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
                             "invalid message format");
        return FALSE;
}

int fetch_bss_info(rdkb_wifi_supp_param_t *wpa_s, network_mgr_cfg_t *add_ssid_cfg, scan_list_bss_info_t *scan_bss_info)
{
        rdkb_wifi_bss_t *bss;
        circular_list_for_each(bss, &wpa_s->bss, rdkb_wifi_bss_t, list) {
                if (strcmp(bss->ssid, add_ssid_cfg->ssid) == 0) {
                    memcpy(scan_bss_info, &bss->scan_bss_info, sizeof(scan_list_bss_info_t));
		    strcpy(scan_bss_info->password, add_ssid_cfg->password);
                    return RETURN_OK;
		}
        }  

        return RETURN_ERR;
}

int get_sta_state() {
    wifi_ctrl_t *ctrl = (wifi_ctrl_t *)get_wifictrl_obj();
    vap_svc_t *sta_svc;
    vap_svc_sta_t   *sta;

    sta_svc = get_svc_by_type(ctrl, vap_svc_type_sta);
    sta = &sta_svc->u.sta;

    return sta->conn_state;
}

DBusMessage * rdkb_dbus_handler_add_network(DBusMessage *message,
                                            rdkb_wifi_supp_param_t *wpa_s)
{
        DBusMessage *reply = NULL;
        DBusMessageIter iter;
        struct wpa_ssid *ssid = NULL;
        char path_buf[RDKB_DBUS_OBJ_PATH_MAX], *path = path_buf;
        DBusError error;
        network_mgr_cfg_t add_ssid_cfg = { 0 };
        scan_list_bss_info_t scan_bss_info = { 0 };
	sta_state_t sta_state = get_sta_state();

	if (!wpa_s) {
		wifi_util_error_print(WIFI_SUPP,"wifi supplicant param is NULL\n");
		return;
	} else {
	        wifi_util_dbg_print(WIFI_SUPP,"%s:%d dbus_new_path:%s\n", __func__, __LINE__, wpa_s->dbus_new_path);
	}

	/* Client is already connected. Defensive code to prevent a crash (already registered) */
	if (sta_state == sta_state_connected) {
            dbus_message_iter_init(message, &iter);
            reply = dbus_message_new_method_return(message);
            if (reply == NULL) {
                reply = rdkb_dbus_error_no_memory(message);
                goto err; 
            }
	    wifi_util_dbg_print(WIFI_SUPP, "Client is already connected. State:%d\n", __func__, __LINE__, sta_state);
	    return reply;
	}

        dbus_message_iter_init(message, &iter);

        dbus_error_init(&error);
        if (!set_wifi_network_prop(wpa_s, &add_ssid_cfg, &iter, &error)) {
                 wifi_util_error_print(WIFI_SUPP,"%s[dbus]: control interface couldn't set network properties\r\n", __func__);
                 reply = rdkb_dbus_reply_new_from_error(message, &error,
                                                        DBUS_ERROR_INVALID_ARGS,
                                                        "Failed to add network");
                 dbus_error_free(&error);
                 goto err; 
         }

        fetch_bss_info(wpa_s, &add_ssid_cfg, &scan_bss_info);

        if (wpa_s->dbus_new_path)
                rdkb_supp_add_network(wpa_s, &scan_bss_info);

        os_snprintf(path, RDKB_DBUS_OBJ_PATH_MAX,
                    "%s/" RDKB_DBUS_NEW_NETWORKS_PART "/%d",
                    wpa_s->dbus_new_path, scan_bss_info.network_ssid_id);

        reply = dbus_message_new_method_return(message);
        if (reply == NULL) {
                reply = rdkb_dbus_error_no_memory(message);
                goto err; 
        }    
        if (!dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &path,
                                      DBUS_TYPE_INVALID)) {
                dbus_message_unref(reply);
                reply = rdkb_dbus_error_no_memory(message);
                goto err; 
        }

	return reply;

err:
        if (ssid) {
//                wpas_notify_network_removed(wpa_s, ssid);
//                wpa_config_remove_network(wpa_s->conf, ssid->id);
        }
        return reply;
}

DBusMessage *rdkb_dbus_handler_scan(DBusMessage *message)
{
        DBusMessage *reply = NULL;
        DBusMessageIter iter, dict_iter, entry_iter, variant_iter;
	signed int ret = 0;
        char *key = NULL, *type = NULL;
        size_t i;
	wifi_ctrl_t *ctrl = NULL;
	vap_svc_t *svc = NULL;
        dbus_bool_t allow_roam = 1;

        dbus_message_iter_init(message, &iter);

        dbus_message_iter_recurse(&iter, &dict_iter);

        while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
                dbus_message_iter_recurse(&dict_iter, &entry_iter);
                dbus_message_iter_get_basic(&entry_iter, &key);
                dbus_message_iter_next(&entry_iter);
                dbus_message_iter_recurse(&entry_iter, &variant_iter);

                if (strcmp(key, "Type") == 0) { 
                        if (dbus_get_scan_type(message, &variant_iter,
                                                    &type, &reply) < 0) { 
                                goto out;
			}
                } else {
                        wifi_util_error_print(WIFI_SUPP,"%s[dbus]: Unknown argument %s\r\n",
                                   __func__, key);
                        // reply = rdkb_dbus_error_invalid_args(message, key);
                        goto out; 
                }    

                dbus_message_iter_next(&dict_iter);
        }

        if (!type) {
                wifi_util_error_print(WIFI_SUPP,"%s[dbus]: Scan type not specified\r\n",
                           __func__);
                reply = rdkb_dbus_error_invalid_args(message, key);
                goto out;
        }

        if (strcmp(type, "passive") == 0) {
        } else if (strcmp(type, "active") == 0) {
        } else {
                wifi_util_error_print(WIFI_SUPP,"%s[dbus]: Unknown scan type: %s\r\n",
                           __func__, type);
                reply = rdkb_dbus_error_invalid_args(message,
                                                     "Wrong scan type");
                goto out;
        }

out:
        ctrl = (wifi_ctrl_t *)get_wifictrl_obj();
        svc = get_svc_by_type(ctrl, vap_svc_type_sta);

        /* Scan is internal driven but honor for very first time */
        if (svc->u.sta.last_scan_done) {
            /* Scan result has populated at-least once. Skip external scan and send cached scan results */
            // send_cached_scan_results();

	    /* This is not vorrect but workaround for now as needs further debugging */
	    reply = dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS, NULL);
	    return reply;
	}

        ret = sta_start_scan(svc);
        if (ret == SCAN_INPROGRESS) {
#ifndef WORKAROUND
           printf("SCAN IN PRGORESS...REJECT FROM WORKAROUND\n");
           reply = dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS, NULL);
#else
           printf("SCAN IN PRGORESS...REJECT WITHOUT WORKAROUND\n");
           reply = rdkb_dbus_error_scan_error(message, "Scan request rejected");
#endif
        }

        return reply;
}

dbus_bool_t get_default_wifi_cap(const rdkb_dbus_wifi_prop_desc_t *property_desc, DBusMessageIter *iter, DBusError *error, void *user_data) 
{
        rdkb_wifi_supp_param_t *wpa_s = user_data;
        DBusMessageIter iter_dict, iter_dict_entry, iter_dict_val, iter_array,
                variant_iter;
        const char *scans[] = { "active", "passive", "ssid" };

        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                                              "a{sv}", &variant_iter) ||
            !rdkb_dbus_dict_open_write(&variant_iter, &iter_dict))
                goto nomem;

#ifdef CONFIG_NO_TKIP
	const char *args[] = {"ccmp", "none"};
#else /* CONFIG_NO_TKIP */
	const char *args[] = {"ccmp", "tkip", "none"};
#endif /* CONFIG_NO_TKIP */

	if (!rdkb_dbus_dict_append_str_array(
		    &iter_dict, "Pairwise", args,
		    ARRAY_SIZE(args))) {
		goto nomem;
	}

	const char *args_grp[] = {
		"ccmp",
#ifndef CONFIG_NO_TKIP
		"tkip",
#endif /* CONFIG_NO_TKIP */
#ifdef CONFIG_WEP
		"wep104", "wep40"
#endif /* CONFIG_WEP */
	};   

	if (!rdkb_dbus_dict_append_str_array(
		    &iter_dict, "Group", args_grp,
		    ARRAY_SIZE(args_grp))) {
		goto nomem; 
	}

       const char *args_key_mgmt[] = {
		"wpa-psk", "wpa-eap", "ieee8021x", "wpa-none",
#ifdef CONFIG_WPS
		"wps",
#endif /* CONFIG_WPS */
		"none"
	};
	if (!rdkb_dbus_dict_append_str_array(
		    &iter_dict, "KeyMgmt", args_key_mgmt,
		    ARRAY_SIZE(args_key_mgmt))) {
		goto nomem;
	}

	const char *args_protocol[] = { "rsn", "wpa" };

	if (!rdkb_dbus_dict_append_str_array(
		    &iter_dict, "Protocol", args_protocol,
		    ARRAY_SIZE(args_protocol))) {
		goto nomem;
	}

	const char *args_auth_algo[] = { "open", "shared", "leap" };

	if (!rdkb_dbus_dict_append_str_array(
		    &iter_dict, "AuthAlg", args_auth_algo,
		    ARRAY_SIZE(args_auth_algo))) {
		goto nomem;
	}

        /***** Scan */
        if (!rdkb_dbus_dict_append_str_array(&iter_dict, "Scan", scans,
                                               ARRAY_SIZE(scans))) {
                goto nomem;
	}

        /***** Modes */
        if (!rdkb_dbus_dict_begin_str_array(&iter_dict, "Modes",
                                              &iter_dict_entry,
                                              &iter_dict_val,
                                              &iter_array) ||
            !rdkb_dbus_dict_str_array_add_elem(
                    &iter_array, "infrastructure") ||
            !rdkb_dbus_dict_end_str_array(&iter_dict,
                                            &iter_dict_entry,
                                            &iter_dict_val,
                                            &iter_array)) {
                goto nomem;
	}
        /***** Modes end */

	dbus_int32_t max_scan_ssid = MAX_SCANNED_VAPS;

	if (!rdkb_dbus_dict_append_int32(&iter_dict, "MaxScanSSID",
                                                max_scan_ssid)) {
                        goto nomem;
	}

        if (!rdkb_dbus_dict_close_write(&variant_iter, &iter_dict) ||
            !dbus_message_iter_close_container(iter, &variant_iter))
                goto nomem;

        return TRUE;

nomem:
        dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
        return FALSE;
}

dbus_bool_t rdkb_dbus_getter_wifi_cap(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
	return get_default_wifi_cap(property_desc, iter, error, user_data);
}

const char * rdkb_wifi_supp_state_to_str(enum sta_states state)
{
        switch (state) {
        case STA_DISCONNECTED:
                return "DISCONNECTED";
        case STA_INACTIVE:
                return "INACTIVE";
        case STA_INTERFACE_DISABLED:
                return "INTERFACE_DISABLED";
        case STA_SCANNING:
                return "SCANNING";
        case STA_AUTHENTICATING:
                return "AUTHENTICATING";
        case STA_ASSOCIATING:
                return "ASSOCIATING";
        case STA_ASSOCIATED:
                return "ASSOCIATED";
        case STA_4WAY_HANDSHAKE:
                return "4WAY_HANDSHAKE";
        case STA_GROUP_HANDSHAKE:
                return "GROUP_HANDSHAKE";
        case STA_COMPLETED:
                return "COMPLETED";
        default:
                return "UNKNOWN";
        }
}

dbus_bool_t rdkb_dbus_setter_iface_global(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        rdkb_wifi_supp_param_t *wpa_s = user_data;
        const char *new_value = NULL;
        char buf[250];
        size_t combined_len;
        int ret;

        if (!rdkb_dbus_simple_prop_setter(iter, error, DBUS_TYPE_STRING,
                                              &new_value))
                return FALSE;

        combined_len = os_strlen(property_desc->data) + os_strlen(new_value) +
                3;
        if (combined_len >= sizeof(buf)) {
                dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
                               "Interface property %s value too large",
                               property_desc->dbus_property);
                return FALSE;
        }

        if (!new_value[0])
                new_value = "NULL";

        ret = os_snprintf(buf, combined_len, "%s=%s", property_desc->data,
                          new_value);
        if (snprintf_error(combined_len, ret)) {
                dbus_set_error(error,  RDKB_DBUS_UNKNOWN_ERROR,
                               "Failed to construct new interface property %s",
                               property_desc->dbus_property);
                return FALSE;
        }

        return TRUE;
}

dbus_bool_t rdkb_dbus_getter_ap_scan(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{   
        rdkb_wifi_supp_param_t *wpa_s = user_data;
        dbus_uint32_t ap_scan = 1;
        return rdkb_dbus_simple_prop_getter(iter, DBUS_TYPE_UINT32,
                                                &ap_scan, error);
}

int rdkb_wifi_supp_set_ap_scan(rdkb_wifi_supp_param_t *wpa_s, int ap_scan)
{
        return 0;
}

dbus_bool_t rdkb_dbus_setter_ap_scan(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        rdkb_wifi_supp_param_t *wpa_s = user_data;
        dbus_uint32_t ap_scan;

	wifi_util_dbg_print(WIFI_SUPP,"In rdkb_dbus_setter_ap_scan ***\n");
        if (!rdkb_dbus_simple_prop_setter(iter, error, DBUS_TYPE_UINT32,
                                              &ap_scan))
                return FALSE;
        if (rdkb_wifi_supp_set_ap_scan(wpa_s, ap_scan)) {
                dbus_set_error_const(error, DBUS_ERROR_FAILED,
                                     "ap_scan must be 0, 1, or 2");
                return FALSE;
        }
        return TRUE;
}

dbus_bool_t rdkb_dbus_getter_state(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        rdkb_wifi_supp_param_t *wpa_s = user_data;
        const char *str_state;
        char *state_ls, *tmp;
        dbus_bool_t success = FALSE;

        //str_state = rdkb_wifi_supp_state_to_str(wpa_s->wpa_state);
	// TBD - revisit
        str_state = rdkb_wifi_supp_state_to_str(STA_INACTIVE);
        //str_state = rdkb_wifi_supp_state_to_str(STA_COMPLETED);

        /* make state string lowercase to fit new DBus API convention
         */
        state_ls = tmp = strdup(str_state);
        if (!tmp) {
                dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
                return FALSE;
        }
        while (*tmp) {
                *tmp = tolower(*tmp);
                tmp++;
        }

        success = rdkb_dbus_simple_prop_getter(iter, DBUS_TYPE_STRING,
                                                   &state_ls, error);

        free(state_ls);

        return success;
}

static DBusMessage * process_msg_method_handler(DBusMessage *message,
                                          rdkb_dbus_wifi_obj_desc_t *obj_dsc)
{
        const rdkb_dbus_wifi_method_desc_t *method_dsc = obj_dsc->methods;
        const char *method;
        const char *msg_interface;

        method = dbus_message_get_member(message);
        msg_interface = dbus_message_get_interface(message);

        printf("======>method handler for %s.%s on %s\r\n",
                           msg_interface, method,
                           dbus_message_get_path(message));

        while (method_dsc && method_dsc->dbus_method) {
                if (!strncmp(method_dsc->dbus_method, method,
                                RDKB_DBUS_METHOD_SIGNAL_PROP_MAX) &&
                    !strncmp(method_dsc->dbus_interface, msg_interface,
                                RDKB_DBUS_INTERFACE_MAX))
                        break;

                method_dsc++;
        }    
        if (method_dsc == NULL || method_dsc->dbus_method == NULL) {
                printf("no method handler for %s.%s on %s\r\n",
                           msg_interface, method,
                           dbus_message_get_path(message));
                wifi_util_error_print(WIFI_SUPP,"no method handler for %s.%s on %s",
                           msg_interface, method,
                           dbus_message_get_path(message));
                return dbus_message_new_error(message,
                                              DBUS_ERROR_UNKNOWN_METHOD, NULL);
        }    

        return method_dsc->method_handler(message, obj_dsc->user_data);
}

dbus_bool_t rdkb_dbus_getter_bss_bssid(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;

        res = rdkb_get_bss_helper(args, error, __func__);
        if (!res)
                return FALSE;

        return rdkb_dbus_simple_array_prop_getter(iter, DBUS_TYPE_BYTE,
                                                      res->bssid, ETH_ALEN,
                                                      error);
}

int rdkb_dbus_get_obj_prop(DBusConnection *con, const char *path, const char *interface, DBusMessageIter *iter) 
{
    rdkb_dbus_wifi_obj_desc_t *obj_desc = NULL;
    DBusMessageIter dict_iter;
    DBusError error;

    dbus_connection_get_object_path_data(con, path, (void **) &obj_desc);

    if (!obj_desc) {
        wifi_util_error_print(WIFI_SUPP,"dbus: %s: could not obtain object's private data: %s\r\n", __func__, path);
    }

    if (!rdkb_dbus_dict_open_write(iter, &dict_iter)) {
    	wifi_util_error_print(WIFI_SUPP,"dbus: %s: failed to open message dict\r\n", __func__);
	return FALSE;
    }

    dbus_error_init(&error);
    if (!fill_dict_with_prop(&dict_iter, obj_desc->properties, interface, obj_desc->user_data, &error)) {
    	wifi_util_error_print(WIFI_SUPP,"dbus: %s: failed to get object properties: (%s) %s\r\n", __func__, 
		dbus_error_is_set(&error) ? error.name : "none",
		dbus_error_is_set(&error) ? error.message : "none");
	dbus_error_free(&error);
	rdkb_dbus_dict_close_write(iter, &dict_iter);
	return FALSE;
    }

    return rdkb_dbus_dict_close_write(iter, &dict_iter);
}

dbus_bool_t rdkb_set_changed_prop(
        const rdkb_dbus_wifi_obj_desc_t *obj_dsc, const char *interface,
        DBusMessageIter *dict_iter, int clear_changed)
{
        DBusMessageIter entry_iter;
        const rdkb_dbus_wifi_prop_desc_t *dsc;
        int i;
        DBusError error;

        for (dsc = obj_dsc->properties, i = 0; dsc && dsc->dbus_property;
             dsc++, i++) {
                if (obj_dsc->prop_changed_flags == NULL ||
                    !obj_dsc->prop_changed_flags[i])
                        continue;
                if (strcmp(dsc->dbus_interface, interface) != 0)
                        continue;
                if (clear_changed)
                        obj_dsc->prop_changed_flags[i] = 0;

                if (!dbus_message_iter_open_container(dict_iter,
                                                      DBUS_TYPE_DICT_ENTRY,
                                                      NULL, &entry_iter) ||
                    !dbus_message_iter_append_basic(&entry_iter,
                                                    DBUS_TYPE_STRING,
                                                    &dsc->dbus_property))
                        return FALSE;

                dbus_error_init(&error);
                if (!dsc->getter(dsc, &entry_iter, &error, obj_dsc->user_data))
                {
                        if (dbus_error_is_set(&error)) {
                        	wifi_util_error_print(WIFI_SUPP,"dbus: %s: Cannot get new value of property %s: (%s) %s\r\n",
                                           __func__, dsc->dbus_property,
                                           error.name, error.message);
                        } else {
                                wifi_util_error_print(WIFI_SUPP,"dbus: %s: Cannot get new value of property %s\r\n",
                                           __func__, dsc->dbus_property);
                        }
                        dbus_error_free(&error);
                        return FALSE;
                }

                if (!dbus_message_iter_close_container(dict_iter, &entry_iter))
                        return FALSE;
        }

        return TRUE;
}

static void rdkb_send_prop_changed_signal(
        DBusConnection *con, const char *path, const char *interface,
        const rdkb_dbus_wifi_obj_desc_t *obj_dsc)
{
        DBusMessage *msg;
        DBusMessageIter signal_iter, dict_iter;

        msg = dbus_message_new_signal(path, DBUS_INTERFACE_PROPERTIES,
                                      "PropertiesChanged");
        if (msg == NULL)
                return;

        dbus_message_iter_init_append(msg, &signal_iter);

        if (!dbus_message_iter_append_basic(&signal_iter, DBUS_TYPE_STRING,
                                            &interface) ||
            !dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY,
                                              "{sv}", &dict_iter) ||
            !rdkb_set_changed_prop(obj_dsc, interface, &dict_iter, 0) ||
            !dbus_message_iter_close_container(&signal_iter, &dict_iter) ||
            !dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY,
                                              "s", &dict_iter) ||
            !dbus_message_iter_close_container(&signal_iter, &dict_iter)) {
                wifi_util_error_print(WIFI_SUPP,"dbus: %s: Failed to construct signal\r\n",
                           __func__);
        } else {
                dbus_connection_send(con, msg, NULL);
        }

        dbus_message_unref(msg);
}

void rdkb_send_deprecated_prop_changed_signal(DBusConnection *con, const char *path, const char *interface, const struct wpa_dbus_object_desc *obj_dsc)
{
        DBusMessage *msg;
        DBusMessageIter signal_iter, dict_iter;

        msg = dbus_message_new_signal(path, interface, "PropertiesChanged");
        if (msg == NULL) {
		wifi_util_error_print(WIFI_SUPP,"%s:%d dbus msg new signal is NULL\r\n", __func__, __LINE__);
                return;
	}

        dbus_message_iter_init_append(msg, &signal_iter);

        if (!dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY,
                                              "{sv}", &dict_iter) ||
            !rdkb_set_changed_prop(obj_dsc, interface, &dict_iter, 1) ||
            !dbus_message_iter_close_container(&signal_iter, &dict_iter)) {
                wifi_util_error_print(WIFI_SUPP,"dbus: %s: Failed to construct signal\r\n",
                           __func__);
        } else {
                dbus_connection_send(con, msg, NULL);
        }

        dbus_message_unref(msg);
}

static void rdkb_send_prop_changed_sig(
        DBusConnection *con, const char *path, const char *interface,
        const rdkb_dbus_wifi_obj_desc_t *obj_dsc)
{
        rdkb_send_prop_changed_signal(con, path, interface, obj_dsc);
        rdkb_send_deprecated_prop_changed_signal(con, path, interface, obj_dsc);
}


void rdkb_dbus_flush_obj_changed_prop(DBusConnection *con,
                                              const char *path)
{
        rdkb_dbus_wifi_obj_desc_t *obj_desc = NULL;
        const rdkb_dbus_wifi_prop_desc_t *dsc;
        int i;

        dbus_connection_get_object_path_data(con, path, (void **) &obj_desc);
        if (!obj_desc) return;

        for (dsc = obj_desc->properties, i = 0; dsc && dsc->dbus_property;
             dsc++, i++) {
                if (obj_desc->prop_changed_flags == NULL ||
                    !obj_desc->prop_changed_flags[i])
                        continue;
                rdkb_send_prop_changed_sig(con, path, dsc->dbus_interface,
                                         obj_desc);
        }
}

void rdkb_dbus_signal_prop_changed(DBusConnection *connection, char *path, rdkb_dbus_wifi_prop_t property)
{
        char *prop;
        dbus_bool_t flush;
	rdkb_dbus_wifi_obj_desc_t *obj_desc = NULL;
	const rdkb_dbus_wifi_prop_desc_t *dsc;
	int i = 0;

        if (path == NULL ) {
                return;
	}

        flush = FALSE;
        switch (property) {
        case RDKB_DBUS_WIFI_PROP_AP_SCAN:
                prop = "ApScan";
                break;
        case RDKB_DBUS_WIFI_PROP_SCANNING:
                prop = "Scanning";
                break;
        case RDKB_DBUS_WIFI_PROP_STATE:
                prop = "State";
                break;
        case RDKB_DBUS_WIFI_PROP_CURRENT_BSS:
                prop = "CurrentBSS";
                break;
        case RDKB_DBUS_WIFI_PROP_CURRENT_NETWORK:
                prop = "CurrentNetwork";
                break;
        case RDKB_DBUS_WIFI_PROP_BSSS:
                prop = "BSSs";
                break;
        case RDKB_DBUS_WIFI_PROP_STATIONS:
                prop = "Stations";
                break;
        case RDKB_DBUS_WIFI_PROP_CURRENT_AUTH_MODE:
                prop = "CurrentAuthMode";
                break;
        case RDKB_DBUS_WIFI_PROP_DISCONNECT_REASON:
                prop = "DisconnectReason";
                flush = TRUE;
                break;
        case RDKB_DBUS_WIFI_PROP_AUTH_STATUS_CODE:
                prop = "AuthStatusCode";
                flush = TRUE;
                break;
        case RDKB_DBUS_WIFI_PROP_ASSOC_STATUS_CODE:
                prop = "AssocStatusCode";
                flush = TRUE;
                break;
        case RDKB_DBUS_WIFI_PROP_ROAM_TIME:
                prop = "RoamTime";
                break;
       case RDKB_DBUS_WIFI_PROP_ROAM_COMPLETE:
                prop = "RoamComplete";
                break;
        case RDKB_DBUS_WIFI_PROP_SESSION_LENGTH:
                prop = "SessionLength";
                break;
        case RDKB_DBUS_WIFI_PROP_BSS_TM_STATUS:
                prop = "BSSTMStatus";
                break;
        default:
                wifi_util_error_print(WIFI_SUPP,"dbus: %s: Unknown Property value %d\r\n",
                           __func__, property);
                return;
        }

	dbus_connection_get_object_path_data(connection, path, &obj_desc);

	for (dsc = obj_desc->properties; dsc && dsc->dbus_property; dsc++, i++)
                if (strcmp(prop, dsc->dbus_property) == 0 &&
                    strcmp(path, dsc->dbus_interface) == 0) {
                        if (obj_desc->prop_changed_flags)
                                obj_desc->prop_changed_flags[i] = 1;
                        break;
                }

	if (!dsc || !dsc->dbus_property) {
             wifi_util_error_print(WIFI_SUPP,"dbus: wpa_dbus_property_changed: no property:%d in object path:%s\r\n", property, path);
             return;
        }

        if (flush) {
             rdkb_dbus_flush_obj_changed_prop(connection, path);
        }
}

rdkb_wifi_bss_t * rdkb_bss_get(rdkb_wifi_supp_param_t *wpa_s, const uint8_t *bssid, const uint8_t *ssid, size_t ssid_len)
{
        rdkb_wifi_bss_t *bss;
        circular_list_for_each(bss, &wpa_s->bss, rdkb_wifi_bss_t, list) {
		if(memcmp(bss->bssid, bssid, ETH_ALEN) == 0 &&
		    bss->ssid_len == ssid_len &&
		    memcmp(bss->ssid, ssid, ssid_len) == 0) {
                        return bss;
		}
        }
        return NULL;
}

rdkb_wifi_bss_t * rdkb_bss_get_id(rdkb_wifi_supp_param_t *wpa_s, unsigned int id)
{
        rdkb_wifi_bss_t *bss;
        circular_list_for_each(bss, &wpa_s->bss, rdkb_wifi_bss_t, list) {
                if (bss->id == id)
                        return bss;
        }
        return NULL;
}

static rdkb_wifi_bss_t * rdkb_get_bss_helper(struct bss_handler_args *args,
                                       DBusError *error, const char *func_name)
{
        rdkb_wifi_bss_t *res = rdkb_bss_get_id(args->wpa_s, args->id);

        if (!res) {
                wifi_util_error_print(WIFI_SUPP,"%s[dbus]: no bss with id %d found\r\n",
                           func_name, args->id);
                dbus_set_error(error, DBUS_ERROR_FAILED,
                               "%s: BSS %d not found",
                               func_name, args->id);
        }

        return res;
}

static inline const uint8_t * rdkb_bss_ie_ptr(const rdkb_wifi_bss_t *bss)
{
        return bss->ies;
}

const uint8_t * rdkb_bss_get_ie(const rdkb_wifi_bss_t *bss, uint8_t ie)
{
	return rdkb_bss_ie_ptr(bss); 
}

int rdkb_bss_get_bit_rates(const rdkb_wifi_bss_t *bss, uint8_t **rates)
{
        const uint8_t *ie, *ie2;
        int i, j;
        unsigned int len;
        uint8_t *r;

        ie = rdkb_bss_get_ie(bss, WLAN_EID_SUPP_RATES);
        ie2 = rdkb_bss_get_ie(bss, WLAN_EID_EXT_SUPP_RATES);

        len = (ie ? ie[1] : 0) + (ie2 ? ie2[1] : 0);

        r = malloc(len);
        if (!r)
                return -1;

        for (i = 0; ie && i < ie[1]; i++)
                r[i] = ie[i + 2] & 0x7f;

        for (j = 0; ie2 && j < ie2[1]; j++)
                r[i + j] = ie2[j + 2] & 0x7f;

        *rates = r;
        return len;
}

static dbus_bool_t rdkb_dbus_get_bss_sec_prop(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, struct wpa_ie_data *ie_data, DBusError *error)
{
        DBusMessageIter iter_dict, variant_iter;
        const char *group;
        const char *pairwise[5]; /* max 5 pairwise ciphers is supported */
        const char *key_mgmt[16]; /* max 16 key managements may be supported */
        int n;

        if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                                              "a{sv}", &variant_iter))
                goto nomem;

        if (!rdkb_dbus_dict_open_write(&variant_iter, &iter_dict))
                goto nomem;

        n = 0;
        if (ie_data->key_mgmt & WPA_KEY_MGMT_PSK)
                key_mgmt[n++] = "wpa-psk";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_PSK)
                key_mgmt[n++] = "wpa-ft-psk";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_PSK_SHA256)
                key_mgmt[n++] = "wpa-psk-sha256";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X)
                key_mgmt[n++] = "wpa-eap";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_IEEE8021X)
                key_mgmt[n++] = "wpa-ft-eap";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SHA256)
                key_mgmt[n++] = "wpa-eap-sha256";
#ifdef CONFIG_SUITEB
        if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B)
                key_mgmt[n++] = "wpa-eap-suite-b";
#endif /* CONFIG_SUITEB */
#ifdef CONFIG_SUITEB192
        if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B_192)
                key_mgmt[n++] = "wpa-eap-suite-b-192";
#endif /* CONFIG_SUITEB192 */
#ifdef CONFIG_FILS
        if (ie_data->key_mgmt & WPA_KEY_MGMT_FILS_SHA256)
                key_mgmt[n++] = "wpa-fils-sha256";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_FILS_SHA384)
                key_mgmt[n++] = "wpa-fils-sha384";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA256)
                key_mgmt[n++] = "wpa-ft-fils-sha256";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA384)
                key_mgmt[n++] = "wpa-ft-fils-sha384";
#endif /* CONFIG_FILS */
#ifdef CONFIG_SAE
        if (ie_data->key_mgmt & WPA_KEY_MGMT_SAE)
                key_mgmt[n++] = "sae";
        if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_SAE)
                key_mgmt[n++] = "ft-sae";
#endif /* CONFIG_SAE */
#ifdef CONFIG_OWE
        if (ie_data->key_mgmt & WPA_KEY_MGMT_OWE)
                key_mgmt[n++] = "owe";
#endif /* CONFIG_OWE */
        if (ie_data->key_mgmt & WPA_KEY_MGMT_NONE)
                key_mgmt[n++] = "wpa-none";

        if (!rdkb_dbus_dict_append_str_array(&iter_dict, "KeyMgmt",
                                               key_mgmt, n))
                goto nomem;

        switch (ie_data->group_cipher) {
#ifdef CONFIG_WEP
        case WPA_CIPHER_WEP40:
                group = "wep40";
                break;
        case WPA_CIPHER_WEP104:
                group = "wep104";
                break;
#endif /* CONFIG_WEP */
#ifndef CONFIG_NO_TKIP
        case WPA_CIPHER_TKIP:
                group = "tkip";
                break;
#endif /* CONFIG_NO_TKIP */
        case WPA_CIPHER_CCMP:
                group = "ccmp";
                break;
        case WPA_CIPHER_GCMP:
                group = "gcmp";
                break;
        case WPA_CIPHER_CCMP_256:
                group = "ccmp-256";
                break;
        case WPA_CIPHER_GCMP_256:
                group = "gcmp-256";
                break;
        default:
                group = "";
                break;
        }

        if (!rdkb_dbus_dict_append_str(&iter_dict, "Group", group))
                goto nomem;

        n = 0;
#ifndef CONFIG_NO_TKIP
        if (ie_data->pairwise_cipher & WPA_CIPHER_TKIP)
                pairwise[n++] = "tkip";
#endif /* CONFIG_NO_TKIP */
        if (ie_data->pairwise_cipher & WPA_CIPHER_CCMP)
                pairwise[n++] = "ccmp";
        if (ie_data->pairwise_cipher & WPA_CIPHER_GCMP)
                pairwise[n++] = "gcmp";
        if (ie_data->pairwise_cipher & WPA_CIPHER_CCMP_256)
                pairwise[n++] = "ccmp-256";
        if (ie_data->pairwise_cipher & WPA_CIPHER_GCMP_256)
                pairwise[n++] = "gcmp-256";

        if (!rdkb_dbus_dict_append_str_array(&iter_dict, "Pairwise",
                                               pairwise, n))
                goto nomem;

        if (ie_data->proto == WPA_PROTO_RSN) {
                switch (ie_data->mgmt_group_cipher) {
                case WPA_CIPHER_AES_128_CMAC:
                        group = "aes128cmac";
                        break;
                default:
                        group = "";
                        break;
                }

                if (!rdkb_dbus_dict_append_str(&iter_dict, "MgmtGroup",
                                                 group))
                        goto nomem;
        }

        if (!rdkb_dbus_dict_close_write(&variant_iter, &iter_dict) ||
            !dbus_message_iter_close_container(iter, &variant_iter))
                goto nomem;

        return TRUE;

nomem:
        dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
        return FALSE;
}

dbus_bool_t rdkb_dbus_getter_bss_rsn(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;
        struct wpa_ie_data wpa_data;
        const uint8_t *ie;

        res = rdkb_get_bss_helper(args, error, __func__);
        if (!res)
                return FALSE;

        os_memset(&wpa_data, 0, sizeof(wpa_data));
        ie = rdkb_bss_get_ie(res, WLAN_EID_RSN);

	if (ie && res->ie_len != 0 && wpa_parse_wpa_ie_rsn(ie, 2 + ie[1], &wpa_data) < 0) {
                dbus_set_error_const(error, DBUS_ERROR_FAILED,
                                     "failed to parse RSN IE");
                return FALSE;
        }

        return rdkb_dbus_get_bss_sec_prop(property_desc, iter, &wpa_data, error);
}

static int cmp_unsigned_char(const void *a, const void *b)
{
        return (*(uint8_t *) b - *(uint8_t *) a);
}

dbus_bool_t rdkb_dbus_getter_bss_rates(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;
        uint8_t *ie_rates = NULL;
        uint32_t *real_rates;
        int rates_num, i;
        dbus_bool_t success = FALSE;

        res = rdkb_get_bss_helper(args, error, __func__);
        if (!res)
                return FALSE;

        rates_num = rdkb_bss_get_bit_rates(res, &ie_rates);
        if (rates_num < 0)
                return FALSE;

        qsort(ie_rates, rates_num, 1, cmp_unsigned_char);

        real_rates = malloc(sizeof(uint32_t) * rates_num);
        if (!real_rates) {
                free(ie_rates);
                dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
                return FALSE;
        }

        for (i = 0; i < rates_num; i++)
                real_rates[i] = ie_rates[i] * 500000;

        success = rdkb_dbus_simple_array_prop_getter(iter, DBUS_TYPE_UINT32,
                                                         real_rates, rates_num,
                                                         error);

        free(ie_rates);
        free(real_rates);
        return success;
}

dbus_bool_t rdkb_dbus_getter_bss_freq(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;
        uint16_t freq;

        res = rdkb_get_bss_helper(args, error, __func__);
        if (!res)
                return FALSE;

        freq = (uint16_t) res->freq;
        return rdkb_dbus_simple_prop_getter(iter, DBUS_TYPE_UINT16,
                                                &freq, error);
}

dbus_bool_t rdkb_dbus_getter_bss_signal(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;
        int16_t level;

        res = rdkb_get_bss_helper(args, error, __func__);
        if (!res)
                return FALSE;

        level = (int16_t) res->level;
        return rdkb_dbus_simple_prop_getter(iter, DBUS_TYPE_INT16,
                                                &level, error);
}

dbus_bool_t rdkb_dbus_getter_bss_mode(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;
        const char *mode;
        const uint8_t *mesh;

        res = rdkb_get_bss_helper(args, error, __func__);
        if (!res) return FALSE;

        return rdkb_dbus_simple_prop_getter(iter, DBUS_TYPE_STRING,
                                                &mode, error);
}

dbus_bool_t rdkb_dbus_getter_bss_privacy(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{            
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;
        dbus_bool_t privacy;
    
        res = rdkb_get_bss_helper(args, error, __func__);
        if (!res)
                return FALSE;
    
        privacy = (res->caps & IEEE80211_CAP_PRIVACY) ? TRUE : FALSE;
        return rdkb_dbus_simple_prop_getter(iter, DBUS_TYPE_BOOLEAN,
                                                &privacy, error);
}

dbus_bool_t rdkb_dbus_getter_bss_ssid(
        const rdkb_dbus_wifi_prop_desc_t *property_desc,
        DBusMessageIter *iter, DBusError *error, void *user_data)
{
        struct bss_handler_args *args = user_data;
        rdkb_wifi_bss_t *res;

	res = rdkb_get_bss_helper(args, error, __func__);
	if (!res) return FALSE;

        return rdkb_dbus_simple_array_prop_getter(iter, DBUS_TYPE_BYTE,
						res->ssid, res->ssid_len,
                                                error);
}

static dbus_bool_t fill_dict_with_prop(
        DBusMessageIter *dict_iter,
        const rdkb_dbus_wifi_prop_desc_t *props,
        const char *interface, void *user_data, DBusError *error)
{
        DBusMessageIter entry_iter;
        const rdkb_dbus_wifi_prop_desc_t *dsc;

        for (dsc = props; dsc && dsc->dbus_property; dsc++) {
		wifi_util_dbg_print(WIFI_SUPP,"dbus_property:%s, dbus_interface:%s, type:%s\n", dsc->dbus_property, dsc->dbus_interface, dsc->type);
                if (strncmp(dsc->dbus_interface, interface,
                               RDKB_DBUS_INTERFACE_MAX) != 0)
                        continue;

                if (dsc->getter == NULL)
                        continue;

                if (!dbus_message_iter_open_container(dict_iter,
                                                      DBUS_TYPE_DICT_ENTRY,
                                                      NULL, &entry_iter) ||
                    !dbus_message_iter_append_basic(&entry_iter,
                                                    DBUS_TYPE_STRING,
                                                    &dsc->dbus_property))
                        goto error;

                if (!dsc->getter(dsc, &entry_iter, error, user_data)) {
                        wifi_util_error_print(WIFI_SUPP,
                                   "dbus: %s dbus_interface=%s dbus_property=%s getter failed\r\n",
                                   __func__, dsc->dbus_interface,
                                   dsc->dbus_property);
                        return FALSE;
                }

                if (!dbus_message_iter_close_container(dict_iter, &entry_iter))
                        goto error;
        }
        return TRUE;

error:
        dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
        return FALSE;
}

static DBusMessage * rdkb_get_all_wifi_prop(DBusMessage *message, char *interface,
                                        rdkb_dbus_wifi_obj_desc_t *obj_dsc)
{
        DBusMessage *reply;
        DBusMessageIter iter, dict_iter;
        DBusError error;

        reply = dbus_message_new_method_return(message);
        if (reply == NULL)
                return rdkb_dbus_error_no_memory(message);

        dbus_message_iter_init_append(reply, &iter);
        if (!rdkb_dbus_dict_open_write(&iter, &dict_iter)) {
                dbus_message_unref(reply);
                return rdkb_dbus_error_no_memory(message);
        }

        dbus_error_init(&error);
        if (!fill_dict_with_prop(&dict_iter, obj_dsc->properties,
                                       interface, obj_dsc->user_data, &error)) {
                rdkb_dbus_dict_close_write(&iter, &dict_iter);
                dbus_message_unref(reply);
                reply = rdkb_dbus_reply_new_from_error(
                        message, &error, DBUS_ERROR_INVALID_ARGS,
                        "No readable properties in this interface");
                dbus_error_free(&error);
                return reply;
        }

        if (!rdkb_dbus_dict_close_write(&iter, &dict_iter)) {
                dbus_message_unref(reply);
                return rdkb_dbus_error_no_memory(message);
        }

        return reply;
}

static DBusMessage * rdkb_wifi_prop_get_all(DBusMessage *message, char *interface,
                                        rdkb_dbus_wifi_obj_desc_t *obj_dsc)
{
        if (strcmp(dbus_message_get_signature(message), "s") != 0)
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
                                              NULL);

        return rdkb_get_all_wifi_prop(message, interface, obj_dsc);
}
static DBusMessage * rdkb_wifi_prop_get(DBusMessage *message,
                                    const rdkb_dbus_wifi_prop_desc_t *dsc,
                                    void *user_data)
{
        DBusMessage *reply;
        DBusMessageIter iter;
        DBusError error;

        if (os_strcmp(dbus_message_get_signature(message), "ss")) {
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
                                              NULL);
        }

        if (dsc->getter == NULL) {
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
                                              "Property is write-only");
        }

        reply = dbus_message_new_method_return(message);
        dbus_message_iter_init_append(reply, &iter);

        dbus_error_init(&error);
        if (dsc->getter(dsc, &iter, &error, user_data) == FALSE) {
                dbus_message_unref(reply);
                reply = rdkb_dbus_reply_new_from_error(
                        message, &error, DBUS_ERROR_FAILED,
                        "Failed to read property");
                dbus_error_free(&error);
        }

        return reply;
}


static DBusMessage * rdkb_wifi_prop_set(DBusMessage *message,
                                    const rdkb_dbus_wifi_prop_desc_t *dsc,
                                    void *user_data)
{
        DBusMessage *reply;
        DBusMessageIter iter;
        DBusError error;

        if (os_strcmp(dbus_message_get_signature(message), "ssv")) {
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
                                              NULL);
        }

        if (dsc->setter == NULL) {
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
                                              "Property is read-only");
        }

        dbus_message_iter_init(message, &iter);
        dbus_message_iter_next(&iter);
        dbus_message_iter_next(&iter);

        dbus_error_init(&error);
        if (dsc->setter(dsc, &iter, &error, user_data) == TRUE) {
                reply = dbus_message_new_method_return(message);
        } else {
                reply = rdkb_dbus_reply_new_from_error(
                        message, &error, DBUS_ERROR_FAILED,
                        "Failed to set property");
                dbus_error_free(&error);
        }

        return reply;
}

static DBusMessage *
rdkb_wifi_prop_get_or_set(DBusMessage *message, DBusMessageIter *iter,
                      char *interface,
                      rdkb_dbus_wifi_obj_desc_t *obj_dsc)
{
        const rdkb_dbus_wifi_prop_desc_t *property_dsc;
        char *property;
        const char *method;

        method = dbus_message_get_member(message);
        property_dsc = obj_dsc->properties;

        if (!dbus_message_iter_next(iter) ||
            dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING) {
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
                                              NULL);
        }
        dbus_message_iter_get_basic(iter, &property);

        while (property_dsc && property_dsc->dbus_property) {
        	wifi_util_dbg_print(WIFI_SUPP,"Loop for property handler for %s.%s on %s, property_dsc->dbus_property:%s\n",
                           interface, property,
                           dbus_message_get_path(message), property_dsc->dbus_property);
                if (!os_strncmp(property_dsc->dbus_property, property,
                                RDKB_DBUS_METHOD_SIGNAL_PROP_MAX) &&
                    !os_strncmp(property_dsc->dbus_interface, interface,
                                RDKB_DBUS_INTERFACE_MAX))
                        break;

                property_dsc++;
        }
        if (property_dsc == NULL || property_dsc->dbus_property == NULL) {
                wifi_util_error_print(WIFI_SUPP,"no property handler for %s.%s on %s\r\n",
                           interface, property,
                           dbus_message_get_path(message));
                return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
                                              "No such property");
        }

        if (os_strncmp(RDKB_DBUS_PROP_GET, method,
                       RDKB_DBUS_METHOD_SIGNAL_PROP_MAX) == 0) {
                wifi_util_dbg_print(WIFI_SUPP,"%s: Get(%s)\r\n", __func__, property);
                return rdkb_wifi_prop_get(message, property_dsc,
                                      obj_dsc->user_data);
        }

        wifi_util_dbg_print(WIFI_SUPP,"%s: Set(%s)\r\n", __func__, property);
        return rdkb_wifi_prop_set(message, property_dsc, obj_dsc->user_data);
}

DBusMessage *process_prop_msg_handler(DBusMessage *message, rdkb_dbus_wifi_obj_desc_t *obj_dsc) 
{
        DBusMessageIter iter;
        char *interface;
        const char *method;

        method = dbus_message_get_member(message);
        dbus_message_iter_init(message, &iter);

        printf("======>property handler for %s\r\n", method);

        if (!strncmp(RDKB_DBUS_PROP_GET, method,
                        RDKB_DBUS_METHOD_SIGNAL_PROP_MAX) ||
            !strncmp(RDKB_DBUS_PROP_SET, method,
                        RDKB_DBUS_METHOD_SIGNAL_PROP_MAX) ||
            !strncmp(RDKB_DBUS_PROP_GETALL, method,
                        RDKB_DBUS_METHOD_SIGNAL_PROP_MAX)) {
                if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
                        return dbus_message_new_error(message,
                                                      DBUS_ERROR_INVALID_ARGS,
                                                      NULL);
                }    

                dbus_message_iter_get_basic(&iter, &interface);

                if (!strncmp(RDKB_DBUS_PROP_GETALL, method,
                                RDKB_DBUS_METHOD_SIGNAL_PROP_MAX)) {
                        return rdkb_wifi_prop_get_all(message, interface, obj_dsc);
                }    
                return rdkb_wifi_prop_get_or_set(message, &iter, interface, obj_dsc);
        } else {
            printf("%s:%d unknown properties:%s\r\n", __func__, __LINE__, method);
        }
        return dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_METHOD, NULL);

}


static void rdkb_wifi_bss_copy_res(rdkb_wifi_bss_t *dst, rdkb_wifi_bss_t *src)
{
        dst->flags = src->flags;
        memcpy(dst->bssid, src->bssid, ETH_ALEN);
        dst->freq = src->freq;
        dst->beacon_int = src->beacon_int;
        dst->caps = src->caps;
        dst->qual = src->qual;
        dst->noise = src->noise;
        dst->level = src->level;
        dst->tsf = src->tsf;
        dst->beacon_newer = src->beacon_newer;
        dst->est_throughput = src->est_throughput;
        dst->snr = src->snr;

        memcpy(&dst->scan_bss_info.external_ap, &src->scan_bss_info.external_ap, sizeof(src->scan_bss_info.external_ap));
	dst->scan_bss_info.vap_index = src->scan_bss_info.vap_index;
	dst->scan_bss_info.radio_index = src->scan_bss_info.radio_index;
}

static rdkb_wifi_bss_t * rdkb_wifi_bss_add(rdkb_wifi_supp_param_t *p_supp_obj,
                                    const uint8_t *ssid, size_t ssid_len,
                                    rdkb_wifi_bss_t *res, int bss_id)
{
        rdkb_wifi_bss_t *bss = malloc(sizeof(*bss));

        if (bss == NULL) {
	    perror("malloc failed\n");
            return NULL;
	}
	memset(bss, 0, sizeof(rdkb_wifi_bss_t));

        bss->id = bss_id;
        rdkb_wifi_bss_copy_res(bss, res);
        strcpy(bss->ssid, ssid);
        bss->ssid_len = ssid_len;
        bss->ie_len = res->ie_len;
        memcpy(bss->ies, res->ies, res->ie_len);

        circular_list_add_tail(&p_supp_obj->bss, &bss->list);

        p_supp_obj->num_bss++;

	wifi_util_info_print(WIFI_SUPP,"rdkb_wifi_bss_add to add new SSID:%s: %d\n", ssid, p_supp_obj->num_bss);

        return bss;
}

static int are_ies_equal(const rdkb_wifi_bss_t *old,
                         const rdkb_wifi_bss_t *new_res, uint32_t ie)
{
    const uint8_t *old_ie, *new_ie;
    int new_ie_len, old_ie_len, ret;

    switch (ie) {
        case WLAN_EID_RSN:
        case WLAN_EID_SUPP_RATES:
        case WLAN_EID_EXT_SUPP_RATES:
            old_ie = rdkb_bss_get_ie(old, ie);
            new_ie = rdkb_bss_get_ie(new_res, ie);
            break;
        default:
            wifi_util_info_print(WIFI_SUPP,"bss: %s: cannot compare IEs\r\n", __func__);
            return 0;
    }

    /* in case of single IE */
    old_ie_len = old_ie ? old_ie[1] + 2 : 0;
    new_ie_len = new_ie ? new_ie[1] + 2 : 0;

    if (!old_ie || !new_ie) {
        ret = !old_ie && !new_ie;
    } else {
        ret = (old_ie_len == new_ie_len &&
            memcmp(old_ie, new_ie, old_ie_len) == 0);
    }

    return ret;
}

static uint32_t bss_compare_res(const rdkb_wifi_bss_t *old, const rdkb_wifi_bss_t *new_res)
{
    uint32_t changes = 0;
    int caps_diff = old->caps ^ new_res->caps;

    if (old->freq != new_res->freq) {
        changes |= SUPP_FREQ_CHANGED_FLAG;
    }

    if (old->level != new_res->level) {
        changes |= SUPP_SIGNAL_CHANGED_FLAG;
    }
	
    if (caps_diff & IEEE80211_CAP_PRIVACY) {
        changes |= SUPP_PRIVACY_CHANGED_FLAG;
    }
	
    if (caps_diff & IEEE80211_CAP_IBSS) {
        changes |= SUPP_MODE_CHANGED_FLAG;
    }

    if (old->ie_len == new_res->ie_len &&
        memcmp(rdkb_bss_ie_ptr(old), rdkb_bss_ie_ptr(new_res), old->ie_len) == 0) {
        return changes;
    }

    changes |= SUPP_IES_CHANGED_FLAG;

    if (!are_ies_equal(old, new_res, WLAN_EID_RSN)) {
        changes |= SUPP_RSNIE_CHANGED_FLAG;
    }

    return changes;
}

static void recursive_flush_changed_properties(DBusConnection *con, const char *path)
{
    char **objects = NULL;
    char subobj_path[RDKB_DBUS_OBJ_PATH_MAX];
    int i;

    rdkb_dbus_flush_obj_changed_prop(con, path);

    if (!dbus_connection_list_registered(con, path, &objects)) {
        goto out;
    }

    for (i = 0; objects[i]; i++) {
        snprintf(subobj_path, RDKB_DBUS_OBJ_PATH_MAX,
                            "%s/%s", path, objects[i]);
        recursive_flush_changed_properties(con, subobj_path);
    }

out:
    dbus_free_string_array(objects);
}

void rdkb_dbus_flush_all_changed_properties(DBusConnection *con)
{
    recursive_flush_changed_properties(con, RDKB_DBUS_OBJ_PATH);
}

void rdkb_dbus_mark_property_changed(DBusConnection *dbus_connection,
                                    const char *path, const char *interface,
                                    const char *property)
{
    rdkb_dbus_wifi_obj_desc_t *obj_desc = NULL;
    const rdkb_dbus_wifi_prop_desc_t *dsc;
    int i = 0;

    if (dbus_connection == NULL) {
        return;
    }

    dbus_connection_get_object_path_data(dbus_connection, path, (void **) &obj_desc);
    if (!obj_desc) {
        wifi_util_error_print(WIFI_SUPP, "dbus: rdkb_dbus_property_changed:"
            " could not obtain object's private data: %s", path);
        return;
    }
        
    for (dsc = obj_desc->properties; dsc && dsc->dbus_property; dsc++, i++) {
        if (strcmp(property, dsc->dbus_property) == 0 &&
            strcmp(interface, dsc->dbus_interface) == 0) {
            wifi_util_dbg_print(WIFI_SUPP,"%s:%d property:%s interface:%s\r\n", __func__, __LINE__, property, interface);
            if (obj_desc->prop_changed_flags)
                obj_desc->prop_changed_flags[i] = 1;
                break;
            }
    }

    if (!dsc || !dsc->dbus_property) {
        wifi_util_dbg_print(WIFI_SUPP,"dbus: wpa_dbus_property_changed:"
            " no property %s in object %s\r\n", property, path);
            return;
    }
}

void rdkb_dbus_bss_signal_prop_changed(rdkb_wifi_supp_param_t *wpa_s,
                                       rdkb_dbus_bss_prop_t property,
                                       uint32_t id)
{
    char path[RDKB_DBUS_OBJ_PATH_MAX];
    char *prop;                 

    switch (property) {
        case rdkb_dbus_bss_prop_signal: {
            prop = "Signal";
            break;
        }
        case rdkb_dbus_bss_prop_freq: {
            prop = "Frequency";
            break;
        }
        case rdkb_dbus_bss_prop_mode: {
            prop = "Mode";
            break;
        }
        case rdkb_dbus_bss_prop_privacy: {
            prop = "Privacy";
            break;
        }
        case rdkb_dbus_bss_prop_rates: {
            prop = "Rates";
            break;
        }
        case rdkb_dbus_bss_prop_wpa: {
            prop = "WPA";
            break;
        }
        case rdkb_dbus_bss_prop_rsn: {
            prop = "RSN";
            break;
        }
        case rdkb_dbus_bss_prop_wps: {
            prop = "WPS";
            break;
        }
        case rdkb_dbus_bss_prop_ies: {
            prop = "IEs";
            break;
        }
        case rdkb_dbus_bss_prop_age: {
            prop = "Age";
            break;
        }
        default: {
            wifi_util_error_print(WIFI_SUPP,"dbus: %s: Unknown Property value %d",
                           __func__, property);
            return;
        }
    }

    snprintf(path, RDKB_DBUS_OBJ_PATH_MAX, "%s/" RDKB_DBUS_NEW_BSSIDS_PART "/%u",
                    wpa_s->dbus_new_path, id);

    wifi_util_dbg_print(WIFI_SUPP,"dbus: %s: prop:%s path:%s\r\n", __func__, prop, path);
    rdkb_dbus_mark_property_changed(wpa_s->dbus_connection, path,
                                       RDKB_DBUS_NEW_IFACE_BSS, prop);
}

void notify_bss_prop_changed(rdkb_wifi_supp_param_t *wpa_s, uint32_t id, rdkb_dbus_bss_prop_t bss_prop_type)
{
    rdkb_dbus_bss_signal_prop_changed(wpa_s, bss_prop_type, id);
}

void notify_wifi_bss_changes(rdkb_wifi_supp_param_t *wpa_s, uint32_t changes, const rdkb_wifi_bss_t *bss)
{
    if (changes & SUPP_FREQ_CHANGED_FLAG) {
        notify_bss_prop_changed(wpa_s, bss->id, rdkb_dbus_bss_prop_freq);
    }

    if (changes & SUPP_SIGNAL_CHANGED_FLAG) {
        notify_bss_prop_changed(wpa_s, bss->id, rdkb_dbus_bss_prop_signal);
    }

    if (changes & SUPP_PRIVACY_CHANGED_FLAG) {
        notify_bss_prop_changed(wpa_s, bss->id, rdkb_dbus_bss_prop_privacy);
    }

    if (changes & SUPP_MODE_CHANGED_FLAG) {
        notify_bss_prop_changed(wpa_s, bss->id, rdkb_dbus_bss_prop_mode);
    }

    if (changes & SUPP_RSNIE_CHANGED_FLAG) {
        notify_bss_prop_changed(wpa_s, bss->id, rdkb_dbus_bss_prop_rsn);
    }
}

static rdkb_wifi_bss_t * rdkb_wifi_bss_update(rdkb_wifi_supp_param_t *p_supp_obj, rdkb_wifi_bss_t *bss, rdkb_wifi_bss_t *res)
{
    uint32_t changes;
    wifi_util_dbg_print(WIFI_SUPP,"UPDATE SCAN RESULT: %s:%d\n", __func__, __LINE__);

    changes = bss_compare_res(bss, res);
    rdkb_wifi_bss_copy_res(bss, res);

    if (changes & SUPP_IES_CHANGED_FLAG) {
        bss->ie_len = res->ie_len;
        memcpy(bss->ies, res->ies, res->ie_len);
    }

    notify_wifi_bss_changes(p_supp_obj, changes, bss);
    return bss;
}

void update_wifi_scan_results(rdkb_wifi_supp_param_t *p_supp_obj, rdkb_wifi_bss_t *res) {
    rdkb_wifi_bss_t *bss;
    int bss_id = p_supp_obj->num_bss;

    wifi_util_dbg_print(WIFI_SUPP,"%s:%d: p_supp_obj:%p, ssid:%s\n", __func__, __LINE__, p_supp_obj, res->ssid);

    bss = rdkb_bss_get(p_supp_obj, res->bssid, res->ssid, strlen(res->ssid));
    if (bss == NULL) {
        bss = rdkb_wifi_bss_add(p_supp_obj, res->ssid, strlen(res->ssid), res, bss_id);
	//rdkb_dbus_reg_bss(res, bss_id);
	rdkb_dbus_reg_bss(p_supp_obj, bss_id);
    } else {
	rdkb_wifi_bss_update(p_supp_obj, bss, res);
    }
}

int rdkb_dbus_reg_bss(rdkb_wifi_supp_param_t *wpa_s, uint32_t bss_id) {

    DBusMessageIter iter;
    char bss_obj_path[RDKB_DBUS_OBJ_PATH_MAX];
    struct bss_handler_args *arg;
    char temp_dbus_obj_name[32] = { 0 };
    char key_buff[32] = { 0 };

    snprintf(bss_obj_path, RDKB_DBUS_OBJ_PATH_MAX, "%s/" RDKB_DBUS_NEW_BSSIDS_PART "/%u", RDKB_DBUS_NEW_INTERFACE_PATH, bss_id);

    arg = (struct bss_handler_args *) malloc(sizeof(struct bss_handler_args));
    if (arg == NULL) {
        wifi_util_error_print(WIFI_SUPP,"%s:%d bss:%d handler arg malloc is falied\n", __func__, __LINE__, bss_id);
        return RETURN_ERR;
    }
    arg->wpa_s = wpa_s;
    arg->id = bss_id;

    if (wpa_s->bss_handler_args_map != NULL) {
        snprintf(key_buff, sizeof(key_buff), "bss_%d_reg", bss_id);
        hash_map_put(wpa_s->bss_handler_args_map, strdup(key_buff), arg);
    } else {
        wifi_util_error_print(WIFI_SUPP,"%s:%d bss handler arg map is NULL\n", __func__, __LINE__);
    }

    wifi_util_dbg_print(WIFI_SUPP,"arg.wpa_s:%p, id:%d\n", arg->wpa_s, arg->id);

    generate_bss_dbus_obj_name(temp_dbus_obj_name, sizeof(temp_dbus_obj_name), bss_id);

    rdkb_dbus_wifi_obj_desc_t *wpa_obj_desc =  rdkb_dbus_obj_init_desc(temp_dbus_obj_name, bss_obj_path, arg, NULL, NULL, rdkb_dbus_wifi_bss_prop, rdkb_dbus_wifi_bss_signals);

    rdkb_dbus_reg_obj_per_iface(bss_obj_path, wpa_obj_desc);
    rdkb_dbus_signal_process(RDKB_DBUS_NEW_INTERFACE, RDKB_DBUS_NEW_IFACE_BSS, RDKB_DBUS_NEW_INTERFACE_PATH,
    	RDKB_DBUS_SERVICE_NAME, "BSSAdded", TRUE, wpa_obj_desc->connection, bss_obj_path);
    
    rdkb_dbus_signal_prop_changed(wpa_obj_desc->connection, RDKB_DBUS_NEW_INTERFACE_PATH, RDKB_DBUS_WIFI_PROP_BSSS);

    return RETURN_OK;
}

DBusHandlerResult rdkb_dbus_message_handler(DBusConnection *connection,
                                        DBusMessage *message, void *user_data)
{
    DBusMessage *reply;
    const char *msg_interface;
    const char *method;
    const char *path;
    rdkb_dbus_wifi_obj_desc_t *obj_desc_user_data = NULL;

    obj_desc_user_data = (rdkb_dbus_wifi_obj_desc_t *)user_data;

    method = dbus_message_get_member(message);
    path = dbus_message_get_path(message);
    msg_interface = dbus_message_get_interface(message);

    if (!strncmp(RDKB_DBUS_PROP_INTERFACE, msg_interface, RDKB_DBUS_INTERFACE_MAX)) {
    	wifi_util_dbg_print(WIFI_SUPP,"%s():%d: dbus_prop: %s.%s (%s) [%s]\r\n", __func__, __LINE__,
	   msg_interface, method, path,
	   dbus_message_get_signature(message));

        reply = process_prop_msg_handler(message, obj_desc_user_data);

    } else {
    	wifi_util_dbg_print(WIFI_SUPP,"%s():%d: dbus_method: %s.  %s (%s) [%s]\r\n", __func__, __LINE__,
	   msg_interface, method, path,
	   dbus_message_get_signature(message));

        reply = process_msg_method_handler(message, obj_desc_user_data);
    }

    if (!reply) {
        reply = dbus_message_new_method_return(message);
    } else {
        if (!dbus_message_get_no_reply(message)) {
            wifi_util_dbg_print(WIFI_SUPP,"IN %s():%d calling dbus_connection_send\n", __func__, __LINE__);
            dbus_connection_send(connection, reply, NULL);
        }
        dbus_message_unref(reply);
    }

    rdkb_dbus_flush_all_changed_properties(connection);
    return DBUS_HANDLER_RESULT_HANDLED;
}

int rdkb_dbus_obj_init(rdkb_dbus_wifi_obj_desc_t *p_obj_desc, void *p_user_data)
{
    DBusError error;

    DBusObjectPathVTable vtable = {
        .message_function = rdkb_dbus_message_handler,
    };

    dbus_error_init(&error);

    p_obj_desc->user_data = p_user_data;
    p_obj_desc->user_data_free_func = NULL;
    p_obj_desc->methods = rdkb_dbus_global_wifi_methods;
    p_obj_desc->properties = rdkb_dbus_global_wifi_prop;
    p_obj_desc->signals = rdkb_dbus_global_wifi_signals;
    p_obj_desc->path = RDKB_DBUS_OBJ_PATH;

    wifi_util_info_print(WIFI_SUPP,"%s:%d DBUS service start\n", __func__, __LINE__);

    p_obj_desc->connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (dbus_error_is_set(&error)) {
        wifi_util_error_print(WIFI_SUPP, "%s:%d: dbus: Could not acquire the system bus: %s - %s", __func__, __LINE__, error.name, error.message);
	dbus_error_free(&error);
    }

    if (!dbus_connection_register_object_path(p_obj_desc->connection, RDKB_DBUS_OBJ_PATH, &vtable, p_obj_desc)) {
        wifi_util_error_print(WIFI_SUPP,"Failed to register object path\n");
        return RETURN_ERR;
    }

    if (dbus_bus_request_name(p_obj_desc->connection, RDKB_DBUS_SERVICE_NAME, DBUS_NAME_FLAG_REPLACE_EXISTING, &error) != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        wifi_util_error_print(WIFI_SUPP, "%s:%d: dbus: Error requesting name: %s\n", __func__, __LINE__, error.message);
	dbus_error_free(&error);
    }

    dbus_error_free(&error);

    return RETURN_OK;
}

void* rdkb_dbus_process_task(void* arg)
{
    rdkb_wifi_supp_info_t *p_wifi_supp = (rdkb_wifi_supp_info_t *)arg;
    rdkb_dbus_wifi_obj_desc_t *p_global_dbus_obj = get_wifi_supp_dbus_obj(SUPP_GLOBAL_DBUS_OBJ_NAME);
    DBusConnection *p_connection = (DBusConnection *)p_global_dbus_obj->connection;

    while ((p_wifi_supp->is_wifi_supp_conn_init == true)
        && (dbus_connection_read_write_dispatch(p_connection, -1))) {
    }

    dbus_connection_unref(p_connection);
    wifi_util_info_print(WIFI_SUPP,"%s:%d: wifi supp dbus process task is stopped\n", __func__, __LINE__);
    return NULL;
}

int rdkb_wifi_dbus_init(rdkb_wifi_supp_info_t **p_wifi_supp_obj)
{
    pthread_t thread_id;
    pthread_attr_t attr;
    pthread_attr_t *attrp = &attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int ret = 0;
    rdkb_wifi_supp_info_t *p_wifi_supp = NULL;

    p_wifi_supp = (*p_wifi_supp_obj = malloc(sizeof(rdkb_wifi_supp_info_t)));
    if (p_wifi_supp == NULL) {
        wifi_util_error_print(WIFI_SUPP,"%s:%d Error: memory alloc failed"
            " for wifi supp main obj\n", __func__, __LINE__);
        return RETURN_ERR;
    }
    memset(p_wifi_supp, 0, sizeof(rdkb_wifi_supp_info_t));

    p_wifi_supp->supp_dbus_obj_desc_map = hash_map_create();
    p_wifi_supp->supp_info.bss_handler_args_map = hash_map_create();
    p_wifi_supp->supp_info.network_handler_args_map = hash_map_create();

    circular_list_init(&p_wifi_supp->supp_info.bss);

    strcpy(p_wifi_supp->supp_info.dbus_new_path, RDKB_DBUS_NEW_INTERFACE_PATH);

    rdkb_dbus_wifi_obj_desc_t *p_global_dbus_obj = get_wifi_supp_dbus_obj(SUPP_GLOBAL_DBUS_OBJ_NAME);
    rdkb_dbus_obj_init(p_global_dbus_obj, (void *)&p_wifi_supp->supp_info);

    p_wifi_supp->supp_info.dbus_connection = p_global_dbus_obj->connection;
    p_wifi_supp->is_wifi_supp_conn_init = true;

    ret = pthread_create(&thread_id, attrp, rdkb_dbus_process_task, p_wifi_supp);
    if (ret != 0) {
        wifi_util_error_print(WIFI_SUPP,"%s:%d Error: pthread_create"
            " failed with code %d\n", __func__, __LINE__, ret);
        return RETURN_ERR;
    } else if (attrp != NULL) {
        pthread_attr_destroy(attrp);
    }

    wifi_util_info_print(WIFI_SUPP,"%s:%d dbus msg process thread is started\r\n", __func__, __LINE__);
    return RETURN_OK;
}
