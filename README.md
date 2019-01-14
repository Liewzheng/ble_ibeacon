# ESP-IDF iBeacon
  此iBeacon版本程序实现了对Beacon设备的扫描、过滤、记录及防重功能。
  本设备可用于扫描特定MAC地址的iBeacon，并接受不同Company Manufacturer的数据包,已添加Wifi和MQTT，尚未完成函数构建。

## iBeacon 模式
- **IBEACON_RECEIVER**: 能够接收和解析iBeacon的数据包。

## iBeacon 事件
---ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT
---ESP_GAP_BLE_SCAN_START_COMPLETE_EVT
---ESP_GAP_BLE_SCAN_RESULT_EVT
   |---ESP_GAP_SEARCH_INQ_RES_EVT
---ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT

# BLE功能
## Scan模式参数
```c
// iBeacon Mode Setting
static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_ENABLE
};
```
## 设备信息记录结构体
```c
int Device_Number_g = 0;
int Device_Scanned_Number_g = 0;
//int New_Device_Scanned_Number_g = 0;
typedef struct{
	unsigned char Device_Address_3;
	unsigned char Device_Address_4;
	unsigned char Device_Address_5;
	//int MajorID;
	//int MinorID;
	int Scanned_Time;
}Device_Information;

Device_Information DIR[100];
```

## 设备过滤
### 基于MAC Address
```c
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
```
### 基于Company Manufacturer(esp_ibeacon_api.c)
```c
/* Constant part of iBeacon data */
esp_ble_ibeacon_head_t ibeacon_common_head = {
    .flags = {0x02, 0x01, 0x06},
    .length = 0x1A,
    .type = 0xFF,
    .company_id = 0x004C,
    .beacon_type = 0x1502
};

/* Constant part of alternative data */
esp_ble_ibeacon_head_t ibeacon_common_head_alternative = {
    .flags = {0x02, 0x01, 0x06},
    .length = 0x1A,
    .type = 0xFF,
    .company_id = 0x005A,
    .beacon_type = 0x1503
};
```

## 设备记录
```c
int Device_Address_Record(unsigned char *Address)
{
	if(Device_Number_g > 100) return 0;             //0 represents "do not record"
	if(!Device_Address_Repeat_Preventation(Address))
	{
		DIR[Device_Number_g].Device_Address_3 = * (Address+3);
		DIR[Device_Number_g].Device_Address_4 = * (Address+4);
		DIR[Device_Number_g].Device_Address_5 = * (Address+5);
		Device_Number_g++;
		return 1;                                    //1 represents "recorded successfully"
	}
	else
		return 0;
}
```

## 设备遍历（防重）
```c
int Device_Address_Repeat_Preventation(unsigned char *Address)
{
	int i=0;
	for(;i<Device_Number_g;i++){
		if( DIR[i].Device_Address_3 == *(Address+3) &&
			DIR[i].Device_Address_4 == *(Address+4) &&
			DIR[i].Device_Address_5 == *(Address+5) )
		{
			return 1;           //1 represents Repeat-Device
		}
	}

	if(i<100) DIR[i].Scanned_Time++;
	return 0;                  //0 represents New-Device
}
```

## 事件处理
```c
case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t * scan_result = (esp_ble_gap_cb_param_t * )param;
        switch (scan_result-> scan_rst.search_evt )
        {
        	case ESP_GAP_SEARCH_INQ_RES_EVT:
        		//Search for BLE iBeacon Packet
        		if (esp_ble_is_ibeacon_packet(scan_result->scan_rst.ble_adv, scan_result->scan_rst.adv_data_len))
        		{
        			//Device MAC Address Filter
        			if(Device_Address_Filter(&(scan_result->scan_rst.bda)) )
        			{
        				ESP_LOGI(DEMO_TAG, "[System] Total Device Scanned: %d ", ++Device_Scanned_Number_g);
        				//New Beacon found
        				if( Device_Address_Record(&(scan_result->scan_rst.bda)) )
        				{
        					ESP_LOGI(DEMO_TAG, "----------New Beacon Found----------");
        					ESP_LOGI(DEMO_TAG, "[System] New Device Scanned: %d ", Device_Number_g);
        				}
        				//Old Beacon Found
        				else
        				{
        					ESP_LOGI(DEMO_TAG, "----------Old Beacon Found----------");
        				}
        				esp_ble_ibeacon_t *ibeacon_data = (esp_ble_ibeacon_t*)(scan_result->scan_rst.ble_adv);
        				esp_log_buffer_hex("IBEACON: Device address", scan_result->scan_rst.bda, ESP_BD_ADDR_LEN );
        				uint16_t major = ENDIAN_CHANGE_U16(ibeacon_data->ibeacon_vendor.major);
        				uint16_t minor = ENDIAN_CHANGE_U16(ibeacon_data->ibeacon_vendor.minor);
        				ESP_LOGI(DEMO_TAG, "Major & Minor: 0x%04x (%d), 0x%04x (%d)", major, major, minor, minor);
        			}
        		}
            break;
        	default:
        	break;
        }
        break;
    }
```

# WiFi功能
## 基础参数定义
```c
#define WIFI_SSID          "hello"
#define WIFI_PASS          "12345678"
#define TCP_SEVER_ADDRESS  "192.168.12.125"

const char* MQTT_URI = "mqtt://atomtechnology.cn:1883";      //MQTT SERVER IP
const char* MQTT_USER = "IOT";                               //MQTT SERVER ACCESS USER
const char* MQTT_PASSWORD = "LiewIOT2018-";                  //MQTT SERVER ACCESS PASSWORD

static EventGroupHandle_t s_wifi_event_group;                //WiFi事件组定义

const int WIFI_CONNECTED_BIT = BIT0;
```

## 连接
```c
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
```

## 事件处理
```c
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
```
