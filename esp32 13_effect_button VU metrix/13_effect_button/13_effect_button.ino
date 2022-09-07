// (Heavily) adapted from https://github.com/G6EJD/ESP32-8266-Audio-Spectrum-Display/blob/master/ESP32_Spectrum_Display_02.ino
// Adjusted to allow brightness changes on press+hold, Auto-cycle for 3 button presses within 2 seconds
// Edited to add Neomatrix support for easier compatibility with different layouts.

#include <FastLED_NeoMatrix.h>
#include <arduinoFFT.h>
#include <EasyButton.h>

#define SAMPLES         512            // Must be a power of 2
#define SAMPLING_FREQ   30000     // Hz, must be 40000 or less due to ADC conversion time. Determines maximum frequency that can be analysed by the FFT Fmax=sampleF/2.
#define AMPLITUDE       1000          // Depending on your audio source level, you may need to alter this value. Can be used as a 'sensitivity' control.
#define AUDIO_IN_PIN    35            // Signal in on this pin
#define LED_PIN         5             // LED strip data
#define BTN_PIN         4             // Connect a push button to this pin to change patterns
#define LONG_PRESS_MS   400           // Number of ms to count as a long press
#define COLOR_ORDER     GRB           // If colours look wrong, play with this
#define CHIPSET         WS2812B       // LED strip type
#define MAX_MILLIAMPS   4000          // Careful with the amount of power here if running off USB port
const int BRIGHTNESS_SETTINGS[3] = {10, 60, 255};  // 3 Integer array for 3 brightness settings (based on pressing+holding BTN_PIN)
#define LED_VOLTS       5             // Usually 5 or 12
#define NUM_BANDS       16            // To change this, you will need to change the bunch of if statements describing the mapping from bins to bands
#define NOISE           165           // Used as a crude noise filter, values below this are ignored
const uint8_t kMatrixWidth = 16;                          // Matrix width
const uint8_t kMatrixHeight = 11;                         // Matrix height
#define NUM_LEDS       (kMatrixWidth * kMatrixHeight)     // Total number of LEDs
#define BAR_WIDTH      (kMatrixWidth  / (NUM_BANDS - 1))  // If width >= 8 light 1 LED width per bar, >= 16 light 2 LEDs width bar etc
#define TOP            (kMatrixHeight - 0)                // Don't allow the bars to go offscreen
#define SERPENTINE     true                               // Set to false if you're LEDS are connected end to end, true if serpentine

