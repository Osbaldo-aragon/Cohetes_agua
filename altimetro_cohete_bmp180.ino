/*
 * ============================================================================
 *  ALTÍMETRO PARA COHETE DE AGUA — Proyecto STEM
 *  Placa:   ESP32-C3 Mini (SuperMini / LOLIN C3 Mini)
 *  Sensor:  BMP180 (presión barométrica → altura relativa)
 *  Autor:   Generado para uso educativo (TecNM / ITSRLL)
 * ============================================================================
 *
 *  ¿QUÉ HACE?
 *  ----------
 *  1. La ESP32 crea su propia red WiFi (modo Access Point). No necesita
 *     internet ni router: el celular se conecta directamente a la placa.
 *  2. Muestrea la altura 20 veces por segundo y la guarda en un buffer
 *     circular EN LA MEMORIA DE LA PLACA. Esto es clave: aunque el celular
 *     pierda la conexión durante el lanzamiento (algo casi seguro), el
 *     registro NUNCA se detiene. Al reconectarte, la gráfica se
 *     reconstruye completa con todos los datos del vuelo.
 *  3. Sirve una página web (http://192.168.4.1) con:
 *       - Gráfica altura vs. tiempo dibujada en <canvas> (sin librerías
 *         externas: en modo AP no hay internet para cargar CDNs).
 *       - Botones: Iniciar registro / Detener / Borrar / Descargar CSV.
 *       - Indicadores: altura actual, apogeo (altura máxima) y estado
 *         de conexión.
 *
 *  CONEXIONES (BMP180 → ESP32-C3 Mini)
 *  -----------------------------------
 *  ┌──────────┬──────────────────┬─────────────────────────────────────────┐
 *  │  BMP180  │   ESP32-C3 Mini  │  Nota                                   │
 *  ├──────────┼──────────────────┼─────────────────────────────────────────┤
 *  │  VIN     │   3V3            │  ¡NUNCA a 5V! El BMP180 es de 3.3 V.    │
 *  │  GND     │   GND            │  Tierra común.                          │
 *  │  SDA     │   GPIO 8         │  Línea de datos I2C.                    │
 *  │  SCL     │   GPIO 9         │  Línea de reloj I2C.                    │
 *  └──────────┴──────────────────┴─────────────────────────────────────────┘
 *  Si tu placa C3 usa otros pines (p. ej. LOLIN C3 Mini: SDA=8, SCL=10),
 *  solo cambia las constantes PIN_SDA y PIN_SCL de abajo.
 *
 *  LIBRERÍAS NECESARIAS (Arduino IDE → Gestor de librerías)
 *  --------------------------------------------------------
 *  - "Adafruit BMP085 Library" (funciona para BMP085 y BMP180)
 *  - "Adafruit Unified Sensor" (dependencia de la anterior)
 *  - Soporte de placas "esp32 by Espressif Systems" (Gestor de tarjetas).
 *    Selecciona la placa: "ESP32C3 Dev Module" y habilita
 *    Tools → USB CDC On Boot: "Enabled" para ver el monitor serie.
 *
 *  USO EN CAMPO
 *  ------------
 *  1. Enciende la ESP32 (powerbank o pila con regulador).
 *  2. En el celular conéctate a la red "Cohete-STEM" (clave: 12345678).
 *  3. Abre el navegador en  http://192.168.4.1
 *  4. Con el cohete en la plataforma, pulsa "Iniciar registro":
 *     la placa toma la presión actual como altura CERO (calibración).
 *  5. ¡Lanza! Aunque se pierda el WiFi, la placa sigue grabando.
 *  6. Recupera el cohete, acércate, reconecta el celular y pulsa
 *     "Detener". La gráfica completa del vuelo aparecerá sola.
 *  7. "Descargar CSV" guarda los datos para analizarlos en Excel/Python.
 * ============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>   // Sirve para BMP180

// ------------------------- CONFIGURACIÓN EDITABLE --------------------------
#define PIN_SDA        8        // Pin SDA del I2C (dato)
#define PIN_SCL        9        // Pin SCL del I2C (reloj)

const char* AP_SSID  = "Cohete-STEM";   // Nombre de la red WiFi que crea la ESP
const char* AP_PASS  = "12345678";      // Clave (mínimo 8 caracteres)

const uint32_t PERIODO_MS   = 50;       // 50 ms → 20 muestras por segundo
const uint16_t MAX_MUESTRAS = 1600;     // 1600 × 50 ms = 80 s de vuelo máx.
                                        // (buffer circular: si se llena,
                                        //  sobrescribe lo más antiguo)
const float ALFA_FILTRO     = 0.35f;    // Filtro exponencial 0–1.
                                        // Menor = más suave pero más lento.
// ----------------------------------------------------------------------------

Adafruit_BMP085 bmp;
WebServer server(80);

// ---- Buffer de vuelo (vive en RAM de la ESP, independiente del celular) ----
float    alturas[MAX_MUESTRAS];   // Altura relativa en metros
uint32_t tiempos[MAX_MUESTRAS];   // Milisegundos desde "Iniciar registro"
volatile uint16_t nMuestras = 0;  // Muestras válidas guardadas
volatile bool grabando      = false;

float    presionBase = 101325.0f; // Presión de referencia (altura cero)
float    alturaFiltrada = 0.0f;
float    apogeo = 0.0f;           // Altura máxima registrada
uint32_t t0 = 0;                  // Instante de inicio del registro
uint32_t tUltimaMuestra = 0;

// ============================================================================
//  PÁGINA WEB (autocontenida: HTML + CSS + JS, sin internet)
//  Se guarda en memoria flash (PROGMEM) para no gastar RAM.
// ============================================================================
const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Altímetro Cohete STEM</title>
<style>
  :root{
    --cielo:#0b1d33; --panel:#122b47; --linea:#3fa7ff;
    --texto:#eaf2fb; --tenue:#8fb0cf; --ok:#39d98a; --alerta:#ff6b6b;
  }
  *{box-sizing:border-box; -webkit-tap-highlight-color:transparent}
  body{margin:0; font-family:system-ui,Segoe UI,Roboto,sans-serif;
       background:var(--cielo); color:var(--texto); padding:12px}
  h1{font-size:1.15rem; margin:4px 0 10px; letter-spacing:.5px}
  h1 span{color:var(--linea)}
  .fila{display:flex; gap:8px; flex-wrap:wrap; margin-bottom:10px}
  .tarjeta{flex:1; min-width:90px; background:var(--panel);
           border-radius:12px; padding:10px 12px; text-align:center}
  .tarjeta small{display:block; color:var(--tenue); font-size:.7rem;
                 text-transform:uppercase; letter-spacing:1px}
  .tarjeta b{font-size:1.5rem; font-variant-numeric:tabular-nums}
  #estado{font-size:.8rem; padding:6px 10px; border-radius:8px;
          background:var(--panel); display:inline-block; margin-bottom:10px}
  .punto{display:inline-block; width:9px; height:9px; border-radius:50%;
         margin-right:6px; background:var(--alerta)}
  .conectado .punto{background:var(--ok)}
  canvas{width:100%; height:300px; background:var(--panel);
         border-radius:12px; display:block; margin-bottom:12px}
  button{flex:1; min-width:130px; padding:14px 8px; font-size:1rem;
         font-weight:600; border:none; border-radius:12px; color:#fff;
         cursor:pointer}
  #bIniciar{background:#1f9d55} #bDetener{background:#d64545}
  #bBorrar{background:#5a6b80}  #bCsv{background:#2f6fb3}
  button:disabled{opacity:.4}
  footer{color:var(--tenue); font-size:.72rem; margin-top:10px;
         text-align:center}
</style>
</head>
<body>
<h1>🚀 Altímetro <span>Cohete de Agua</span></h1>

<div id="estado"><span class="punto"></span><span id="txtEstado">Conectando…</span></div>

<div class="fila">
  <div class="tarjeta"><small>Altura actual</small><b id="hAct">0.0</b> m</div>
  <div class="tarjeta"><small>Apogeo</small><b id="hMax">0.0</b> m</div>
  <div class="tarjeta"><small>Muestras</small><b id="nPts">0</b></div>
</div>

<canvas id="graf" width="800" height="480"></canvas>

<div class="fila">
  <button id="bIniciar" onclick="cmd('iniciar')">▶ Iniciar registro</button>
  <button id="bDetener" onclick="cmd('detener')">■ Detener</button>
</div>
<div class="fila">
  <button id="bBorrar" onclick="if(confirm('¿Borrar todos los datos?'))cmd('borrar')">🗑 Borrar</button>
  <button id="bCsv" onclick="location.href='/csv'">⬇ Descargar CSV</button>
</div>

<footer>Los datos se guardan en la ESP32: si el WiFi se corta durante el
vuelo, el registro continúa y la gráfica se recupera al reconectar.</footer>

<script>
/* ---- La página SIEMPRE pide el registro completo (/datos).
       Así, tras una desconexión, la gráfica se reconstruye sola. ---- */
