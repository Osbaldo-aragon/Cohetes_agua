#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h> // Compatible con BMP180

// --------- Pantalla ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define OLED_ADDR    0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --------- Sensor ----------
Adafruit_BMP085 bmp;

// --------- Muestreo & buffer ----------
const unsigned long SAMPLE_MS = 100;   // 100 ms (10 Hz)
float altBuf[SCREEN_WIDTH];            // 128 muestras visibles
uint16_t countSamples = 0;
uint16_t head = 0;                     // índice circular
unsigned long lastSample = 0;

// --------- Altitud ----------
const float SEA_LEVEL_HPA = 1013.25f;  // Ajusta según tu clima local
float ALT0 = NAN;                      // Altitud de referencia al inicio
float ALT_MIN = 0, ALT_MAX = 0;        // Se fijan tras primera lectura

// Mapea valor [ALT_MIN, ALT_MAX] a Y pantalla [0..63] (0 = arriba)
int16_t mapToY(float v) {
  if (v < ALT_MIN) v = ALT_MIN;
  if (v > ALT_MAX) v = ALT_MAX;
  float norm = (v - ALT_MIN) / (ALT_MAX - ALT_MIN); // 0..1
  float y = (1.0f - norm) * (SCREEN_HEIGHT - 1);
  if (y < 0) y = 0;
  if (y > SCREEN_HEIGHT - 1) y = SCREEN_HEIGHT - 1;
  return (int16_t)y;
}

void pushSample(float alt) {
  // Recorte al rango fijo
  if (alt < ALT_MIN) alt = ALT_MIN;
  if (alt > ALT_MAX) alt = ALT_MAX;

  altBuf[head] = alt;
  head = (head + 1) % SCREEN_WIDTH;
  if (countSamples < SCREEN_WIDTH) countSamples++;
}

void drawGrid() {
  // Líneas guía: inferior (ALT_MIN), media (ALT0), superior (ALT_MAX)
  int16_t yBot = mapToY(ALT_MIN);
  int16_t yMid = mapToY(ALT0);
  int16_t yTop = mapToY(ALT_MAX);

  for (int x = 0; x < SCREEN_WIDTH; x += 2) {
    display.drawPixel(x, yTop, SSD1306_WHITE);
    display.drawPixel(x, yMid, SSD1306_WHITE);
    display.drawPixel(x, yBot, SSD1306_WHITE);
  }

  // Etiquetas
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print((int)ALT_MAX); display.print("m");
  display.setCursor(0, yMid - 6);
  display.print((int)ALT0); display.print("m");
  display.setCursor(0, SCREEN_HEIGHT - 8);
  display.print((int)ALT_MIN); display.print("m");
}

void drawGraph() {
  display.clearDisplay();
  drawGrid();

  // Curva de las últimas 'countSamples'
  int16_t prevY = -1;
  for (uint16_t x = 0; x < countSamples; x++) {
    uint16_t idx = (head + SCREEN_WIDTH - countSamples + x) % SCREEN_WIDTH;
    int16_t y = mapToY(altBuf[idx]);

    if (x > 0 && prevY >= 0) {
      display.drawLine(x - 1, prevY, x, y, SSD1306_WHITE);
    } else {
      display.drawPixel(x, y, SSD1306_WHITE);
    }
    prevY = y;
  }

  // Marca el punto más reciente
  if (countSamples > 0) {
    int16_t x = countSamples - 1;
    uint16_t lastIdx = (head + SCREEN_WIDTH - 1) % SCREEN_WIDTH;
    int16_t y = mapToY(altBuf[lastIdx]);
    display.drawPixel(x, y, SSD1306_WHITE);
    // “cursor” de 3 px (opcional)
    if (y > 0) display.drawPixel(x, y - 1, SSD1306_WHITE);
    if (y < SCREEN_HEIGHT - 1) display.drawPixel(x, y + 1, SSD1306_WHITE);
  }

  // Texto: altitud actual
  float aActual = (countSamples > 0) ? altBuf[(head + SCREEN_WIDTH - 1) % SCREEN_WIDTH] : NAN;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(64, 0);
  display.printf("Alt: %.1fm", aActual);

  display.display();
}

void setup() {
  // Ajusta pines I2C si tu ESP32-C3 lo requiere:
  // Wire.begin(8, 9);  // SDA=8, SCL=9 (ejemplo común en C3 mini)

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    for(;;);
  }
  display.clearDisplay(); display.display();

  if (!bmp.begin()) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("BMP180 FAIL");
    display.display();
    for(;;);
  }

  // Lectura inicial para fijar escala
  float alt0 = bmp.readAltitude(SEA_LEVEL_HPA * 100.0f); // readAltitude espera Pascales
  ALT0 = alt0;
  ALT_MIN = ALT0 - 20.0f;  // ventana fija -20 m
  ALT_MAX = ALT0 + 80.0f;  // ventana fija +80 m
  if (ALT_MIN < -500.0f) ALT_MIN = -500.0f; // seguridad

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Altitud en tiempo real");
  display.print("Ventana: ");
  display.print((int)ALT_MIN); display.print("..");
  display.print((int)ALT_MAX); display.println(" m");
  display.display();
  delay(800);

  lastSample = millis();
}

void loop() {
  unsigned long now = millis();

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    // Nota: Adafruit_BMP085::readAltitude() recibe presion a nivel del mar en Pascales
    float alt = bmp.readAltitude(SEA_LEVEL_HPA * 100.0f); // m
    pushSample(alt);
    drawGraph(); // refresca con el nuevo punto en tiempo real
  }
}