// Sampling and FFT stuff
unsigned int sampling_period_us;
byte peak[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};              // The length of these arrays must be >= NUM_BANDS
int oldBarHeights[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int bandValues[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
double vReal[SAMPLES];
double vImag[SAMPLES];
unsigned long newTime;
arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

// Button stuff
int buttonPushCounter = 0;
bool autoChangePatterns = false;
EasyButton modeBtn(BTN_PIN);

// FastLED stuff
CRGB leds[NUM_LEDS];
/****************************************************************************
 * Colors of bars and peaks in different modes, changeable to your likings  *
 ****************************************************************************/
// Colors mode 0
#define ChangingBar_Color   x * (255 / kMatrixHeight) + colorTimer, 255, 255
// no peaks

// Colors mode 1 These are the colors from the TRIBAR when using Ledstrip
#define TriBar_Color_Top      0 , 255, 255    // Red CHSV
#define TriBar_Color_Bottom   95 , 255, 255   // Green CHSV
#define TriBar_Color_Middle   45, 255, 255    // Yellow CHSV

#define TriBar2_RGB_Top      0 , 0, 255    // Red CHSV
#define TriBar2_RGB_Bottom   255 , 255, 255   // Green CHSV
#define TriBar2_RGB_Middle   0, 179, 116    // Yellow CHSV

#define TriBar_Color_Top_Peak      0 , 255, 255    // Red CHSV
#define TriBar_Color_Bottom_Peak   95 , 255, 255   // Green CHSV
#define TriBar_Color_Middle_Peak   45, 255, 255    // Yellow CHSV

#define TriBar2_RGB_Top_Peak      0 , 0, 255    // Red CHSV
#define TriBar2_RGB_Bottom_Peak   255 , 255, 255   // Green CHSV
#define TriBar2_RGB_Middle_Peak   0, 255, 255    // Yellow CHSV


// Colors mode 2
#define RainbowBar_Color  (x / BAR_WIDTH) * (255 / NUM_LEDS), 255, 255
#define PeakColor1  0, 0, 255       // white CHSV

// Colors mode 3
#define PeakColor2  0, 0, 255       // white CHSV



DEFINE_GRADIENT_PALETTE( purple_gp ) {
  0,   0, 212, 255,   //blue
255, 179,   0, 255 }; //purple
CRGBPalette16 purplePal = purple_gp;


// Colors mode 4
#define SameBar_Color1      0 , 255, 255      //red  CHSV
#define PeakColor3  160, 255, 255   // blue CHSV    0, 0, 255
#define PeakColor6  0, 0, 255
#define PeakColor7  0, 255, 255


// Colors mode 5
#define SameBar_Color2      160 , 255, 255    //blue  CHSV
#define PeakColor4  0, 255, 255   // red CHSV



// Colors mode 6
DEFINE_GRADIENT_PALETTE( redyellow_gp ) {  
  0,   200, 200,  200,   //white
 64,   255, 218,    0,   //yellow
128,   231,   0,    0,   //red
192,   255, 218,    0,   //yellow
255,   200, 200,  200 }; //white
CRGBPalette16 heatPal = redyellow_gp;
// no peaks

// Colors mode 7
DEFINE_GRADIENT_PALETTE( outrun_gp ) {
  0, 141,   0, 100,   //purple
127, 255, 192,   0,   //yellow
255,   0,   5, 255 };  //blue
CRGBPalette16 outrunPal = outrun_gp;
// no peaks

// Colors mode 8
DEFINE_GRADIENT_PALETTE( mark_gp2 ) {
  0,   255,   218,    0,   //Yellow
 64,   200, 200,    200,   //white
128,   141,   0, 100,   //pur
192,   200, 200,    200,   //white
255,   255,   218,    0,};   //Yellow
CRGBPalette16 markPal2 = mark_gp2;

// Colors mode 9
// no bars only peaks
DEFINE_GRADIENT_PALETTE( mark_gp ) {
  0,   231,   0,    0,   //red
 64,   200, 200,    200,   //white
128,   200, 200,    200,   //white
192,   200, 200,    200,   //white
255,   231, 0,  0,};   //red
CRGBPalette16 markPal = mark_gp;

// Colors mode 10
// no bars only peaks
#define PeakColor5  160, 255, 255   // blue CHSV
#define PeakColor8    255 , 255, 255
// These are the colors from the TRIPEAK mode 11
// no bars
#define TriBar_Color_Top_Peak2      255 , 0, 0    // Red CHSV
#define TriBar_Color_Bottom_Peak2   0 , 255, 0   // Green CHSV
#define TriBar_Color_Middle_Peak2   45, 0, 255    // Yellow CHSV

uint8_t colorTimer = 0;

// FastLED_NeoMaxtrix - see https://github.com/marcmerlin/FastLED_NeoMatrix for Tiled Matrixes, Zig-Zag and so forth
FastLED_NeoMatrix *matrix = new FastLED_NeoMatrix(leds, kMatrixWidth, kMatrixHeight,
  NEO_MATRIX_BOTTOM        + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS       + NEO_MATRIX_ZIGZAG +
  NEO_TILE_BOTTOM + NEO_TILE_LEFT + NEO_TILE_COLUMNS);

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalSMD5050);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_VOLTS, MAX_MILLIAMPS);
  FastLED.setBrightness(BRIGHTNESS_SETTINGS[1]);
  FastLED.clear();

  modeBtn.begin();
  modeBtn.onPressed(changeMode);
  modeBtn.onPressedFor(LONG_PRESS_MS, brightnessButton);
  modeBtn.onSequence(3, 2000, startAutoMode);
  modeBtn.onSequence(5, 2000, brightnessOff);
  sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQ));
}

