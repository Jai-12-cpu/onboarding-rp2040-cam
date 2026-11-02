#include <stdio.h>
#include <tusb.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/vreg.h"
#include "hardware/gpio.h"
#include "cam.h"
#include "ImageData.h"
#include "LCD_1in14_V2.h" 
#include "GUI_Paint.h"
#include "hardware/clocks.h"

uint8_t image_buf[324*324]; //camera buffer, 8 bit color
uint16_t displayBuf[240*135]; //display buffer, in a weird RGB565 format

#define FLAG_VALUE 1234 // this flag value is going to be sent across cores!
uint8_t imageReady = 0; //only if the camera buffer has a new image, we display it!

//CORE 1 -------------------------------------------------
//Initializing Display Module
void core1_entry() 
{
    //INITIALIZE THE DISPLAY MODULE
    DEV_Module_Init(); //function dedicated for this module
    LCD_1IN14_V2_Init(HORIZONTAL); //makes the LCD horizontal
    LCD_1IN14_V2_Clear(BLACK); //clears the LCD initially to black 

    //ALLOCATE MEMORY FOR THE LOGO
    UDOUBLE Imagesize = LCD_1IN14_V2_HEIGHT * LCD_1IN14_V2_WIDTH; //allocates the dimension of the image
    UWORD *BlackImage; //pointer to store the address of the memory dedicated for the image
    if ((BlackImage = (UWORD *)malloc(Imagesize)) == NULL) { //if creation of memory failed in heap
        printf("Failed to allocate uwu ...\r\n");
        exit(0);
    }
    multicore_fifo_push_blocking(FLAG_VALUE);   //sent message to core 0 , about ack to operate
    
    //SYNC WITH CORE 0
    // Wait for acknowledgment from core 0
    uint32_t ack = multicore_fifo_pop_blocking();
    if (ack != FLAG_VALUE) //if no ack received from core 0 , error
        printf("Error: Core 1 failed to receive acknowledgment from core 0!\n");
    else //if ack received from  core 0 , success
        printf("Success: Core 1 Received acknowledgment from core 0!\n");

    //IMAGE CACHE (CREATING IMAGE CANVAS AND DRAWING REALITY LABS LOGO!)
    Paint_NewImage((UBYTE *)BlackImage, LCD_1IN14_V2.WIDTH, LCD_1IN14_V2.HEIGHT, 0, WHITE); //function to create a new canvas
    Paint_SetScale(65);
    Paint_SetRotate(ROTATE_0);
    Paint_DrawImage(realityLabsLogo, 0, 0, 240, 135);
    LCD_1IN14_V2_Display(BlackImage);

    //INITIALIZE THE CAM
    //config->image_buf = image_buf;
    //config->image_buf_size = sizeof(image_buf);

    struct cam_config config;
    cam_config_struct(&config);
    cam_init(&config);

    //MAIN LOOP FOR IMAGE PROCESSING
     while (true) {
        cam_capture_frame(&config);
        uint16_t index = 0;
        for (int y = 134; y > 0; y--) {
            for (int x = 0; x < 240; x++) {
                uint16_t c = image_buf[(y)*324+(x)]; //index to the current pixel
                uint16_t imageRGB = (((c & 0xF8) << 8) | ((c & 0xFC) << 3) | ((c & 0xF8) >> 3));
                // BIT MASKING
                /* Red occupies the 15-11 bit position  , Green occupies 10-5 bit position , Blue occupies 4-0 bit position
                */
                displayBuf[index++] = (imageRGB >> 8) | (imageRGB << 8);
            }
        }
        imageReady = 1;
    }
}


int main() 
{
    //CORE 0 -------------------------------------------
    //INITIALIZE AND CONFIGURE
    stdio_init_all(); // initializes the standard input/output 
    vreg_set_voltage(VREG_VOLTAGE_1_10); //sets the voltage regulator to 1.10V
    set_sys_clock_khz(250000, true); //sets the system clock to 250 MHz

    //WAIT FOR USB CONNECTION
    int loops = 20; 
    // tud_cdc_connected() is a part of TinyUSB library
    while (!tud_cdc_connected())  // Loop to wait for 2 seconds (20*100ms)
    {
        sleep_ms(100);
        if (--loops == 0)
            break;
    }
    printf("tud_cdc_connected(%d)\n", tud_cdc_connected() ? 1 : 0);

    //LAUNCH CORE 1
    multicore_launch_core1(core1_entry);

    //SYNC WITH CORE 1
    uint32_t ack = multicore_fifo_pop_blocking(); // waits for message from core 1
    if (ack != FLAG_VALUE)
        printf("Error: Core 0 failed to receive acknowledgment from core 1!\n");
    else {
        multicore_fifo_push_blocking(FLAG_VALUE); //sends message to other core
        printf("Success: Core 0 Received acknowledgment from core 1!\n");
    }

    //MAIN LOOP
    while (true) {
        if (imageReady == 1)  // if image is ready
        {
            LCD_1IN14_V2_Display(displayBuf); //displays the image 
            imageReady = 0; //sets imageready flag to 0
        }
        sleep_ms(1);
    }
    tight_loop_contents(); //placeholder for the loop(imageready one?) "tight"
}

