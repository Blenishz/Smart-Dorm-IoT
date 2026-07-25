#include <Arduino.h>
#include <math.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define THERMISTOR_PIN 32
#define MIC_PIN 34

// -------- 7 SEGMENT --------
#define D1 17
#define D2 16
#define D3 4
#define D4 0
#define A 23
#define B 22
#define C 21
#define D 33
#define E 25
#define F 26
#define G 27

String mode = "celsius";

float currentTemp = 0.0;
float currentNoise = 0.0;
unsigned long lastSensorRead = 0;

// -------- Low-pass filter (EMA) --------
float filteredTemp  = 0.0;
float filteredNoise = 0.0;
const float alphaTemp  = 0.1; // 0.05–0.3 (น้อย = smooth มาก)
const float alphaNoise = 0.2; // เสียงเปลี่ยนเร็ว ให้ alpha สูงกว่าอุณหภูมิ
bool firstReading = true;

// segment table
byte digit[10][7] = {
  {1,1,1,1,1,1,0},
  {0,1,1,0,0,0,0},
  {1,1,0,1,1,0,1},
  {1,1,1,1,0,0,1},
  {0,1,1,0,0,1,1},
  {1,0,1,1,0,1,1},
  {1,0,1,1,1,1,1},
  {1,1,1,0,0,0,0},
  {1,1,1,1,1,1,1},
  {1,1,1,1,0,1,1}
};

void showNumber(int num){
  digitalWrite(A,digit[num][0]);
  digitalWrite(B,digit[num][1]);
  digitalWrite(C,digit[num][2]);
  digitalWrite(D,digit[num][3]);
  digitalWrite(E,digit[num][4]);
  digitalWrite(F,digit[num][5]);
  digitalWrite(G,digit[num][6]);
}

void displayValue(int value){
  if(value < 0) value = 0;
  if(value > 99) value = 99;

  int d1 = value / 10;
  int d2 = value % 10;

  digitalWrite(D1, HIGH); digitalWrite(D2, HIGH); 
  digitalWrite(D3, HIGH); digitalWrite(D4, HIGH);

  showNumber(d1);
  digitalWrite(D1, LOW);
  delay(3); 

  digitalWrite(D1, HIGH); digitalWrite(D2, HIGH); 
  digitalWrite(D3, HIGH); digitalWrite(D4, HIGH);

  showNumber(d2);
  digitalWrite(D2, LOW);
  delay(3);
}

// -------- Thermistor constants --------
const float beta = 3950.0;
const float R0 = 10000.0;
const float T0_K = 298.15;
const float VCC = 3.3;
const float R_FIXED = 10000.0;

// Mic const
const int sampleWindow = 50; 
const float OFFSET_DB = 60.0; 
const float SENSITIVITY_FACTOR = 55.0;

// WiFi
const char* ssid = "BLENIZ";
const char* password = "araigordai";

// MQTT
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