void changeMode() {
  Serial.println("Button pressed");
  if (FastLED.getBrightness() == 0) FastLED.setBrightness(BRIGHTNESS_SETTINGS[0]);  //Re-enable if lights are "off"
  autoChangePatterns = false;
  buttonPushCounter = (buttonPushCounter + 1) % 15;
}

void startAutoMode() {
  autoChangePatterns = true;
}

void brightnessButton() {
  if (FastLED.getBrightness() == BRIGHTNESS_SETTINGS[2])  FastLED.setBrightness(BRIGHTNESS_SETTINGS[0]);
  else if (FastLED.getBrightness() == BRIGHTNESS_SETTINGS[0]) FastLED.setBrightness(BRIGHTNESS_SETTINGS[1]);
  else if (FastLED.getBrightness() == BRIGHTNESS_SETTINGS[1]) FastLED.setBrightness(BRIGHTNESS_SETTINGS[2]);
  else if (FastLED.getBrightness() == 0) FastLED.setBrightness(BRIGHTNESS_SETTINGS[0]); //Re-enable if lights are "off"
}

void brightnessOff(){
  FastLED.setBrightness(0);  //Lights out
}

void loop() {

  // Don't clear screen if waterfall pattern, be sure to change this is you change the patterns / order
  if (buttonPushCounter != 13) FastLED.clear();

  modeBtn.read();

  // Reset bandValues[]
  for (int i = 0; i<NUM_BANDS; i++){
    bandValues[i] = 0;
  }

  // Sample the audio pin
  for (int i = 0; i < SAMPLES; i++) {
    newTime = micros();
    vReal[i] = analogRead(AUDIO_IN_PIN); // A conversion takes about 9.7uS on an ESP32
    vImag[i] = 0;
    while ((micros() - newTime) < sampling_period_us) { /* chill */ }
  }

  // Compute FFT
  FFT.DCRemoval();
  FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(FFT_FORWARD);
  FFT.ComplexToMagnitude();

  // Analyse FFT results
  for (int i = 2; i < (SAMPLES/2); i++){       // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.
    if (vReal[i] > NOISE) {                    // Add a crude noise filter

   
        //16 bands, 12kHz top band  
      if (i<=2 )           bandValues[0]  += (int)vReal[i];
      if (i>2   && i<=3  ) bandValues[1]  += (int)vReal[i];
      if (i>3   && i<=4  ) bandValues[2]  += (int)vReal[i];
      if (i>4   && i<=5  ) bandValues[3]  += (int)vReal[i];
      if (i>5   && i<=7  ) bandValues[4]  += (int)vReal[i];
      if (i>7   && i<=10 ) bandValues[5]  += (int)vReal[i];
      if (i>10  && i<=13 ) bandValues[6]  += (int)vReal[i];
      if (i>13  && i<=18 ) bandValues[7]  += (int)vReal[i];
      if (i>18  && i<=25 ) bandValues[8]  += (int)vReal[i];
      if (i>25  && i<=34 ) bandValues[9]  += (int)vReal[i];
      if (i>34  && i<=46 ) bandValues[10] += (int)vReal[i];
      if (i>46  && i<=63 ) bandValues[11] += (int)vReal[i];
      if (i>63  && i<=87 ) bandValues[12] += (int)vReal[i];
      if (i>87  && i<=118 ) bandValues[13] += (int)vReal[i];
      if (i>118 && i<=162 ) bandValues[14] += (int)vReal[i];
      if (i>162 v && i<=222 ) bandValues[15] += (int)vReal[i];
      if (i>303  )          bandValues[16] += (int)vReal[i];
        
    }
  }

  // Process the FFT data into bar heights
  for (byte band = 0; band < NUM_BANDS; band++) {

    // Scale the bars for the display
    int barHeight = bandValues[band] / AMPLITUDE;
    if (barHeight > TOP) barHeight = TOP;

    // Small amount of averaging between frames
    barHeight = ((oldBarHeights[band] * 1) + barHeight) / 2;

    // Move peak up
    if (barHeight > peak[band]) {
      peak[band] = min(TOP, barHeight);
    }

    switch (buttonPushCounter) {
    case 0:
     
      changingBarsLS(band, barHeight);
     
     break;     
    case 1: 
     
     TriBarLS(band, barHeight);
     TriPeakLS(band);
     
     break;
    case 2:
     
      rainbowBarsLS(band, barHeight);
      NormalPeakLS(band, PeakColor1);
     
      break;
     case 3:
     
      SameBar2LS(band, barHeight); 
      NormalPeakLS(band, PeakColor6);
    
     break;
    case 4:
     
      centerBarsLS(band, barHeight);
     
      break;
       case 5:
    
      SameBar2LS(band, barHeight); 
      NormalPeakLS(band, PeakColor7);
     
     break;
        case 6:
    
      SameBar2LS(band, barHeight); 
      NormalPeakLS(band, PeakColor2);
     
     break;
      case 7:
     
      SameBarLS(band, barHeight); 
      NormalPeakLS(band, PeakColor3);
     
      break;
    case 8:
     
      centerBars2LS(band, barHeight);
     
      break;
    case 9:
     
      centerBars3LS(band, barHeight);
     
      break;
    case 10:
     
       BlackBarLS(band, barHeight);
       outrunPeakLS(band);
     
     break;
    case 11:
    
     
      BlackBarLS(band, barHeight);
      NormalPeakLS(band, PeakColor5);
    
      break;
       case 12:
     
      BlackBarLS(band, barHeight);
      NormalPeakLS(band, PeakColor6);
    
      break;
    case 13:
     
      BlackBar1LS(band, barHeight);
      TriPeakLS(band);
     
      break;

    
      case 14:
     
      changingBarsLS1(band, barHeight);
     NormalPeakLS(band, PeakColor1);
      break;
  } 
  
  

    // Save oldBarHeights for averaging later
    oldBarHeights[band] = barHeight;
  }

  // Decay peak
  EVERY_N_MILLISECONDS(85) {
    for (byte band = 0; band < NUM_BANDS; band++)
      if (peak[band] > 0) peak[band] -= 1;
    colorTimer++;
  }

  // Used in some of the patterns
  EVERY_N_MILLISECONDS(3) {
    colorTimer++;
  }

  EVERY_N_SECONDS(10) {
    if (autoChangePatterns) buttonPushCounter = (buttonPushCounter + 1) % 15;
  }

  FastLED.show();
}