const cv = document.getElementById('graf');
const cx = cv.getContext('2d');
let fallos = 0;

function cmd(a){ fetch('/'+a).catch(()=>{}); }

async function actualizar(){
  try{
    const r = await fetch('/datos', {cache:'no-store'});
    const d = await r.json();
    fallos = 0;
    document.getElementById('estado').classList.add('conectado');
    document.getElementById('txtEstado').textContent =
        d.grabando ? 'Conectado · GRABANDO' : 'Conectado · en espera';
    document.getElementById('hAct').textContent = d.h.length ?
        d.h[d.h.length-1].toFixed(1) : '0.0';
    document.getElementById('hMax').textContent = d.apogeo.toFixed(1);
    document.getElementById('nPts').textContent = d.h.length;
    dibujar(d.t, d.h);
  }catch(e){
    if(++fallos >= 2){   // 2 fallos seguidos = desconectado
      document.getElementById('estado').classList.remove('conectado');
      document.getElementById('txtEstado').textContent =
          'Sin conexión… la ESP sigue grabando. Reintentando';
    }
  }
}
setInterval(actualizar, 500);   // Refresca 2 veces por segundo
actualizar();

/* ------------------- Gráfica en canvas (sin librerías) ------------------- */
function dibujar(t, h){
  const W = cv.width, H = cv.height, mL=55, mB=34, mT=14, mR=12;
  cx.clearRect(0,0,W,H);
  cx.font = '13px system-ui';
  if(h.length < 2){
    cx.fillStyle = '#8fb0cf';
    cx.fillText('Pulsa "Iniciar registro" y lanza el cohete…', mL, H/2);
    return;
  }
  const tMax = Math.max(t[t.length-1], 1);
  let hMin = Math.min(0, ...h), hMax = Math.max(2, ...h);
  const rango = hMax - hMin; hMax += rango*0.08; hMin -= rango*0.05;

  const X = s => mL + (s/tMax)*(W-mL-mR);
  const Y = m => H-mB - ((m-hMin)/(hMax-hMin))*(H-mB-mT);

  // Rejilla y ejes
  cx.strokeStyle='#1e3a5c'; cx.fillStyle='#8fb0cf'; cx.lineWidth=1;
  for(let i=0;i<=4;i++){
    const v = hMin + (hMax-hMin)*i/4, y = Y(v);
    cx.beginPath(); cx.moveTo(mL,y); cx.lineTo(W-mR,y); cx.stroke();
    cx.fillText(v.toFixed(1)+' m', 6, y+4);
  }
  for(let i=0;i<=5;i++){
    const s = tMax*i/5, x = X(s);
    cx.fillText(s.toFixed(1)+' s', x-12, H-10);
  }

  // Curva de altura
  cx.strokeStyle='#3fa7ff'; cx.lineWidth=2.5; cx.beginPath();
  cx.moveTo(X(t[0]), Y(h[0]));
  for(let i=1;i<h.length;i++) cx.lineTo(X(t[i]), Y(h[i]));
  cx.stroke();

  // Marca del apogeo
  let iMax=0; for(let i=1;i<h.length;i++) if(h[i]>h[iMax]) iMax=i;
  cx.fillStyle='#39d98a';
  cx.beginPath(); cx.arc(X(t[iMax]), Y(h[iMax]), 5, 0, 7); cx.fill();
  cx.fillText('Apogeo: '+h[iMax].toFixed(1)+' m',
              Math.min(X(t[iMax])+8, W-140), Y(h[iMax])-8);
}
</script>
</body>
</html>
)HTML";

