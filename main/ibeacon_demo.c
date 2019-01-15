#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
//This one below is new
#include <stddef.h>
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_defs.h"
#include "esp_ibeacon_api.h"
#include "esp_log.h"
//These three are new
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
//These headfile below are new
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "tcpip_adapter.h"



/* ============================================================================
 * 1. Configuration of WiFi
 * 2. Configuration of MQTT Service
 * 3. Event Group Handle - WiFi Event Group
 * 4. The event group allows multiple bits for each event, but we only care about
 * one event - are we connected to the AP with an IP?
 * ============================================================================*/
#define WIFI_SSID          "hello"
#define WIFI_PASS          "12345678"
#define TCP_SEVER_ADDRESS  "192.168.12.125"

const char* MQTT_URI = "mqtt://atomtechnology.cn:1883";      //MQTT SERVER IP
const char* MQTT_USER = "IOT";                      //MQTT SERVER ACCESS USER
const char* MQTT_PASSWORD = "LiewIOT2018-";         //MQTT SERVER ACCESS PASSWORD

static EventGroupHandle_t s_wifi_event_group;

const int WIFI_CONNECTED_BIT = BIT0;



/* ============================================================================
 * 1. TAG:
 *  - DEMO_TAG
 *  - WIFI_TAG
 * 2. iBeacon Vender Configuration
 * ============================================================================*/
static const char* DEMO_TAG = "IBEACON";
static const char* WIFI_TAG = "WIFI";
extern esp_ble_ibeacon_vendor_t vendor_config;



/* =============================================================================
 * New Defined Value:
 * 1. Device Number        //The device scanned number
 * 2. Device Scan
 * 3. Device Information:
 * 	  - MAC Address (3rd 4th 5th)
 * ============================================================================*/
#define Device_Number_Ceiling_g 100

int Device_Number_g = 0;
int Device_Scanned_Number_g = 0;
int Device_Information_List_Flag = 1;

typedef struct{
	unsigned char Device_Address_3;
	unsigned char Device_Address_4;
	unsigned char Device_Address_5;
	int MajorID;
	int MinorID;
	int Scanned_Time;
}Device_Information;

Device_Information DIR[Device_Number_Ceiling_g];

int ID_Array_Temp[Device_Number_Ceiling_g];


// Declare static functions
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);


/* =============================================================================
 * iBeacon Receiver Mode Setting
 * 1. Scan type
 * 2. Owner address type
 * 3. Scan filter policy
 * 4. Scan interval. This is defined as the time interval from when the Controller started its last LE scan until it begins the subsequent LE scan.
 * 5. Scan window. The duration of the LE scan. LE_Scan_Window shall be less than or equal to LE_Scan_Interval。
 * 6. The Scan_Duplicates parameter controls whether the Link Layer should filter out duplicate advertising reports (BLE_SCAN_DUPLICATE_ENABLE)
 *    to the Host, or if the Link Layer should generate advertising reports for each packet received
 * ============================================================================*/
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x0010,
    .scan_window            = 0x0010,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};



/* =============================================================================
 * int Device_Address_Filter(unsigned char *Address)                 //过滤器
 * int Device_Address_Repeat_Preventation(unsigned char *Address)    //重复查询
 * int Device_Information_Record(unsigned char *Address)                 //记录
 * ============================================================================*/
//0C F3 EE Filter_conditionor
int Device_Address_Filter(unsigned char *Address)
{
	int Filter[3]={ 0x0C,
					0xF3,
					0xEE
				  };
	if(Filter[0]==*(Address+0) &&
	   Filter[1]==*(Address+1) &&
	   Filter[2]==*(Address+2)
	   )
		return 1;
	else
		return 0;
}

int Device_Address_Repeat_Preventation(unsigned char *Address)
{
	int i=0;
	for(;i<Device_Number_g;i++)
	{
		if( DIR[i].Device_Address_3 == *(Address+3) &&
			DIR[i].Device_Address_4 == *(Address+4) &&
			DIR[i].Device_Address_5 == *(Address+5) )
		{
			DIR[i].Scanned_Time++;
			return 1;           //1 represents Repeat-Device
		}
	}
	return 0;                  //0 represents New-Device
}