// PATTERNS BELOW //

//************ Mode 0 ***********
 void changingBarsLS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix -> drawPixel(x, y, CHSV(ChangingBar_Color));
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}

//************ Mode 1 ***********
 void TriBarLS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        if (y < (kMatrixHeight/3)) matrix -> drawPixel(x, y, CHSV(TriBar_Color_Top));     //Top red
      else if (y > (1 *kMatrixHeight/2)) matrix -> drawPixel(x, y, CHSV(TriBar_Color_Bottom)); //green
      else matrix -> drawPixel(x, y, CHSV(TriBar_Color_Middle));      //yellow
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}

void TriPeakLS(int band) {
  int xStart = BAR_WIDTH * band;
  int peakHeight = TOP - peak[band] - 1;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    if (peakHeight < 4) matrix -> drawPixel(x, peakHeight, CHSV(TriBar_Color_Top_Peak)); //Top red
    else if (peakHeight > 8) matrix -> drawPixel(x, peakHeight, CHSV(TriBar_Color_Bottom_Peak)); //green
    else matrix -> drawPixel(x, peakHeight, CHSV(TriBar_Color_Middle_Peak)); //yellow
  }
}
//************ Mode 2 ***********
 void rainbowBarsLS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix -> drawPixel(x, y, CHSV(RainbowBar_Color));
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}

void NormalPeakLS(int band, int H, int S, int V) {
  int xStart = BAR_WIDTH * band;
  int peakHeight = TOP - peak[band] - 1;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    matrix -> drawPixel(x, peakHeight, CHSV(H, S, V));
  }
}

//************ Mode 3 ***********

