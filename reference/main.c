/*****************************************************************************/
/*                                                                           */
/* FILENAME                                                                  */
/*   main.c                                                                  */
/*                                                                           */
/* DESCRIPTION                                                               */
/*   TMS320C5515 USB Stick Application.                                      */
/*   Audio Loopback from Microphone In --> to Headphones/Lineout.            */
/*   Buttons, stereo in and stereo out.                                      */
/*   Mode of processing is controlled by push buttons.                       */
/*                                                                           */
/*   Author  : Areshchenkov Dmitriy                                          */
/*   2th Jule 2021. Created from TMS320C5515 USB Stick code.                 */
/*                                                                           */
/*****************************************************************************/

#include "stdio.h"
#include "usbstk5515.h"
#include "usbstk5515_led.h"
#include "aic3204.h"
#include "PLL.h"
#include "bargraph.h"
#include "oled.h"
#include "pushbuttons.h"
#include "stereo.h"
#include "dsplib.h"

Int16 left_input;
Int16 right_input;
Int16 left_output;
Int16 right_output;
Int16 mono_input;

#define SAMPLES_PER_SECOND 48000

/* Use 20 for guitar, 30 for Microphone and 0 for Line Input */
#define GAIN_IN_dB 30

unsigned long int i = 0; // index for cycles
unsigned long int j = 0; // index for cycles

unsigned int Step = 0; 
unsigned int LastStep = 99;

void main( void ) 
{
    /* Initialize BSL */
    USBSTK5515_init( );
	/* Initialize PLL */
	pll_frequency_setup(120);
    /* Initialise hardware interface and I2C for code */
    aic3204_hardware_init();
    /* Initialise the AIC3204 codec */
	aic3204_init(); 
	/* Turn off the 4 coloured LEDs */
	USBSTK5515_ULED_init();
	/* Initialise the OLED LCD display */
	oled_init();
	SAR_init();
	/* Flush display buffer */
    oled_display_message_5x7("                 ", "                   ");
    
    printf("\n\nRunning Project LED, OLED, Button\n");
    printf( "<-> Audio Loopback from Microphone In --> to Headphones/Lineout\n\n" );

	/* Setup sampling frequency and gain */
    set_sampling_frequency_and_gain(SAMPLES_PER_SECOND, GAIN_IN_dB);

    // Display welcome Message
    USBSTK5515_waitusec(1000000);
    oled_display_message_5x7("Project   Audio ", "    Transmission");
    USBSTK5515_waitusec(1000000);
    oled_display_message_5x7("Use buttons for ", "controlling     ");
    USBSTK5515_waitusec(2000000);

    /*descriptive text */
	puts("\n Press SW1 for DOWN, SW2 for UP, SW1 + SW2 for reset\n");
	puts(" Step 1   = Straight through, no signal processing");
	puts(" Step 2   = Stereo to Mono");
	puts(" Step 3   = Left input -> right output. Right input -> left output");
	puts(" Step 4   = Amplification of input");
      
	for ( i = 0  ; i < SAMPLES_PER_SECOND * 600L  ;i++  )
	{
	 aic3204_codec_read(&left_input, &right_input); // Reading of inputs
	 mono_input = stereo_to_mono(left_input, right_input); // mono = left + right / 2
	 Step = pushbuttons_read(4); // transmit number of Steps or States
     // and read current Step
	  if ( Step == 1 )
		{
		 if ( Step != LastStep )
		  {
		   oled_display_message_5x7("Step 1:         ", " No Processing  ");
		   LastStep = Step;
		   USBSTK5515_ULED_setall(0x0E);
		  }
		 left_output = left_input;      // Straight trough. No processing.
		 right_output = right_input;
		}
	  else if ( Step == 2)
		{
		  if ( Step != LastStep)
		  {
		   oled_display_message_5x7("Step 2:         ", " Stereo to Mono ");
		   LastStep = Step;
		   USBSTK5515_ULED_setall(0x0D);
		  }
		  left_output =  mono_input;    // Stereo to mono
		  right_output = mono_input;
		}
	  else if ( Step == 3)
		{
		  if ( Step != LastStep)
		  {
		   oled_display_message_5x7("Step 3:         ", " L-->R , R-->L  ");
		   LastStep = Step;
		   USBSTK5515_ULED_setall(0x0C);
		  }
		  left_output = right_input;    // Swap left and right channels
		  right_output = left_input;
		}  
	  else if ( Step == 4)
          {
            if ( Step != LastStep)
            {
             oled_display_message_5x7("Step 4:         ", " Amplification  ");
             LastStep = Step;
             USBSTK5515_ULED_setall(0x0B);
            }
            right_output = 2*right_input;    // Multiply input signal by 2
            left_output = 2*left_input;
          }
	 aic3204_codec_write(left_output, right_output); // Writing in outputs
	}
   /* Disable I2S and put codec into reset */ 
	aic3204_disable();
	printf( "\n***Program has Terminated***\n" );
	oled_display_message("PROGRAM HAS        ", "TERMINATED        ");
	SW_BREAKPOINT;
}