int Device_Information_Record(unsigned char *Address, int Major, int Minor)
{
	//if(Device_Number_g > 100) return 0;             //0 represents "do not record"
	if(!Device_Address_Repeat_Preventation(Address))
	{
		DIR[Device_Number_g].Device_Address_3 = *(Address+3);
		DIR[Device_Number_g].Device_Address_4 = *(Address+4);
		DIR[Device_Number_g].Device_Address_5 = *(Address+5);
		DIR[Device_Number_g].MajorID = Major;
		DIR[Device_Number_g].MinorID = Minor;
		Device_Number_g++;
		return 1;                                    //1 represents "recorded successfully"
	}
	else
		return 0;
}

int Device_Information_List()
{
	//
	int i = 0, j = Device_Number_g - 10 ;

	if(Device_Number_g >= 20)
	{
		ESP_LOGI(DEMO_TAG, "----------Device List----------");
		for(i = 0; i < 10; i++)
			ESP_LOGI(DEMO_TAG, "%d, %d [%d]", DIR[i].MajorID , DIR[i].MinorID , i);
		ESP_LOGI(DEMO_TAG, "\n");
		for(j = Device_Number_g-10; j < Device_Number_g; j++)
			ESP_LOGI(DEMO_TAG, "%d, %d [%d]", DIR[j].MajorID , DIR[j].MinorID , j);
		Device_Information_List_Flag = !Device_Information_List_Flag;
		return 1;
	}
	else
	{
		ESP_LOGI(DEMO_TAG,"[System] Device Number Required: 20 at least");
		return 0;
	}

}

int Device_Address_Sort()
{
	int i=0, j=0;
	int INT_Temp;
	//unsigned char CHAR_Temp;

	ESP_LOGI(DEMO_TAG,"Device_Number_g = %d\r\n", Device_Number_g);

	for(i = 0; i < (Device_Number_g - 1); i++)
	{
		for(j = 0; j < Device_Number_g - 1 - i; j++)
		{
			if(DIR[j].MinorID > DIR[j+1].MinorID )
			{
				INT_Temp = DIR[j+1].MinorID;
				DIR[j+1].MinorID = DIR[j].MinorID;
				DIR[j].MinorID = INT_Temp;

				/*
				CHAR_Temp = DIR[j+1].Device_Address_3;
				DIR[j+1].Device_Address_3 = DIR[j].Device_Address_3;
				DIR[j].Device_Address_3 = CHAR_Temp;

				CHAR_Temp = DIR[j+1].Device_Address_4;
				DIR[j+1].Device_Address_4 = DIR[j].Device_Address_4;
				DIR[j].Device_Address_4 = CHAR_Temp;

				CHAR_Temp = DIR[j+1].Device_Address_5;
				DIR[j+1].Device_Address_5 = DIR[j].Device_Address_5;
				DIR[j].Device_Address_5 = CHAR_Temp;
				*/


			}
		}
	}

	return 1;
}



