#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ------------------ Configuración I2C (para ESP32-C3) ------------------
// Pines I2C más comunes en ESP32-C3 Mini
#define I2C_SDA 8
#define I2C_SCL 9

// ------------------ Configuración pantalla ------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1   // Pantallas sin pin reset
#define OLED_ADDR     0x3C // Dirección I2C del display (verifica con el scanner)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
// ------------------------------------------------------------

Adafruit_BMP085 bmp;

// --- Variables Globales ---
float maxAlt = 0.0;           // Almacenará la altitud máxima relativa
float startingAltitude = 0.0; // Almacenará la altitud base para la calibración

void setup() {
  Serial.begin(115200); // Inicia Serial para depuración

  // --- MODIFICACIÓN: Inicializar I2C con pines específicos ---
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Inicializa BMP085
  if (!bmp.begin()) {
    Serial.println("No se pudo encontrar el sensor BMP085");
    while (1);
  }

  // Inicializa pantalla
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("Fallo al iniciar SSD1306");
    while (1);
  }

  // Mensaje de inicio
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("BMP085 + OLED OK");
  display.println("Osbaldo Aragon");
  display.display();
  delay(1000); // Reducido el delay

  // --- MODIFICACIÓN: Rutina de Calibración de Altitud ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("Calibrando altitud...");
  display.println("No mover...");
  display.display();

  float totalAltitude = 0;
  for (int i = 0; i < 3; i++) {
    totalAltitude += bmp.readAltitude();
    delay(150); // Pausa entre lecturas para estabilizar
  }
  
  // Calcula el promedio y lo guarda como la altitud de referencia "cero"
  startingAltitude = totalAltitude / 3.0; 

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Calibracion Lista!");
  display.setCursor(0, 25);
  display.print("Alt. Base:");
  display.setCursor(0, 35);
  display.print(startingAltitude, 1);
  display.println(" m");
  display.display();
  delay(1500);
}

void loop() {
  float temp = bmp.readTemperature();
  
  // --- MODIFICACIÓN: Normalización de Altitud ---
  // 1. Lee la altitud absoluta actual (respecto al nivel del mar)
  float currentAbsoluteAlt = bmp.readAltitude();
  
  // 2. Calcula la altitud relativa restando la altitud de inicio
  float alt = currentAbsoluteAlt - startingAltitude;

  // 3. Compara la altitud relativa para encontrar el máximo
  if (alt > maxAlt) {
    maxAlt = alt;
  }

  // --- Muestra en OLED ---
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print("Comp Vuelo");

  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Temp: ");
  display.print(temp, 1);
  display.println(" C");

  display.setCursor(0, 36);
  display.print("Alt: ");
  display.print(alt, 1); // Muestra la altitud relativa (normalizada)
  display.println(" m");
  
  display.setCursor(0, 52);
  display.print("MaxAlt: ");
  display.print(maxAlt, 1); // Muestra la altitud máxima relativa
  display.println(" m");

  display.display();

  delay(100);
}
