// Minimal hardware check: no WiFi, no web server. Rainbow on the onboard LED
// (GPIO 48) and on the strip (GPIO 4) at once. Built by the `blink` env.
#include <Arduino.h>
#include <FastLED.h>

#define STRIP_PIN   4
#define ONBOARD_PIN 48
#define NUM_LEDS    150

CRGB strip[NUM_LEDS];
CRGB onboard[1];

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("blink_test: rainbow on GPIO 4 (strip) + GPIO 48 (onboard)");
  FastLED.addLeds<WS2812B, STRIP_PIN, GRB>(strip, NUM_LEDS);
  FastLED.addLeds<WS2812B, ONBOARD_PIN, GRB>(onboard, 1);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1000);
  FastLED.setBrightness(60);
}

void loop() {
  static uint8_t hue = 0;
  fill_rainbow(strip, NUM_LEDS, hue, 3);
  onboard[0] = CHSV(hue, 255, 255);
  FastLED.show();
  hue++;
  EVERY_N_SECONDS(5) Serial.printf("alive, hue=%u\n", hue);
  delay(20);
}