/*======================================================
 * iBeacon Events Handler:
 * ---ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT
 * ---ESP_GAP_BLE_SCAN_START_COMPLETE_EVT
 * ---ESP_GAP_BLE_SCAN_RESULT_EVT
 *    |---ESP_GAP_SEARCH_INQ_RES_EVT
 * ---ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT
 *====================================================*/
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;

    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
    {
        //the unit of the duration is second, 0 means scan permanently
        uint32_t duration = 0;
        esp_ble_gap_start_scanning(duration);
        break;
    }
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if ((err = param->scan_start_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(DEMO_TAG, "[System] Scan start failed: %s", esp_err_to_name(err));
        }
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        switch (scan_result->scan_rst.search_evt)
        {
        	case ESP_GAP_SEARCH_INQ_RES_EVT:
        		//Search for BLE iBeacon Packet
        		if (esp_ble_is_ibeacon_packet(scan_result->scan_rst.ble_adv, scan_result->scan_rst.adv_data_len))
        		{
        			//Device MAC Address Filter
        			if(Device_Address_Filter(&(scan_result->scan_rst.bda)) && (Device_Number_g < Device_Number_Ceiling_g))
        			{
        				esp_ble_ibeacon_t *ibeacon_data = (esp_ble_ibeacon_t*)(scan_result->scan_rst.ble_adv);
        				uint16_t major = ENDIAN_CHANGE_U16(ibeacon_data->ibeacon_vendor.major);
        				uint16_t minor = ENDIAN_CHANGE_U16(ibeacon_data->ibeacon_vendor.minor);

        				//ESP_LOGI(DEMO_TAG, "[System] Total Device Scanned: %d ", ++Device_Scanned_Number_g);
        				//New Beacon found
        				if( Device_Information_Record( &(scan_result->scan_rst.bda) ,major, minor) )
        				{
        					ESP_LOGI(DEMO_TAG, "----------New Beacon Found----------");
        					ESP_LOGI(DEMO_TAG, "[System] New Device Scanned: %d ", Device_Number_g);
        					esp_log_buffer_hex("IBEACON: Device address", scan_result->scan_rst.bda, ESP_BD_ADDR_LEN );
        					ESP_LOGI(DEMO_TAG, "Major & Minor: 0x%04x (%d), 0x%04x (%d)", major, major, minor, minor);
        				}
        				//Old Beacon Found
        				/*
        				else
        				{
        					ESP_LOGI(DEMO_TAG, "----------Old Beacon Found----------");
        				}
        				esp_log_buffer_hex("IBEACON: Device address", scan_result->scan_rst.bda, ESP_BD_ADDR_LEN );
        				ESP_LOGI(DEMO_TAG, "Major & Minor: 0x%04x (%d), 0x%04x (%d)", major, major, minor, minor);
        				*/
        			}
        			else if(Device_Information_List_Flag && Device_Number_g == Device_Number_Ceiling_g)
        			{
        			    Device_Information_List();
        			    Device_Address_Sort();
        			    ESP_LOGI(DEMO_TAG, "----------After Sorted----------");
        			    Device_Information_List_Flag = !Device_Information_List_Flag;
        			    Device_Information_List();
        			}
        		}
            break;
        	default:
        	break;
        }
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if ((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(DEMO_TAG, "[System] Scan stop failed: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(DEMO_TAG, "[System] Stop scan successfully");
        }
        break;
     default:
        break;
    }
}


void ble_ibeacon_appRegister(void)
{
    esp_err_t status;

    ESP_LOGI(DEMO_TAG, "[System] register callback");

    //register the scan callback function to the gap module
    if ((status = esp_ble_gap_register_callback(esp_gap_cb)) != ESP_OK) {
        ESP_LOGE(DEMO_TAG, "[System] gap register error: %s", esp_err_to_name(status));
        return;
    }
}

void ble_ibeacon_init(void)
{
    esp_bluedroid_init();
    esp_bluedroid_enable();
    ble_ibeacon_appRegister();
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
    	ESP_LOGI(WIFI_TAG, "[SYSTEM] SYSTEM_EVENT_STA_START");
		esp_wifi_connect();
		break;
    case SYSTEM_EVENT_STA_GOT_IP:
    	ESP_LOGI(WIFI_TAG, "[SYSTEM] SYSTEM_EVENT_STA_GOT_IP");
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		break;
    case SYSTEM_EVENT_STA_CONNECTED:
    	ESP_LOGI(WIFI_TAG, "[SYSTEM] SYSTEM_EVENT_STA_CONNECTED");

    	break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    	ESP_LOGI(WIFI_TAG, "[SYSTEM] SYSTEM_EVENT_STA_DISCONNECTED");
    	esp_wifi_connect();
    	xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    	break;
    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_start()
{
	//TCP IP initialize
	tcpip_adapter_init();
	//Set an initial value?
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //????????
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(WIFI_TAG, "[SYSTEM] WiFi is initialized.");
    ESP_LOGI(WIFI_TAG, "[SYSTEM] Connected to WiFi SSID:[%s] password:[%s]",
           WIFI_SSID, "********");

}

void app_main()
{
	ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    ble_ibeacon_init();
    //Function Added Here
    //wifi_init_start();
    //tcpip_adapter_init();

    /* set scan parameters */
    esp_ble_gap_set_scan_params(&ble_scan_params);
}

