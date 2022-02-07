/*
______  _____ _____ _____ _____ ______ _____  ___  ___  ___ ___________
| ___ \|  _  |_   _/  ___/  __ \| ___ \  ___|/ _ \ |  \/  ||  ___| ___ \
| |_/ /| | | | | | \ `--.| /  \/| |_/ / |__ / /_\ \| .  . || |__ | |_/ /
| ___ \| | | | | |  `--. \ |    |    /|  __||  _  || |\/| ||  __||    /
| |_/ /\ \_/ / | | /\__/ / \__/\| |\ \| |___| | | || |  | || |___| |\ \
\____/  \___/  \_/ \____/ \____/\_| \_\____/\_| |_/\_|  |_/\____/\_| \_|
CREATED BY FLY&I (flyandi.net)
Learn more at https://github.com/flyandi/botscreamer

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// -----------------------------------------------------------------------
// configuration, calibration

// Volume, 0 is off, 0.1 - 3.99 where 3.99 is the loudest possible
#define VOLUME 3.99

// Roll rate is used to determinated upside down flying
// Upside down is true when the value is < ROLL_LOW || > ROLL_HIGH,
#define ROLL_LOW -160 // -160 degree is the default
#define ROLL_HIGH 160 // 160 degree is the default

// Pitch rate is used to enable/disable certain functions
// Pitching forward enables it, and pitching backwards disables it
// Becomes true if it's within the range of min and max
#define PITCH_MIN 40
#define PITCH_MAX 60

// disable for production use
//#define DEBUG

// -----------------------------------------------------------------------
// DO NOT CHANGE ANYTHING BEYOND THIS PART
// ONLY IF YOU KNOW WHAT YOU DOING!

// includes
#include "ESP8266WiFi.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorRTTTL.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2S.h"
#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>

// ltm config
#define LTM_RX_PIN D5
#define LTM_TX_PIN D6
#include "ltm.h"

// button config
#define BUTTON_PIN D0
#define BUTTON_MIN 300

// led config
#define LED_PIN D7
#define LED_PIXELS 6
#define LED_BRIGHTNESS 255

#define LED_PROGRAM_DEFAULT 0
#define LED_PROGRAM_POLICE 1
#define LED_PROGRAM_HUE_HEADING 2

// on selector
#define ON_ROLL 0
#define ON_ROLL_TIMER 1200 // at least keep it 1.2 seconds upside down
#define ON_PITCH 1
#define ON_HEADING 2

// actual modes
//
struct
{
  char *file;
  uint8_t on;
  bool loop;
  uint8_t led_program;
  uint16_t led_time;
} MODES[] = {
    {"/goat.wav", ON_ROLL, false, LED_PROGRAM_DEFAULT, 50},
    {"/human.wav", ON_ROLL, false, LED_PROGRAM_DEFAULT, 50},
    {"/goose.wav", ON_PITCH, false, LED_PROGRAM_HUE_HEADING, 50},
    {"/police.wav", ON_PITCH, true, LED_PROGRAM_POLICE, 120},
    {"", ON_HEADING, true, LED_PROGRAM_HUE_HEADING, 50},
};

#define NUM_MODES 5
#define MODE_SYNTH 4

// synth
const char *SYNTH_NOTES[] = {
    "a",
    "b",
    "c",
    "d",
    "e",
    "f",
    "g",
    "a#",
    "b#",
    "c#",
    "d#",
    "e#",
    "f#",
    "g#"};

#define NUM_SYNTH_NOTES 14

// locals
AudioGeneratorWAV *wav;
AudioFileSourceSPIFFS *file;
AudioFileSourcePROGMEM *rts;
AudioOutputI2S *out;
AudioGeneratorRTTTL *rtttl;
Adafruit_NeoPixel pixels(LED_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

bool button_p = false;
bool led_enabled = true;
bool on_enabled = false;
uint8_t mode = 0;
uint16_t led_state = 0;
uint32_t button_t = 0;
uint32_t led_t = 0;
uint32_t on_t = 0;

#ifdef DEBUG
uint32_t debug_t = 0;
#endif

// setup
void setup()
{
  // start with pixels
  pixels.begin();
  pixels.show();
  pixels.setBrightness(LED_BRIGHTNESS);
  wipe(0xFF0000, true);

  // boot up
  WiFi.forceSleepBegin();
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  out = new AudioOutputI2S();
  wav = new AudioGeneratorWAV();
  rtttl = new AudioGeneratorRTTTL();
  out->SetGain(VOLUME);
  ltm_begin();

  // cleanup
  delay(1000);
  wipe(0x00FF00, true);
  delay(1000);
}

// wipe
void wipe(uint32_t color, bool show)
{
  for (int i = 0; i < pixels.numPixels(); i++)
  {
    pixels.setPixelColor(i, color);
  }
  if (show)
  {
    pixels.show();
  }
}

// audio
void audio()
{
  if (rtttl->isRunning())
  {
    rtttl->stop();
  }
  if (wav->isRunning())
  {
    wav->stop();
  }
  if (mode != MODE_SYNTH)
  {
    file = new AudioFileSourceSPIFFS(MODES[mode].file);
    wav->begin(file, out);
  }
}

// cycle_mode
void cycle_mode()
{
  mode += 1;
  if (mode >= NUM_MODES)
  {
    mode = 0;
  }
  led_state = 0;
  on_enabled = false;
  audio();
}

// audio_loop
void audio_loop()
{
  if (mode == MODE_SYNTH)
  {
    if (rtttl->isRunning())
    {
      if (!rtttl->loop())
      {
        rtttl->stop();
      }
    }
    else
    {

      char tone[32];
      uint8_t note = map(LTM_DATA.heading, 0, 360, 0, NUM_SYNTH_NOTES - 1);
      if (note < 0 || note >= NUM_SYNTH_NOTES)
      {
        note = 0;
      }
      strcat(tone, "A:d=4,o=1,b=120:");
      strcat(tone, SYNTH_NOTES[note]);
#ifdef DEBUG
      Serial.printf("Note: heading %d / note %d %s\n", LTM_DATA.heading, note, SYNTH_NOTES[note]);
#endif
      rts = new AudioFileSourcePROGMEM(tone, strlen_P(tone));
      rtttl->begin(rts, out);
    }
    return;
  }

  if (wav->isRunning())
  {
    if (!wav->loop())
    {
      wav->stop();
      if (MODES[mode].loop && on_enabled)
      {
        audio();
      }
    }
  }
  else
  {

    if (on_enabled)
    {
      audio();
    }
  }
}

// process_loop
void process_loop()
{
  if (MODES[mode].on == ON_ROLL)
  {
    if ((LTM_DATA.roll > ROLL_HIGH && LTM_DATA.roll < 360) || (LTM_DATA.roll < ROLL_LOW && LTM_DATA.roll > -360))
    {
      if (on_t == 0)
      {
        on_t = millis();
      }
      else
      {
        if (millis() - on_t > ON_ROLL_TIMER)
        {
          on_enabled = true;
          on_t = 0;
        }
      }
    }
    else
    {
      on_enabled = false;
    }
  }

  if (MODES[mode].on == ON_PITCH)
  {
    if (!on_enabled && LTM_DATA.pitch > PITCH_MIN && LTM_DATA.pitch < PITCH_MAX)
    {
      on_enabled = true;
    }
    else if (on_enabled && LTM_DATA.pitch < -1 * PITCH_MIN && LTM_DATA.pitch > -1 * PITCH_MAX)
    {
      on_enabled = false;
    }
  }

#ifdef DEBUG
  if (millis() - debug_t > 250)
  {
    Serial.printf("P %d / R %d / H %d / ON %d\n", LTM_DATA.pitch, LTM_DATA.roll, LTM_DATA.heading, on_enabled);
    debug_t = millis();
  }
#endif
}

// led_loop
void led_loop()
{
  if (millis() - led_t > MODES[mode].led_time)
  {
    if (MODES[mode].led_program == LED_PROGRAM_POLICE)
    {
      wipe(0x000000, false);

      if (led_state == 0 || led_state == 2)
      {
        pixels.setPixelColor(0, 0xFF0000);
        pixels.setPixelColor(1, 0xFF0000);
        pixels.setPixelColor(2, 0xFF0000);
      }
      if (led_state == 4 || led_state == 6)
      {
        pixels.setPixelColor(3, 0x0000FF);
        pixels.setPixelColor(4, 0x0000FF);
        pixels.setPixelColor(5, 0x0000FF);
      }
      led_state += 1;
      if (led_state > 7)
      {
        led_state = 0;
      }
    }

    if (MODES[mode].led_program == LED_PROGRAM_DEFAULT)
    {
      if (wav->isRunning())
      {
        wipe(led_state % 2 != 0 ? pixels.ColorHSV((65536L / 32) * led_state) : 0x000000, false);
        led_state += 1;
        if (led_state > 512)
        {
          led_state = 0;
        }
      }
      else
      {
        wipe(0x000000, false);
      }
    }

    if (MODES[mode].led_program == LED_PROGRAM_HUE_HEADING)
    {
      wipe(pixels.ColorHSV((65536L / 360) * LTM_DATA.heading), false);
    }

    // general
    pixels.show();
    led_t = millis();
  }
}

// button_loop
void button_loop()
{
  bool button = !digitalRead(BUTTON_PIN);
  if (button && !button_p)
  {
    button_p = true;
    button_t = millis();
  }
  else if (!button && button_p)
  {
    button_p = false;
    if (millis() - button_t > BUTTON_MIN)
    {
      cycle_mode();
    }
  }
}

// loop
void loop()
{
  ltm_loop();
  audio_loop();
  process_loop();
  button_loop();
  led_loop();
}
