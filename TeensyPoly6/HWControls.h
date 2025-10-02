// This optional setting causes Encoder to use more optimized code,
// It must be defined before Encoder.h is included.
#define ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <Bounce.h>
#include "TButton.h"
#include <ADC.h>
#include <ADC_util.h>

ADC *adc = new ADC();

//Teensy 3.6 - Mux Pins

#define MUX1 28
#define MUX2 29
#define MUX3 30

//Teensy 3.6 Pins

#define DEBOUNCE 30
#define RECALL_SW 24
#define SAVE_SW 7
#define SETTINGS_SW 8
#define BACK_SW 6

#define ENCODER_PINA 4
#define ENCODER_PINB 5


#define DEBOUNCE 30

// LEDs

#define MUXCHANNELS 8
#define QUANTISE_FACTOR 7

static byte muxInput = 0;

static int mux1ValuesPrev[MUXCHANNELS] = {};
static int mux2ValuesPrev[MUXCHANNELS] = {};
static int mux3ValuesPrev[MUXCHANNELS] = {};
static int mux4ValuesPrev[MUXCHANNELS] = {};
static int mux5ValuesPrev[MUXCHANNELS] = {};
static int mux6ValuesPrev[MUXCHANNELS] = {};

static int mux1Read = 0;
static int mux2Read = 0;
static int mux3Read = 0;
static int mux4Read = 0;
static int mux5Read = 0;
static int mux6Read = 0;


static long encPrevious = 0;

int mux0;
int mux1;
int mux2;
int mux3;
int oldmux3;
int mux4;
int mux5;
int mux6;
int mux7;
int mux8;
int mux9;
int mux10;
int mux11;
int mux12;
int mux13;
int mux14;
int mux15;
int mux16;
int mux17;
int mux18;
int mux19;
int mux20;
int mux21;
int mux22;
int mux23;
int mux24;
int mux25;
int mux26;
int mux27;
int mux28;
int mux29;
int mux30;
int FILTER_MODE;
int MONO_POLY_1;
int MONO_POLY_2;
int A_SHAPE_1;
int A_SHAPE_2;
int B_SHAPE_1;
int B_SHAPE_2;
int MAIN_OCT_1;
int MAIN_OCT_2;
int B_OCTAVE_1;
int B_OCTAVE_2;
int C_OCTAVE_1;
int C_OCTAVE_2;
int LFOA_DEST_1;
int LFOA_DEST_2;
int LFOA_SHAPE_1;
int LFOA_SHAPE_2;


//These are pushbuttons and require debouncing

TButton saveButton{ SAVE_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton settingsButton{ SETTINGS_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton backButton{ BACK_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION };
TButton recallButton{ RECALL_SW, LOW, HOLD_DURATION, DEBOUNCE, CLICK_DURATION }; // on encoder

Encoder encoder(ENCODER_PINB, ENCODER_PINA);//This often needs the pins swapping depending on the encoder

void setupHardware()
{
  adc->adc0->setAveraging(16);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc0->setResolution(10);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  //MUXs on ADC1
  adc->adc1->setAveraging(16);                                          // set number of averages 0, 4, 8, 16 or 32.
  adc->adc1->setResolution(10);                                         // set bits of resolution  8, 10, 12 or 16 bits.
  adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);  // change the conversion speed
  adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);           // change the sampling speed

  analogWriteResolution(10);
  analogReadResolution(10);


  //Mux address pins

  pinMode(MUX1, OUTPUT);
  pinMode(MUX2, OUTPUT);
  pinMode(MUX3, OUTPUT);


  digitalWrite(MUX1, LOW);
  digitalWrite(MUX2, LOW);
  digitalWrite(MUX3, LOW);

  analogReadResolution(10);

  //Switches
  pinMode(RECALL_SW, INPUT_PULLUP); //On encoder
  pinMode(SAVE_SW, INPUT_PULLUP);
  pinMode(SETTINGS_SW, INPUT_PULLUP);
  pinMode(BACK_SW, INPUT_PULLUP);

}
