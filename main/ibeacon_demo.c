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
#define WIFI_SSID              "hello"
#define WIFI_PASS              "12345678"
#define TCP_SERVER_ADDRESS     "192.168.12.125"
#define TCP_SERVER_PORT        3010
char TCP_CONNECT_MESSAGE_g[30] = " ";

const char* MQTT_URI =       "mqtt://atomtechnology.cn:1883";      //MQTT SERVER IP
const char* MQTT_USER =      "IOT";                      //MQTT SERVER ACCESS USER
const char* MQTT_PASSWORD =  "LiewIOT2018-";         //MQTT SERVER ACCESS PASSWORD

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;


//Tag here for ESP_LOGI();
static const char* DEMO_TAG = "IBEACON";
static const char* WIFI_TAG = "   WIFI";
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
//Used to transform Decimal to Hexadecimal and saved as string
char Hexadecimal_Code[]="0123456789ABCDEF";
char str_Temp[10];


// Declare static functions
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);


static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x0010,
    .scan_window            = 0x0010,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};


struct sockaddr_in tcpServerAddr_g;

BaseType_t xReturned_g;
TaskHandle_t xHandle_tcp_g = NULL;
int xTaskCreat_tcp_flag_g = 1;


char *Decimal_To_Hexadecimal_String(int Decimal_Data)
{
	int High,Low,i=0;
	if(Decimal_Data <= 0 || Decimal_Data > 255)
		exit(0);

	High=Decimal_Data >> 4;  //取二进制位的前4位
   	Low=Decimal_Data & 15;   //取二进制位的后4位

  	str_Temp[i++]=Hexadecimal_Code[High];
	str_Temp[i++]=Hexadecimal_Code[Low];
	str_Temp[i]='\0';

	return str_Temp;
}

int Message_Transmit(unsigned char *Message_Address)
{
	int i = 0, j = 0;
	while(TCP_CONNECT_MESSAGE_g[j])
	{
		TCP_CONNECT_MESSAGE_g[j] = NULL;
		j++;
	}

	for(i = 0; i < 6; i++)
	{
		strcat( TCP_CONNECT_MESSAGE_g, Decimal_To_Hexadecimal_String( (int)*(Message_Address + i) ) );
	}

	return 1;
}

void Wifi_Connect()
{
	wifi_config_t wifi_config = {
	        .sta = {
	            .ssid = WIFI_SSID,
	            .password = WIFI_PASS
	        },
	    };

	ESP_ERROR_CHECK( esp_wifi_disconnect() );
	ESP_ERROR_CHECK( esp_wifi_set_config( ESP_IF_WIFI_STA, &wifi_config ) );
	ESP_ERROR_CHECK( esp_wifi_connect() );

	ESP_LOGI(WIFI_TAG, "[ SYSTEM ] Connected to WiFi SSID:[%s] password:[%s].",WIFI_SSID, "********");
}

void TCP_Server_Connect(void)
{
	tcpServerAddr_g.sin_addr.s_addr = inet_addr( TCP_SERVER_ADDRESS );
	tcpServerAddr_g.sin_family = AF_INET;
	tcpServerAddr_g.sin_port = htons( TCP_SERVER_PORT );

	ESP_LOGI(WIFI_TAG, "[ SYSTEM ] Connected to TCP server:[%s:%d].",TCP_SERVER_ADDRESS, TCP_SERVER_PORT);
}

