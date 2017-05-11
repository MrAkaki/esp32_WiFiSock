
#include <math.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"


static const char *TAG = "CLIENTE";

#define DEBUG(...) ESP_LOGD(TAG,__VA_ARGS__);
#define INFO(...) ESP_LOGI(TAG,__VA_ARGS__);
#define ERROR(...) ESP_LOGE(TAG,__VA_ARGS__);

#define delay_ms(ms) vTaskDelay((ms) / portTICK_RATE_MS)

#define WIFI_SSID "PODO"
#define WIFI_PASS "244466666"

#define PORT_NUMBER 8001
#define TAM_DATA 512

volatile uint8_t dataMia[TAM_DATA];//datos de este dispositivo

static EventGroupHandle_t wifi_event_group; //manejo de banderas de conexiones

const int STA_CONNECTED_BIT = BIT0;// bandera de conexion sta

//Tarea donde se deberia de ejecutar todos los calculos.
void task_update(void* ignore){
  dataMia[0]='W';//el primer dato del vector dice que hace, w escribe

  //TODO: falta hacer los calculo y todo aca.
  while(1){
    delay_ms((rand()%30)*100);
    for(int i=1;i<TAM_DATA;++i){
      dataMia[i] = rand()%256;
    }
  }
}

//manejador de eventos wifi
static esp_err_t event_handler(void *ctx, system_event_t *event){
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, STA_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, STA_CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

//tarea encargada de comunicacion de sockets
static void task_socket(void *ignore){
  struct timeval  tv1, tv2;
  while(1){
    gettimeofday(&tv1, NULL);
    xEventGroupWaitBits(
              wifi_event_group,   /* The event group being tested. */
              STA_CONNECTED_BIT, /* The bits within the event group to wait for. */
              false,        /* BIT_0 & BIT_4 should be cleared before returning. */
              true,       /* Don't wait for both bits, either bit will do. */
              portMAX_DELAY );
  	int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
  	   ERROR("SOCKET: %d ERRno:%d", sock, errno);
    }else{
    	struct sockaddr_in serverAddress;
    	serverAddress.sin_family = AF_INET;
    	inet_pton(AF_INET, "192.168.4.1", &serverAddress.sin_addr.s_addr);
    	serverAddress.sin_port = htons(PORT_NUMBER);

    	int rs = connect(sock, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in));
      if(rs<0){
    	   ERROR("Concetando: %d ERRno:%d", sock, errno);
         close(sock);
      }else{
      	rs = write(sock,(char*) dataMia, TAM_DATA);
        close(sock);
        if(rs<0){
      	   ERROR("Escribiendo: %d ERRno:%d", sock, errno);
        }else{
          // si todo esto para esperar de forma chiva solo cuando todo sale bien:D
          do{
            delay_ms(5);
            gettimeofday(&tv2,NULL);
          }while(
            ((tv2.tv_usec - tv1.tv_usec)/1000 +
            (tv2.tv_sec - tv1.tv_sec)*1000)<1000
          );
        }
      }
    }
  }
}

//Inicializador de comunicacion WiFi
static void initialise_wifi(void){
  tcpip_adapter_init();
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
  ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
  wifi_config_t sta_wifi_config = {
    .sta ={
      .ssid = WIFI_SSID,
      .password = WIFI_PASS,
    }
  };
  INFO("Configurando WiFi STA: SSID %s", sta_wifi_config.sta.ssid);
  INFO(" Pass %s", sta_wifi_config.sta.password);

  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_wifi_config) );
  ESP_ERROR_CHECK( esp_wifi_start());
}

//Funcion de arranque.
void app_main()
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    xTaskCreate(&task_socket, "socket", 2*1024  , NULL, 5, NULL);// 2k de ram es mas que suficiente para este proceso.
    xTaskCreate(&task_update, "update", 4*1024  , NULL, 5, NULL);// 4k de ram para calculos
}