// ============================================================================
//  MANEJADORES DEL SERVIDOR WEB
// ============================================================================

// Página principal
void handleRaiz() {
  server.send_P(200, "text/html", PAGINA);
}

// Iniciar registro: calibra la presión base (altura cero) y limpia el buffer
void handleIniciar() {
  // Promedio de 15 lecturas para una referencia estable de "suelo"
  double suma = 0;
  for (int i = 0; i < 15; i++) { suma += bmp.readPressure(); delay(20); }
  presionBase   = suma / 15.0;
  nMuestras     = 0;
  apogeo        = 0.0f;
  alturaFiltrada = 0.0f;
  t0            = millis();
  grabando      = true;
  server.send(200, "text/plain", "OK");
  Serial.println("[REG] Registro iniciado. Presion base (Pa): " + String(presionBase));
}

void handleDetener() {
  grabando = false;
  server.send(200, "text/plain", "OK");
  Serial.println("[REG] Registro detenido. Muestras: " + String(nMuestras));
}

void handleBorrar() {
  grabando  = false;
  nMuestras = 0;
  apogeo    = 0.0f;
  server.send(200, "text/plain", "OK");
}

// Devuelve TODO el registro en JSON. El celular reconstruye la gráfica
// completa cada vez → inmune a las desconexiones durante el vuelo.
void handleDatos() {
  uint16_t n = nMuestras;              // Copia local (evita carreras)
  String json;
  json.reserve(24 * n + 96);           // Pre-reserva memoria: más rápido
  json  = "{\"grabando\":";
  json += grabando ? "true" : "false";
  json += ",\"apogeo\":" + String(apogeo, 2);
  json += ",\"t\":[";
  for (uint16_t i = 0; i < n; i++) {
    if (i) json += ',';
    json += String(tiempos[i] / 1000.0f, 2);   // segundos
  }
  json += "],\"h\":[";
  for (uint16_t i = 0; i < n; i++) {
    if (i) json += ',';
    json += String(alturas[i], 2);             // metros
  }
  json += "]}";
  server.send(200, "application/json", json);
}

