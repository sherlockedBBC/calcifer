// ============================================================
// Calcifer Lamp
// 
// Author: Fabian Schmitt
//
// Device: ESP32-S2
// ============================================================
// Description:
// This code is for a Calcifer lamp using an ESP32-S2 microcontroller.
// ============================================================

#include <Arduino.h>
#include <TouchHandler.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <Smoothed.h>
#include <EEPROM.h>

// SETTINGS
#define LED_PIN    18                 // Pin number for the LED strip            
#define LED_NUM    25                 // Number of LEDs in the strip
#define ENABLE_AUTO_BRIGHTNESS true   // Enable or disable auto brightness adjustment

#define TOUCH_FRONT 14                // Pin number for front touch sensor
#define TOUCH_RIGHT 12                // Pin number for right side touch sensor
#define TOUCH_LEFT  13                // Pin number for left side touch sensor

#define LIGHTSENSOR_PIN 8             // Pin number for the TEMT6000 light sensor (if used)
#define LIGHTSENSOR_SMOOTHNESS 300    // Number of samples for smoothing the light sensor value
#define BRIGHTNESS_DEADBAND 0.05      // Deadband threshold for brightness changes

// Define EEPROM addresses for storing settings
#define EEPROM_SIZE 512
#define ADDR_CURRENT_BRIGHTNESS_STEP 0
#define ADDR_PREVIOUS_BRIGHTNESS_STEP 4
#define ADDR_CURRENT_BRIGHTNESS_VALUE 8
#define ADDR_PREVIOUS_BRIGHTNESS_VALUE 12
#define ADDR_CURRENT_ANIMATION_SETTING 16

// INSTANCES AND VARIABLES
const int touchPins[3] = {TOUCH_FRONT, TOUCH_LEFT, TOUCH_RIGHT};
const int numTouchPins = sizeof(touchPins) / sizeof(touchPins[0]);
TouchHandler touchHandler(touchPins, numTouchPins);

NeoPixelBus<NeoGrbFeature, NeoWs2812xMethod> strip(LED_NUM, LED_PIN);
NeoGamma<NeoGammaTableMethod> colorGamma;
NeoPixelAnimator animations(4, NEO_CENTISECONDS);


Smoothed <float> lightsensor;

float brightnessSteps[4] = {0.25, 0.5, 0.75, 1.0};
int currentBrightnessStep = 3;
int previousBrightnessStep = 2;
float currentBrightnessValue = 1.0; 
float previousBrightnessValue = 1.0;
int currentAnimantionSetting = 0; 
int currentPowerSetting = 1;
float previousEnvBrightnessValue = 1.0;

float brightnessValue = 1.0;
float powerCoeficient = 0.0;

RgbColor outputBuffer[LED_NUM] = {};

// FUNCTIONS
float getlux(float analogValue);
void showBuffer(RgbColor buffer[], int bufferSize);
void breathUpdate(AnimationParam param);
void softStart(AnimationParam param);
void softStop(AnimationParam param);
void brightnessFade(AnimationParam param);
void flameUpdate(AnimationParam param);
void restoreSettings();
void saveSettings();
void solidUpdate(AnimationParam param);
void nightFlameUpdate(AnimationParam param);

void setup() {

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  strip.Begin();
  strip.ClearTo(0);
  strip.Show();

  touchHandler.begin();
  
  Serial.begin(115200);
  delay(500);

  restoreSettings();

  lightsensor.begin(SMOOTHED_AVERAGE, LIGHTSENSOR_SMOOTHNESS);

  for(int i = 0; i < LED_NUM; i++) {
    outputBuffer[i] = RgbColor(0, 0, 0);
  }
}