void TCP_Client_Message(void *pvParameters)
{
	int s, r;
	char recv_buf[64];

	while(1)
	{
		xEventGroupWaitBits( wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY );
	    s = socket( AF_INET, SOCK_STREAM, 0 );
	    if(s < 0)
	    {
	        ESP_LOGE(WIFI_TAG, "[ SYSTEM ] Failed to allocate socket.\n");
	        vTaskDelay(1000 / portTICK_PERIOD_MS);
	        continue;
	    }
	    ESP_LOGI(WIFI_TAG, "[ SYSTEM ] allocated socket.\n");
	    if(connect(s, (struct sockaddr *)&tcpServerAddr_g, sizeof(tcpServerAddr_g)) != 0)
	    {
	        ESP_LOGE(WIFI_TAG, "[ SYSTEM ] socket connect failed errno=%d. \n", errno);
	        close(s);
	        vTaskDelay(4000 / portTICK_PERIOD_MS);
	        continue;
	    }
	    ESP_LOGI(WIFI_TAG, "[ SYSTEM ] connected. \n");
	    if( write( s , TCP_CONNECT_MESSAGE_g , strlen( TCP_CONNECT_MESSAGE_g ) ) < 0)
	    {
	        ESP_LOGE(WIFI_TAG, "[ SYSTEM ] Send failed. \n");
	        close(s);
	        vTaskDelay(4000 / portTICK_PERIOD_MS);
	        continue;
	    }
	    ESP_LOGI(WIFI_TAG, "[ SYSTEM ] socket send success");
	    do
	    {
	        bzero(recv_buf, sizeof(recv_buf));
	        r = read(s, recv_buf, sizeof(recv_buf)-1);
	        for(int i = 0; i < r; i++)
	        {
	            putchar(recv_buf[i]);
	        }
	    } while(r > 0);
	    ESP_LOGI(WIFI_TAG, "[ SYSTEM ] done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
	    close(s);
	    ESP_LOGI(WIFI_TAG, "[ SYSTEM ] new request in 5 seconds");
	    vTaskDelay(5000 / portTICK_PERIOD_MS);
	}
	ESP_LOGI(WIFI_TAG, "[ SYSTEM ] tcp_client task closed\n");

	ESP_LOGI(WIFI_TAG, "[ SYSTEM ] TCP_Client_Message()_CHECK_3");
}

void xTask_TCP(void)
{
	if( xTaskCreat_tcp_flag_g)
	{
		xReturned_g = xTaskCreate( &TCP_Client_Message, "TCP_Client_Message" , 4048 , NULL , 5 , &xHandle_tcp_g);
		xTaskCreat_tcp_flag_g = 0;
	}

	if(xHandle_tcp_g == NULL)
	{
		if(xReturned_g == pdPASS)
		{
			vTaskResume(xHandle_tcp_g);
			ESP_LOGI(DEMO_TAG, "[ SYSTEM ] TCP task is resumed.");
		}
		else
		{
			xReturned_g = xTaskCreate( &TCP_Client_Message, "TCP_Client_Message" , 4048 , NULL , 5 , &xHandle_tcp_g);
			vTaskSuspend(xHandle_tcp_g);
			ESP_LOGI(DEMO_TAG, "[ SYSTEM ] TCP task is suspended.");
		}
	}
	else
	{

		vTaskDelete(xHandle_tcp_g);
		ESP_LOGI(DEMO_TAG, "[ SYSTEM ] TCP task is killed.");
	}



//	if(xReturned == pdPASS)
//	{
//		//vTaskDelete(xHandle_tcp_g);
//		//ESP_LOGI(DEMO_TAG, "[ SYSTEM ] TCP task is killed.");
//
//
//	}
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
    	Wifi_Connect();
    	TCP_Server_Connect();
		break;
    case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
		break;
    case SYSTEM_EVENT_STA_CONNECTED:
    	ESP_LOGI(WIFI_TAG, "[ SYSTEM ] Wifi connected successfully.");

    	break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
    	esp_wifi_connect();
    	xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    	break;
    default:
        break;
    }
    ESP_LOGI(WIFI_TAG, "[ SYSTEM ] Didn't enter Wifi event group.");
    return ESP_OK;
}

void Wifi_Initialize()
{
	tcpip_adapter_init();
	esp_log_level_set("wifi", ESP_LOG_NONE); // disable wifi driver logging
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    //ESP_ERROR_CHECK( esp_wifi_set_storage( WIFI_STORAGE_RAM ) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );

}


