

#include <unistd.h>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <hwlib/hwlib.h>

#ifdef __cplusplus
}
#endif

#include "iot_monitor.h"
#include "microrl_config.h"
#include "microrl.h"
#include "hts221.h"
#include "lis2dw12.h"
#include "binio.h"
#include "mytimer.h"
#include "http.h"
#include "m2x.h"
#include "MAX31855.hpp"

#include "mal.hpp"

#ifdef __cplusplus
extern "C" {
#endif
void set_color(char *);
void wwan_io(int);
#ifdef __cplusplus
}
#endif

static float lat;
static float lng;
static int   idx = 0;

void *check_gps(void *ptr)
{
    json_keyval om[12];
    int k, done, i;

    setGPSmode(4);
    getGPSconfig(om,sizeof(om));
    enableGPS();
    done = 0;
    while( !done ) {
        k=getGPSlocation(om,sizeof(om));
        done = atoi(om[3].value)?1:0;
        for( i=1; i<k; i++ ) {
            if( !strcmp(om[i].key,"latitude") ) {
                sscanf( om[i].value, "%f", &lat);
                }
            else if( !strcmp(om[i].key,"longitude") ) {
                sscanf( om[i].value, "%f", &lng);
                }
            else if( !strcmp(om[i].key,"errno") ) {
                if( atoi(om[i].value) )
                    printf("GPS ERROR! %d\n",atoi(om[i].value));
                }
            else if( !strcmp(om[i].key,"errmsg") ) {
                if( strcmp(om[i].value,"<null>") )
                    printf("GPS ERROR MESSAGE: %s\n",om[i].value);
                }
            }
        }
    disableGPS();
    return NULL;
}


static volatile int  user_press;
static gpio_handle_t boot_key, user_key, red_led, green_led, blue_led;

#define WAIT_FOR_BPRESS(x) {while( !x ); while( x );}

int gpio_ftirq_callback(gpio_pin_t pin_name, gpio_irq_trig_t direction)
{
    int *key;

    if (pin_name == GPIO_PIN_98) 
        key= (int*)&user_press;
    else
        return 0;

    *key = ! *key;
    return 0;
}

void purty(void) 
{
    char p[6] = {'/', '-', '\\', '/', '-', '\\'};
    printf("%c\r",p[idx]);
    idx = ++idx % 6;
}