void callback(char* topic, byte* payload, unsigned int length){
  String msg="";
  for(int i=0;i<length;i++){
    msg += (char)payload[i];
  }
  Serial.print("MQTT message: ");
  Serial.println(msg);

  if(msg=="celsius" || msg=="decibel"){
    mode = msg;
  }
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" WiFi connected");
  } else {
    Serial.println(" WiFi failed");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT...");
    if (client.connect("ESP32_Temp_Device123")) {
      Serial.println("connected");
      client.subscribe("room/mode");
    } else {
      Serial.print("failed ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void celsius(float Cel) {
  displayValue((int)Cel);

  digitalWrite(D1, HIGH); digitalWrite(D2, HIGH); 
  digitalWrite(D3, HIGH); digitalWrite(D4, HIGH);
  
  digitalWrite(A, HIGH); digitalWrite(B, HIGH); digitalWrite(C, LOW);
  digitalWrite(D, LOW); digitalWrite(E, LOW); digitalWrite(F, HIGH); digitalWrite(G, HIGH);
  digitalWrite(D3, LOW);
  delay(3);

  digitalWrite(D1, HIGH); digitalWrite(D2, HIGH); 
  digitalWrite(D3, HIGH); digitalWrite(D4, HIGH);

  digitalWrite(A, HIGH); digitalWrite(B, LOW); digitalWrite(C, LOW);
  digitalWrite(D, HIGH); digitalWrite(E, HIGH); digitalWrite(F, HIGH); digitalWrite(G, LOW);
  digitalWrite(D4, LOW);
  delay(3);
}

void decibel(float dB) {
  displayValue((int)dB);

  digitalWrite(D1, HIGH); digitalWrite(D2, HIGH); 
  digitalWrite(D3, HIGH); digitalWrite(D4, HIGH);

  digitalWrite(D3, LOW);
  digitalWrite(D4, HIGH);
  digitalWrite(A, LOW); digitalWrite(B, HIGH); digitalWrite(C, HIGH);
  digitalWrite(D, HIGH); digitalWrite(E, HIGH); digitalWrite(F, LOW); digitalWrite(G, HIGH);
  delay(3);

  digitalWrite(D1, HIGH); digitalWrite(D2, HIGH); 
  digitalWrite(D3, HIGH); digitalWrite(D4, HIGH);

  digitalWrite(D3, HIGH);
  digitalWrite(D4, LOW);
  digitalWrite(A, HIGH); digitalWrite(B, HIGH); digitalWrite(C, HIGH);
  digitalWrite(D, HIGH); digitalWrite(E, HIGH); digitalWrite(F, HIGH); digitalWrite(G, HIGH);
  delay(3);
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(MIC_PIN, INPUT);

  pinMode(D1,OUTPUT); pinMode(D2,OUTPUT); pinMode(D3,OUTPUT); pinMode(D4,OUTPUT);
  pinMode(A,OUTPUT); pinMode(B,OUTPUT); pinMode(C,OUTPUT); pinMode(D,OUTPUT);
  pinMode(E,OUTPUT); pinMode(F,OUTPUT); pinMode(G,OUTPUT);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (millis() - lastSensorRead >= 1000) {
    lastSensorRead = millis();

    // -- Temperature --
    int adcValue = analogRead(THERMISTOR_PIN);
    Serial.print("ADC raw: "); Serial.println(adcValue);
    float Vr = (adcValue / 4095.0) * VCC;
    Serial.print("Vr: "); Serial.println(Vr);
    float Rt = R_FIXED * Vr / (VCC - Vr);
    Serial.print("Rt: "); Serial.println(Rt);
    float tempK = 1.0 / ((log(Rt / R0) / beta) + (1.0 / T0_K));
    currentTemp = tempK - 273.15;

    // -- Noise --
    unsigned long startMillis = millis();
    unsigned int signalMax = 0;
    unsigned int signalMin = 4095;
    unsigned int sample;

    while (millis() - startMillis < sampleWindow) {
      sample = analogRead(MIC_PIN);
      if (sample < 4095) {
        if (sample > signalMax) signalMax = sample;
        else if (sample < signalMin) signalMin = sample;
      }
    }
    unsigned int peakToPeak = signalMax - signalMin;
    double volts = (peakToPeak * 3.3) / 4095.0;

    if (volts > 0.01) {
      currentNoise = SENSITIVITY_FACTOR * log10(volts) + OFFSET_DB;
    } else {
      currentNoise = 25.0;
    }

    // -------- Low-pass filter (EMA) --------
    if (firstReading) {
      // seed ค่าแรกทันที ไม่งั้น filter จะค่อยๆ ไต่จาก 0
      filteredTemp  = currentTemp;
      filteredNoise = currentNoise;
      firstReading  = false;
    } else {
      filteredTemp  = alphaTemp  * currentTemp  + (1 - alphaTemp)  * filteredTemp;
      filteredNoise = alphaNoise * currentNoise + (1 - alphaNoise) * filteredNoise;
    }

    Serial.print("Raw Temp: ");      Serial.print(currentTemp, 2);
    Serial.print(" → Filtered: ");   Serial.println(filteredTemp, 2);
    Serial.print("Raw Noise: ");     Serial.print(currentNoise, 1);
    Serial.print(" dB → Filtered: ");Serial.print(filteredNoise, 1);
    Serial.println(" dB");

    // -- ส่ง MQTT (ส่งค่า filtered) --
    char msgTemp[10];
    char msgNoise[10];
    dtostrf(filteredTemp, 4, 2, msgTemp);
    client.publish("room/temperature", msgTemp);
    dtostrf(filteredNoise, 4, 2, msgNoise);
    client.publish("room/noise", msgNoise);
  }

  // -------- แสดงผล (ใช้ค่า filtered) --------
  if(mode=="celsius"){
    celsius(filteredTemp);
  }
  else if(mode=="decibel"){
    decibel(filteredNoise);
  }
}