/* =============================================================================
 * int Device_Address_Filter(unsigned char *Address)                 //过滤器
 * int Device_Address_Repeat_Preventation(unsigned char *Address)    //重复查询
 * int Device_Information_Record(unsigned char *Address)                 //记录
 * int Device_Information_List()
 * int Device_Address_Sort()
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

int Device_Information_List(void)
{
	//
	int i = 0, j = Device_Number_g - 10 ;

	if(Device_Number_g >= 20)
	{
		ESP_LOGI(DEMO_TAG, "----------Device List----------");
		for(i = 0; i < 10; i++)
			ESP_LOGI(DEMO_TAG, "0x%04X, 0x%04X [%d]", DIR[i].MajorID , DIR[i].MinorID , i);

		for(j = Device_Number_g-10; j < Device_Number_g; j++)
			ESP_LOGI(DEMO_TAG, "0x%04x, 0x%04X [%d]", DIR[j].MajorID , DIR[j].MinorID , j);
		Device_Information_List_Flag = !Device_Information_List_Flag;
		return 1;
	}
	else
	{
		ESP_LOGI(DEMO_TAG,"[ SYSTEM ] Device Number Required: 20 at least");
		return 0;
	}

}

int Device_Address_Sort(void)
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
        					ESP_LOGI(DEMO_TAG, "[ SYSTEM ] New Device Scanned: %d ", Device_Number_g);
        					esp_log_buffer_hex("IBEACON: Device address", scan_result->scan_rst.bda, ESP_BD_ADDR_LEN );
        					ESP_LOGI(DEMO_TAG, "Major & Minor: 0x%04X (%d), 0x%04X (%d)", major, major, minor, minor);

        					Message_Transmit( &(scan_result->scan_rst.bda) );
        					ESP_LOGI(DEMO_TAG, "[ SYSTEM ] TCP_CONNECT_MESSAGE[] = %s", TCP_CONNECT_MESSAGE_g );
        					xTask_TCP();
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
            ESP_LOGE(DEMO_TAG, "[System] Scanning stopped failed: %s.", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(DEMO_TAG, "[System] Scanning stopped successfully.");
        }
        break;
     default:
        break;
    }
}

void ble_ibeacon_appRegister(void)
{
    esp_err_t status;

    ESP_LOGI(DEMO_TAG, "[ SYSTEM ] Register Callback.");

    //register the scan callback function to the gap module
    if ((status = esp_ble_gap_register_callback(esp_gap_cb)) != ESP_OK) {
        ESP_LOGE(DEMO_TAG, "[ SYSTEM ] Gap Register Error: %s.", esp_err_to_name(status));
        return;
    }
}

void ble_ibeacon_init(void)
{
    esp_bluedroid_init();
    esp_bluedroid_enable();
    ble_ibeacon_appRegister();
}





void app_main()
{
	wifi_event_group = xEventGroupCreate();
	if(wifi_event_group != NULL)
	{
		ESP_LOGI(DEMO_TAG, "[ SYSTEM ] WiFi events group was created.");
		ESP_ERROR_CHECK( esp_event_loop_init( wifi_event_handler, NULL ) );               //

		esp_err_t ret = nvs_flash_init();
		while(ret == ESP_ERR_NVS_NO_FREE_PAGES)
		{
			ESP_LOGI(DEMO_TAG, "[ SYSTEM ] NVS flash was not initialized successfully. Rebuilding....");
			ESP_ERROR_CHECK( nvs_flash_erase() );
			ret = nvs_flash_init();
		}
		ESP_ERROR_CHECK( ret );

		ESP_ERROR_CHECK( esp_bt_controller_mem_release( ESP_BT_MODE_CLASSIC_BT ) );

		esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
		esp_bt_controller_init( &bt_cfg );
		esp_bt_controller_enable( ESP_BT_MODE_BLE );

		Wifi_Initialize();
		ble_ibeacon_init();
		esp_ble_gap_set_scan_params( &ble_scan_params );
	}
	else
		ESP_LOGI(DEMO_TAG, "[ SYSTEM ] WiFi events group was not created successfully.");

}