int command_facttest(int argc __attribute__((unused)), const char *const *argv)
{
    extern GPIOPIN_IN gpio_input;
    MAX31855 max31855;
    json_keyval om[20];
    int i;
    float val;
    pthread_t thread1;
    struct timeval start, end;  //measure duration of flow calls...
    double elapse=0;

    binary_io_init();
    gpio_deinit( &gpio_input.hndl);

    i=start_data_service();
    while ( i < 0 ) {
        printf("WAIT: starting WNC MAL Interface (%d)\n",i);
        sleep(10);
        i=start_data_service();
        }

//    i=gpio_init( GPIO_PIN_1,  &boot_key); printf("initialize boot key gpio. (%d)\n",i);
//    i=gpio_dir(boot_key, GPIO_DIR_INPUT); printf("set boot key direction (%d)\n",i);
//    i=gpio_irq_request(boot_key, GPIO_IRQ_TRIG_BOTH, gpio_ftirq_callback); printf("set boot key as interrupt input. (%d)\n",i);


    if( (i=gpio_init( GPIO_PIN_98,  &user_key)) != 0 )
        printf("ERROR: unable to initialize user key gpio. (%d)\n",i);
    if( (i=gpio_dir(user_key, GPIO_DIR_INPUT)) != 0 )
        printf("ERROR: can't set user key as input. (%d)\n",i);
    if( (i=gpio_irq_request(user_key, GPIO_IRQ_TRIG_BOTH, gpio_ftirq_callback)) != 0)
        printf("ERROR: can't set user key as interrupt input. (%d)\n",i);

    user_press = 0;
    printf("\n---- BTN Test 1 ------------------------------\n");
    printf("BTN: Press User key");
    fflush(stdout);
    WAIT_FOR_BPRESS(user_press);
    printf("= PASS\n");
    printf("\n---- WNC Test 2 ------------------------------\n");
    mySystem.model=getModelID(om, sizeof(om));
    mySystem.firmVer=getFirmwareVersion(om, sizeof(om));
    mySystem.malwarVer=getMALManVer(om, sizeof(om));
    mySystem.ip=get_ipAddr(om, sizeof(om));
    mySystem.iccid=getICCID(om, sizeof(om));
    mySystem.imei=getIMEI(om, sizeof(om));
    mySystem.imsi=getIMSI(om, sizeof(om));

    printf("WNC: Module   #    = %s\n", mySystem.model.c_str());
    printf("WNC: Firmware #    = %s\n", mySystem.firmVer.c_str());
    printf("WNC: MAL Ver. #    = %s\n", mySystem.malwarVer.c_str());
    printf("WNC: IP Addr. #    = %s\n", mySystem.ip.c_str());
    printf("SIM: ICCID    #    = %s\n", mySystem.iccid.c_str());
    printf("SIM: IMEA     #    = %s\n",mySystem.imei.c_str());
    printf("SIM: IMSI     #    = %s\n",mySystem.imsi.c_str());

    get_wwan_status(om, sizeof(om));
    printf("\n---- WNC Test 3 ------------------------------\n");
    printf("LTE: Signal RSSI   = %s\n", om[4].value);
    printf("LTE: Signal Level  = %s\n", om[6].value);

// start the GPS interface.  It takes a while and uses the MAL interface
// so don't start it until we are done accessing the MAL for other info

    pthread_create( &thread1, NULL, check_gps, NULL);

    printf("\n---- ADC Test 4 ------------------------------\n");
    adc_handle_t my_adc=(adc_handle_t)NULL;

    adc_init(&my_adc);
    adc_read(my_adc, &val);
    printf("ADC: Light Sensor  = %f\n",val);
    adc_deinit(&my_adc);

    printf("\n---- I2C Test 5 ------------------------------\n");
    printf("I2C: LIS2DW12 ID   = 0x%02X\n", lis2dw12_getDeviceID());
    printf("I2C: PMOD/HTS ID   = 0x%02X\n", hts221_getDeviceID());

    printf("\n---- SPI Test 6 ------------------------------\n");
    printf("SPI: PMOD Result   = %s\n", max31855.loopbackTest()?"PASS":"FAIL");

    printf("\n---- LED Test 7 ------------------------------\n");
    wwan_io(1);
    gpio_init( GPIO_PIN_92,  &red_led );
    gpio_init( GPIO_PIN_101, &green_led );
    gpio_init( GPIO_PIN_102, &blue_led );

    gpio_dir(red_led,   GPIO_DIR_OUTPUT);
    gpio_dir(green_led, GPIO_DIR_OUTPUT);
    gpio_dir(blue_led,  GPIO_DIR_OUTPUT);

    printf("LED: PWR           = Green\n");
    printf("LED: WWAN          = Amber\n");
    printf("LED: USER Blnk     = R-G-B\n");
    printf("LED: CHECK LEDs\n");
    do_gpio_blink( 0, 1 );
    gettimeofday(&start, NULL);

    printf("\n---- GPS Test 8 ------------------------------\n");
    pthread_join( thread1, NULL); //wait here for the GSP to finish
    check_gps(NULL);
    printf("GPS: Latitude      = %8.5f\n",lat);
    printf("GPS: Longitude     = %8.5f\n",lng);

    gettimeofday(&end, NULL);
    elapse += (((end.tv_sec - start.tv_sec)*1000) + (end.tv_usec/1000 - start.tv_usec/1000));
    i = ((ft_time*1000)-round(elapse))/1000;

    if( i>0 ) {
        printf("\nDelay %d seconds, check LEDs\n",i);
        fflush(stdout);
        sleep(i);
        }
    printf("\n\n");
    gpio_deinit( &user_key);

    binario_io_close();
    binary_io_init();
}