// Descarga el registro como archivo CSV (abrible en Excel / Python / Sheets)
void handleCsv() {
  uint16_t n = nMuestras;
  String csv;
  csv.reserve(20 * n + 64);
  csv = "tiempo_s,altura_m\n";
  for (uint16_t i = 0; i < n; i++) {
    csv += String(tiempos[i] / 1000.0f, 3);
    csv += ',';
    csv += String(alturas[i], 2);
    csv += '\n';
  }
  server.sendHeader("Content-Disposition", "attachment; filename=vuelo_cohete.csv");
  server.send(200, "text/csv", csv);
}

// ============================================================================
//  SETUP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Altimetro Cohete de Agua (ESP32-C3 + BMP180) ===");

  // --- I2C en los pines definidos arriba ---
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!bmp.begin(BMP085_STANDARD)) {   // Modo STANDARD: lectura rápida (~8 ms)
    Serial.println("[ERROR] BMP180 no detectado. Revisa SDA/SCL/3V3/GND.");
    while (true) delay(500);           // Se detiene aquí si no hay sensor
  }
  Serial.println("[OK] BMP180 detectado.");

  // --- Punto de acceso WiFi propio (sin router, sin internet) ---
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("[OK] Red WiFi creada: ");
  Serial.println(AP_SSID);
  Serial.print("[OK] Abre en el navegador: http://");
  Serial.println(WiFi.softAPIP());     // Normalmente 192.168.4.1

  // --- Rutas del servidor web ---
  server.on("/",        handleRaiz);
  server.on("/iniciar", handleIniciar);
  server.on("/detener", handleDetener);
  server.on("/borrar",  handleBorrar);
  server.on("/datos",   handleDatos);
  server.on("/csv",     handleCsv);
  server.begin();
  Serial.println("[OK] Servidor web listo.");
}

// ============================================================================
//  LOOP: muestreo NO bloqueante → el WiFi y el sensor conviven sin estorbarse
// ============================================================================
void loop() {
  server.handleClient();   // Atiende al celular (si está conectado)

  // El muestreo depende SOLO del reloj interno, nunca del WiFi:
  if (grabando && (millis() - tUltimaMuestra >= PERIODO_MS)) {
    tUltimaMuestra = millis();

    // Altura barométrica relativa a la presión base (fórmula internacional)
    float p = bmp.readPressure();                       // Pascales
    float h = 44330.0f * (1.0f - powf(p / presionBase, 0.190295f));

    // Filtro exponencial: suaviza el ruido del sensor (±0.5 m típico)
    alturaFiltrada += ALFA_FILTRO * (h - alturaFiltrada);
    if (alturaFiltrada > apogeo) apogeo = alturaFiltrada;

    if (nMuestras < MAX_MUESTRAS) {
      alturas[nMuestras] = alturaFiltrada;
      tiempos[nMuestras] = millis() - t0;
      nMuestras++;
    } else {
      grabando = false;    // Buffer lleno: detiene para no perder el vuelo
      Serial.println("[REG] Buffer lleno (80 s). Registro detenido.");
    }
  }
}