void loop() {

  static unsigned long lastTouchedTime_Front = 0; // Tracks the last time a touch was detected
  static unsigned long lastTouchedTime_Left = 0; // Tracks the last time a touch was detected
  static unsigned long lastTouchedTime_Right = 0; // Tracks the last time a touch was detected
  const unsigned long debounceDelay = 350; // Debounce delay in milliseconds

  lightsensor.add(analogRead(LIGHTSENSOR_PIN));

  touchHandler.update();

  if (touchHandler.isTouched(0)) {
    unsigned long currentTime = millis();
    if (currentTime - lastTouchedTime_Front > debounceDelay) {
      lastTouchedTime_Front = currentTime; // Update the last touched time
      
      // Set Power
      if(currentPowerSetting == 0) {
        currentPowerSetting = 1;
        animations.StartAnimation(2, 75, softStart);
      } else {
        currentPowerSetting = 0;
        if(currentAnimantionSetting == 2) {
          animations.StopAnimation(1);
        }
        animations.StartAnimation(2, 75, softStop);
      }
    }
  }

  if (touchHandler.isTouched(1)) {
    unsigned long currentTime = millis();
    if (currentTime - lastTouchedTime_Left > debounceDelay) {
      lastTouchedTime_Left = currentTime; // Update the last touched time
      
      // Set Brightness
      #ifdef ENABLE_AUTO_BRIGHTNESS

      if(currentBrightnessStep == 4) {
        currentBrightnessStep = 0;
      } else {
        currentBrightnessStep++;
      }

      if(currentBrightnessStep == 0) {
        previousBrightnessStep = 3;
      } else {
        previousBrightnessStep = currentBrightnessStep - 1;
      }

      currentBrightnessValue = brightnessSteps[currentBrightnessStep];
      previousBrightnessValue = brightnessValue;

      if(currentBrightnessStep < 4) {
        animations.StartAnimation(3, 50, brightnessFade);
      }

      #elif

      if(currentBrightnessStep == 3) {
        currentBrightnessStep = 0;
      } else {
        currentBrightnessStep++;
      }

      currentBrightnessValue = brightnessSteps[currentBrightnessStep];
      previousBrightnessValue = brightnessSteps[previousBrightnessStep];
      
      animations.StartAnimation(3, 50, brightnessFade);

      #endif

      saveSettings(); // Save settings to EEPROM

    }
  }

  if (touchHandler.isTouched(2)) {
    unsigned long currentTime = millis();
    if (currentTime - lastTouchedTime_Right > debounceDelay) {
      lastTouchedTime_Right = currentTime; // Update the last touched time

      if(currentAnimantionSetting == 2) {
        currentAnimantionSetting = 0;
      } else {
        currentAnimantionSetting++;
      }

      switch (currentAnimantionSetting)
      {
      case 0: // Flame Animation
        animations.StartAnimation(1, 200, flameUpdate);
        break;
      case 1: // Solid Color
        animations.StartAnimation(1, 200, solidUpdate);
        break;
      case 2: // Night Flame Animation
        animations.StartAnimation(1, 200, nightFlameUpdate);
        break;
      }

      saveSettings(); // Save settings to EEPROM

    }
  }

  if(currentBrightnessStep == 4) {
    // automatic Brightness adjustment
    
    float lux = getlux(lightsensor.get());
    float newEnvBrightnessValue;

    if (lux >= 150) {
      newEnvBrightnessValue = 1.0;
    } else if (lux <= 25) {
      newEnvBrightnessValue = 0.2;
    } else {
      newEnvBrightnessValue = 0.2 + (log10(lux / 25.0) / log10(150.0 / 25.0)) * (1.0 - 0.2);
    }

    // Apply deadband filter
    if (abs(newEnvBrightnessValue - previousEnvBrightnessValue) > BRIGHTNESS_DEADBAND) {
      previousBrightnessValue = brightnessValue;
      currentBrightnessValue = newEnvBrightnessValue;
      previousEnvBrightnessValue = newEnvBrightnessValue;  // Update the previous value

      animations.StartAnimation(3, 100, brightnessFade);
    }
  }

  if(!animations.IsAnimationActive(1)) {

    switch (currentAnimantionSetting)
    {
    case 0: // Flame Animation
      animations.StartAnimation(1, 200, flameUpdate);
      break;
    case 1: // Solid Color
      animations.StartAnimation(1, 200, solidUpdate);
      break;
    case 2: // Night Flame Animation
      animations.StartAnimation(1, 200, nightFlameUpdate);
      break;
    }
  }

  animations.UpdateAnimations();
  showBuffer(outputBuffer, LED_NUM);
  delay(10);
  
}

void restoreSettings() {
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(ADDR_CURRENT_BRIGHTNESS_STEP, currentBrightnessStep);
  EEPROM.get(ADDR_PREVIOUS_BRIGHTNESS_STEP, previousBrightnessStep);
  EEPROM.get(ADDR_CURRENT_BRIGHTNESS_VALUE, brightnessValue);
  EEPROM.get(ADDR_CURRENT_BRIGHTNESS_VALUE, currentBrightnessValue);
  EEPROM.get(ADDR_PREVIOUS_BRIGHTNESS_VALUE, previousBrightnessValue);
  EEPROM.get(ADDR_CURRENT_ANIMATION_SETTING, currentAnimantionSetting);

  animations.StartAnimation(3, 100, softStart);
}

void saveSettings() {
  EEPROM.put(ADDR_CURRENT_BRIGHTNESS_STEP, currentBrightnessStep);
  EEPROM.put(ADDR_PREVIOUS_BRIGHTNESS_STEP, previousBrightnessStep);
  EEPROM.put(ADDR_CURRENT_BRIGHTNESS_VALUE, currentBrightnessValue);
  EEPROM.put(ADDR_PREVIOUS_BRIGHTNESS_VALUE, previousBrightnessValue);
  EEPROM.put(ADDR_CURRENT_ANIMATION_SETTING, currentAnimantionSetting);
  EEPROM.commit(); // Save changes to EEPROM
}