void purpleBarsLS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix -> drawPixel(x, y, ColorFromPalette(purplePal, y * (255 / (barHeight + 1))));
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}

// for peaks see mode 2

//************ Mode 4 ***********

void SameBarLS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix -> drawPixel(x, y, CHSV(SameBar_Color1)); //blue
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}
// for peaks see mode 2

//************ Mode 5 ***********
void SameBar2LS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix -> drawPixel(x, y, CHSV(SameBar_Color2)); //blue
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}
// for peaks see mode 2

//************ Mode 6 ***********
void centerBarsLS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  int center= TOP/2;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    if (barHeight % 2 == 0) barHeight--;
    if (barHeight < 0) barHeight = 1; // at least a white line in the middle is what we want
    int yStart = ((kMatrixHeight - barHeight) / 2);
    for (int y = yStart; y <= (yStart + barHeight); y++) {
      int colorIndex = constrain((y - yStart) * (255 / barHeight), 0, 255);
      matrix -> drawPixel(x, y, ColorFromPalette(heatPal, colorIndex));
    }
    for (int y= barHeight/2;y<TOP;y++){
      matrix->drawPixel(x, center+y+1, CRGB(0, 0, 0)); // make unused pixel bottom black
      matrix->drawPixel(x, center-y-2, CRGB(0, 0, 0)); // make unused pixel bottom black
    }
    
  }
}

//************ Mode 7 ***********
void centerBars2LS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  int center= TOP/2;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    if (barHeight % 2 == 0) barHeight--;
    if (barHeight < 0) barHeight = 1; // at least a white line in the middle is what we want
    int yStart = ((kMatrixHeight - barHeight) / 2);
    for (int y = yStart; y <= (yStart + barHeight); y++) {
      int colorIndex = constrain((y - yStart) * (255 / barHeight), 0, 255);
      matrix -> drawPixel(x, y, ColorFromPalette(markPal, colorIndex));
    }
    for (int y= barHeight/2;y<TOP;y++){
      matrix->drawPixel(x, center+y+1, CRGB(0, 0, 0)); // make unused pixel bottom black
      matrix->drawPixel(x, center-y-2, CRGB(0, 0, 0)); // make unused pixel bottom black
    }
    
  }
}

//************ Mode 8 ***********
void centerBars3LS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  int center= TOP/2;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    if (barHeight % 2 == 0) barHeight--;
    if (barHeight < 0) barHeight = 1; // at least a white line in the middle is what we want
    int yStart = ((kMatrixHeight - barHeight) / 2);
    for (int y = yStart; y <= (yStart + barHeight); y++) {
      int colorIndex = constrain((y - yStart) * (255 / barHeight), 0, 255);
      matrix -> drawPixel(x, y, ColorFromPalette(markPal2, colorIndex));
    }
    for (int y= barHeight/2;y<TOP;y++){
      matrix->drawPixel(x, center+y+1, CRGB(0, 0, 0)); // make unused pixel bottom black
      matrix->drawPixel(x, center-y-2, CRGB(0, 0, 0)); // make unused pixel bottom black
    }
    
  }
}
//************ Mode 9 ***********
void BlackBarLS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix->drawPixel(x, y, CRGB(5, 5, 5)); // make unused pixel in a band black
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}

void changingBarsLS1(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix -> drawPixel(x, y, CHSV(ChangingBar_Color));
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}

void BlackBar1LS(int band, int barHeight) {
  int xStart = BAR_WIDTH * band;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    for (int y = TOP; y >= 0; y--) {
     if(y >= TOP - barHeight){
        matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
     else {
      matrix->drawPixel(x, y, CRGB(0, 0, 0)); // make unused pixel in a band black
     }
    } 
  }
}
void outrunPeakLS(int band) {
  int xStart = BAR_WIDTH * band;
  int peakHeight = TOP - peak[band] - 1;
  for (int x = xStart; x < xStart + BAR_WIDTH; x++) {
    matrix -> drawPixel(x, peakHeight, ColorFromPalette(outrunPal, peakHeight * (255 / kMatrixHeight)));
  }
}
