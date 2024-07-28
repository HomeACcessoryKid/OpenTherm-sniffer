/* (c) 2024 HomeAccessoryKid
 * OpenTherm-sniffer for ESP32
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
// #include "lcm_api.h"
#include <udplogger.h>

#include "driver/gpio.h"

#include "esp_timer.h"

// You must set VERSION=x.y.z of the lcm-demo code to match github version tag x.y.z via e.g. version.txt file

#define GPIO_OUTPUT_IO      21 //used to power transistors
#define GPIO_OUTPUT_PIN_SEL (1ULL<<GPIO_OUTPUT_IO)
#define GPIO_INPUT_IO_0     22
#define GPIO_INPUT_IO_1     23
#define GPIO_INPUT_PIN_SEL  ((1ULL<<GPIO_INPUT_IO_0) | (1ULL<<GPIO_INPUT_IO_1))

#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue0;
static QueueHandle_t gpio_evt_queue1;

#define  READY 0
#define  START 1
#define  RECV  2
int      resp_idx0=0, rx_state0=READY;
int      resp_idx1=0, rx_state1=READY;
uint32_t response0=0;
uint32_t response1=0;
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    static uint64_t before0=0;
    static uint64_t before1=0;
    uint64_t now,delta;
    int even,inv_read;
    switch (gpio_num) {
        case GPIO_INPUT_IO_0: {
            now=esp_timer_get_time(),delta=now-before0;
            even=0, inv_read=gpio_get_level(GPIO_INPUT_IO_0);//note that gpio_read gives the inverted value of the symbol
            if (rx_state0==READY) {
                if (inv_read) return;
                rx_state0=START;
                before0=now;
            } else if (rx_state0==START) {
                if (400<delta && delta<650 && inv_read) {
                    resp_idx0=0; response0=0; even=0;
                    rx_state0=RECV;
                } //else error state but might be a new start, so just stay in this state
                before0=now;
            } else if (rx_state0==RECV)  {
                if (900<delta && delta<1150) {
                    if (resp_idx0<32) {
                        response0=(response0<<1)|inv_read;
                        if (inv_read) even++;
                        resp_idx0++;
                        before0=now;
                    } else {
                        if (even%2==0) {
                            if (response0&0x0f000000) resp_idx0=-2; //signal issue reserved bits not zero
                            else {
                                response0&=0x7fffffff; //mask parity bit
                                xQueueSendToBackFromISR(gpio_evt_queue0, (void*)&response0, NULL);
                            }
                        } else resp_idx0=-1; //signal issue parity failure
                        rx_state0=READY;
                    }
                } else if (delta>=1150) { //error state
                    if (inv_read) rx_state0=READY;
                    else {rx_state0=START; before0=now;}
                } //else do nothing so before0+=500 and next transit is a databit
            }
            
            break;}
        case GPIO_INPUT_IO_1: {
            now=esp_timer_get_time(),delta=now-before1;
            even=0, inv_read=gpio_get_level(GPIO_INPUT_IO_1);//note that gpio_read gives the inverted value of the symbol
            if (rx_state1==READY) {
                if (inv_read) return;
                rx_state1=START;
                before1=now;
            } else if (rx_state1==START) {
                if (400<delta && delta<650 && inv_read) {
                    resp_idx1=0; response1=0; even=0;
                    rx_state1=RECV;
                } //else error state but might be a new start, so just stay in this state
                before1=now;
            } else if (rx_state1==RECV)  {
                if (900<delta && delta<1150) {
                    if (resp_idx1<32) {
                        response1=(response1<<1)|inv_read;
                        if (inv_read) even++;
                        resp_idx1++;
                        before1=now;
                    } else {
                        if (even%2==0) {
                            if (response1&0x0f000000) resp_idx1=-2; //signal issue reserved bits not zero
                            else {
                                response1&=0x7fffffff; //mask parity bit
                                xQueueSendToBackFromISR(gpio_evt_queue1, (void*)&response1, NULL);
                            }
                        } else resp_idx1=-1; //signal issue parity failure
                        rx_state1=READY;
                    }
                } else if (delta>=1150) { //error state
                    if (inv_read) rx_state1=READY;
                    else {rx_state1=START; before1=now;}
                } //else do nothing so before1+=500 and next transit is a databit
            }
            break;}
        default:
            break;
    }
}

void task0(void *arg) {
    uint32_t message;
    while (true) {
        if (xQueueReceive(gpio_evt_queue0, &(message), (TickType_t)850/portTICK_PERIOD_MS) == pdTRUE) {
            UDPLUS("Slave: %08lx\n",message);
        } else {
            UDPLUS("!!! NO_RSP Slave:  resp_idx=%d rx_state=%d response=%08lx\n",resp_idx0, rx_state0, response0);
            resp_idx0=0, rx_state0=READY, response0=0;
        }
    }
}

void task1(void *arg) {
    uint32_t message;
    while (true) {
        if (xQueueReceive(gpio_evt_queue1, &(message), (TickType_t)850/portTICK_PERIOD_MS) == pdTRUE) {
            UDPLUS("Master:%08lx\n",message);
        } else {
            UDPLUS("!!! NO_RSP Master: resp_idx=%d rx_state=%d response=%08lx\n",resp_idx1, rx_state1, response1);
            resp_idx1=0, rx_state1=READY, response1=0;
        }
    }
}

void main_task(void *arg) {
    udplog_init(3);
    gpio_evt_queue0 = xQueueCreate(10, sizeof(uint32_t));
    gpio_evt_queue1 = xQueueCreate(10, sizeof(uint32_t));
        
    gpio_config_t io_conf = {}; //zero-initialize the config structure.

    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL; //bit mask of the pins
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf); //configure GPIO with the given settings
    gpio_set_level(GPIO_OUTPUT_IO, 1); //set it to 1 to provide 3V3

    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL; //bit mask of the pins
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1; //enable pull-up mode for open collector driving
    gpio_config(&io_conf); //configure GPIO with the given settings

   
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT); //install gpio isr service
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void*) GPIO_INPUT_IO_0); //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) GPIO_INPUT_IO_1); //hook isr handler for specific gpio pin
    
    gpio_dump_io_configuration(stdout, (GPIO_INPUT_PIN_SEL|GPIO_OUTPUT_PIN_SEL) );
    
    xTaskCreate(task0,"task0",4096,NULL,1,NULL);
    xTaskCreate(task1,"task1",4096,NULL,1,NULL);
    while (true) { //TODO: if task deleted, semaphore of UDPlogger also 
        vTaskDelay(1000); 
    }
    vTaskDelete(NULL);
}    

void app_main(void) {
    printf("app_main-start\n");

    //The code in this function would be the setup for any app that uses wifi which is set by LCM
    //It is all boilerplate code that is also used in common_example code
    esp_err_t err = nvs_flash_init(); // Initialize NVS
    if (err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); //NVS partition truncated and must be erased
        err = nvs_flash_init(); //Retry nvs_flash_init
    } ESP_ERROR_CHECK( err );

    //TODO: if no wifi setting found, trigger otamain
    
    //block that gets you WIFI with the lowest amount of effort, and based on FLASH
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    esp_netif_config.route_prio = 128;
    esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    //end of boilerplate code

    xTaskCreate(main_task,"main",4096,NULL,1,NULL);
    while (true) {
        vTaskDelay(1000); 
    }
    printf("app_main-done\n"); //will never exit here
}