void solidUpdate(AnimationParam param) {
  // Set the color to a solid value (e.g., red)
  RgbColor color = RgbColor(255, 64, 0); // Red color
  for (int i = 0; i < LED_NUM; i++) {
    outputBuffer[i] = color;
  }
}

void flameUpdate(AnimationParam param) {
  static RgbColor previousBuffer[LED_NUM]; // Store the previous frame's colors

  for (int i = 0; i < LED_NUM; i++) {
    // Random intensity for red and orange tones
    uint8_t redIntensity = random(120, 255);   // Red component (intense)
    uint8_t greenIntensity = random(0, 20);  // Green component (to create orange tones)
    uint8_t flicker = random(0, 140);          // Small flicker variation

    // Ensure values stay within valid range
    int adjustedRed = constrain(redIntensity + flicker, 0, 255);
    int adjustedGreen = constrain(greenIntensity + flicker, 0, 255);

    // Create the new color
    RgbColor newColor = RgbColor(adjustedRed, adjustedGreen, 0);

    // Blend the new color with the previous color for smooth transitions
    outputBuffer[i] = RgbColor::LinearBlend(previousBuffer[i], newColor, 0.23f); // Adjust blending factor (0.3) for smoothness

    // Store the current color for the next frame
    previousBuffer[i] = outputBuffer[i];
  }
}

void nightFlameUpdate(AnimationParam param) {

  static RgbColor previousBuffer[LED_NUM]; // Store the previous frame's colors

  for (int i = 0; i < LED_NUM; i++) {
    // Random intensity for red and orange tones
    uint8_t redIntensity = random(120, 255);   // Red component (intense)
    uint8_t greenIntensity = random(0, 20);  // Green component (to create orange tones)
    uint8_t flicker = random(0, 140);          // Small flicker variation

    // Ensure values stay within valid range
    int adjustedRed = constrain(redIntensity + flicker, 0, 255);
    int adjustedGreen = constrain(greenIntensity + flicker, 0, 255);

    // Create the new color
    RgbColor newColor = RgbColor(adjustedRed, adjustedGreen, 0);

    // Blend the new color with the previous color for smooth transitions
    outputBuffer[i] = RgbColor::LinearBlend(previousBuffer[i], newColor, 0.23f); // Adjust blending factor (0.3) for smoothness

    // Store the current color for the next frame
    previousBuffer[i] = outputBuffer[i];
  }

  outputBuffer[0] = RgbColor(255, 0, 50);
  outputBuffer[8] = RgbColor(255, 0, 50);
  outputBuffer[16] = RgbColor(255, 0, 50);

}

void breathUpdate(AnimationParam param) {

  // Adjust progress to create a ramp-up and ramp-down effect
  float progress = param.progress < 0.5 
                   ? NeoEase::QuadraticIn(param.progress * 2) // Ramp up
                   : NeoEase::QuadraticOut((1.0 - param.progress) * 2); // Ramp down

  // Set the color with the calculated brightness (e.g., red)
  RgbColor color = RgbColor::LinearBlend(RgbColor(255,0,0),RgbColor(255,128,0),progress);
  // in this case, just apply the color to first pixel
  for (int i = 0; i < LED_NUM; i++) {
    outputBuffer[i] = color;
  }

}

void brightnessFade(AnimationParam param) {

  float progress = NeoEase::CubicInOut(param.progress);

  brightnessValue = brightnessSteps[previousBrightnessStep] + (brightnessSteps[currentBrightnessStep] - brightnessSteps[previousBrightnessStep]) * progress;
  brightnessValue = previousBrightnessValue + (currentBrightnessValue - previousBrightnessValue) * progress;

}

void softStart(AnimationParam param) {

  powerCoeficient = NeoEase::CubicInOut(param.progress);

}

void softStop(AnimationParam param) {

  if(currentAnimantionSetting == 2) {
    for(int i = 0; i < LED_NUM; i++) {
      outputBuffer[i] = RgbColor::LinearBlend(outputBuffer[i], RgbColor(0,255,255), 0.4f); 
    }
  }

  powerCoeficient = NeoEase::CubicInOut(1.0 - param.progress);

}

void showBuffer(RgbColor buffer[], int bufferSize) {

  // Show the gamma-corrected buffer on the LED strip

  for (int i = 0; i < bufferSize; i++) {
    RgbColor bufferColor = RgbColor(
      buffer[i].R*brightnessValue*powerCoeficient, 
      buffer[i].G*brightnessValue*powerCoeficient, 
      buffer[i].B*brightnessValue*powerCoeficient
    );
    strip.SetPixelColor(i, colorGamma.Correct(bufferColor));
  }
  strip.Show();

}

float getlux(float analogValue) {

  // Copied from https://forum.arduino.cc/t/converting-temt6000-value-to-lux/180676/10
  float lux = analogValue * 0.9765625;  // 1000/1024

  return lux;
}