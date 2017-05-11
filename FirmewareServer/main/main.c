
#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
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



#define STA_WIFI_SSID "LA JAURIA"
#define STA_WIFI_PASS "244466666"
#define AP_WIFI_SSID "PODO"
#define AP_WIFI_PASS "244466666"

#define PORT_NUMBER 8001
#define TAM_DATA 512

volatile uint8_t dataCliente[TAM_DATA]; // datos del otro dispositivo
volatile uint8_t dataMia[TAM_DATA]; //datos de este dispositivo

uint8_t dataClienteTemp[TAM_DATA]; //datos temporales de lecturas
uint8_t data[TAM_DATA*2]; // datos temporales de la union de los 2 dispositivos


static EventGroupHandle_t wifi_event_group; //manejo de banderas de conexiones

const int STA_CONNECTED_BIT = BIT0; // bandera de conexion sta
const int AP_CONNECTED_BIT = BIT1; //bandera de conexion ap

//Tarea donde se deberia de ejecutar todos los calculos.
void task_update(void* ignore){
  dataMia[0]='W';
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
    case SYSTEM_EVENT_AP_STACONNECTED:
        	ESP_LOGI(TAG, "station:"MACSTR" join,AID=%d\n",
    		      MAC2STR(event->event_info.sta_connected.mac),
    		      event->event_info.sta_connected.aid);
        	xEventGroupSetBits(wifi_event_group, AP_CONNECTED_BIT);
        	break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        	ESP_LOGI(TAG, "station:"MACSTR"leave,AID=%d\n",
    		      MAC2STR(event->event_info.sta_disconnected.mac),
    		      event->event_info.sta_disconnected.aid);
        	xEventGroupClearBits(wifi_event_group, AP_CONNECTED_BIT);
        	break;
    default:
        break;
    }
    return ESP_OK;
}

//tarea encargada de comunicacion de sockets
static void task_socket(void *pvParameters){
  struct sockaddr_in clientAddress;
  struct sockaddr_in serverAddress;
  int err = 0;
  while(1){
    xEventGroupWaitBits(
              wifi_event_group,   /* The event group being tested. */
              AP_CONNECTED_BIT | STA_CONNECTED_BIT, /* The bits within the event group to wait for. */
              true,        /* BIT_0 & BIT_4 should be cleared before returning. */
              false,       /* Don't wait for both bits, either bit will do. */
              portMAX_DELAY );

    bool error = false;
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
      ERROR("SOCKET: %d ERRno:%d", sock, errno);
      error = true;
    }

    // Bind our server socket to a port.
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(PORT_NUMBER);
    int rc  = bind(sock, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (rc < 0) {
      ERROR("Haciendo BIND %d ERRno:%d", rc, errno);
      error = true;
    }

    rc = listen(sock, 5);
    if (rc < 0) {
      ERROR("Ecuchando: %d ERRno:%d", rc, errno);
      error = true;
    }
    socklen_t clientAddressLength = sizeof(clientAddress);
    while (!error) {
      int clientSock = accept(sock, (struct sockaddr *)&clientAddress, &clientAddressLength);
      if (clientSock < 0) {
        ERROR("Aceptando: %d ERRno:%d", clientSock, errno);
        error=true;
      }else{
        ssize_t sizeRead = recv(clientSock, dataClienteTemp, TAM_DATA, MSG_WAITALL );
        if(sizeRead<0){
           ERROR("Leyendo: %d ERRno:%d", clientSock, errno);
           error = true;
        }
        if(sizeRead>0){
          switch (dataClienteTemp[0]) {
            case 'W':
              for(int i=0; i<sizeRead;++i){
                dataCliente[i] = dataClienteTemp[i];
              }
              break;
            case 'R':
              //UNIMOS los datos de ambas plantillas
              for(int i=0; i<TAM_DATA;++i){
                data[i] = dataMia[i];
              }
              for(int i=0; i<TAM_DATA;++i){
                data[i+TAM_DATA] = dataCliente[i];
              }
              //las enviamos
              rc = write(clientSock, data, TAM_DATA*2);
              if(rc<0){
                ERROR("Leyendo: %d ERRno:%d", clientSock, errno);
              }
              break;
          }
        }
        close(clientSock);
      }
    }
    ++err;
    ERROR("Probemas graves, van: %d",err);
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
      .ssid = STA_WIFI_SSID,
      .password = STA_WIFI_PASS,
    }
  };
  wifi_config_t ap_wifi_config = {
    .ap = {
      .ssid = AP_WIFI_SSID,
      .ssid_len = 0,
      .password = AP_WIFI_PASS,
      .channel = 1,
      .authmode = WIFI_AUTH_WPA2_PSK,
      .beacon_interval = 400,
      .max_connection = 4, // TODO: conexiones maximas
    }
  };

  ESP_LOGI(TAG, "Setting WiFi AP: SSID %s", ap_wifi_config.ap.ssid);
  ESP_LOGI(TAG, " Pass %s", ap_wifi_config.ap.password);
  ESP_LOGI(TAG, "Setting WiFi STA: SSID %s", sta_wifi_config.sta.ssid);
  ESP_LOGI(TAG, " Pass %s", sta_wifi_config.sta.password);

  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_wifi_config) );
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_wifi_config) );
  ESP_ERROR_CHECK( esp_wifi_start());
}

//Funcion de arranque.
void app_main(){
    ESP_ERROR_CHECK( nvs_flash_init() );
    initialise_wifi();
    xTaskCreate(&task_socket, "socket",4*1024, NULL, 5, NULL);// esta tarea requiere el doble que el otro dispositivo
    xTaskCreate(&task_update, "update", 4*1024  , NULL, 5, NULL);// en teoria deberia de ocupar la misma cantidad que el otro
}
