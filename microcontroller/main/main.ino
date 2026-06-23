// ====================================================================
// PROJECT: QUANTUM EQUILIBRIUM
// DESCRIPTION: 1-Second Wire Difference Check & 1-Second MQTT JSON Publisher
//              With 5-Second Auto-Extendable LED Indicator
//              Conditions Updated: Added "selisih" to Serial & MQTT Payload
// ====================================================================

#include <WiFi.h>
#include <PubSubClient.h>

// Pin Configuration
const int pinLED    = 13; // LED connected to D13
const int pinKawatA = 34; // Wire A connected to D34
const int pinKawatB = 35; // Wire B connected to D35

// WiFi Configurations
const char* ssid     = "PutriTunggal";
const char* password = "uu311009";

// MQTT Configurations
const char* mqttServer = "192.168.100.35";
const int mqttPort     = 1883;
const char* mqttTopic  = "monitoring/wire";

WiFiClient espClient;
PubSubClient client(espClient);

// Timing variables (using non-blocking millis)
unsigned long lastCheckTime = 0;   // For checking difference every 1 second
unsigned long lastPublishTime = 0; // For publishing to MQTT every 1 second
unsigned long ledStartTime = 0;    // Timestamp when the LED was activated/re-triggered

const int thresholdVal = 200;      // Threshold value set to 200 (more sensitive to fire)
bool isLedActive = false;

// Global values to hold latest measurements for MQTT publication
int latestNilaiA = 0;
int latestNilaiB = 0;
int latestSelisih = 0; // Variabel baru untuk menyimpan nilai selisih terakhir

void setupWifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Connection Failed. Proceeding (will retry in loop)...");
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); 
  
  pinMode(pinLED, OUTPUT);
  digitalWrite(pinLED, LOW);
  analogReadResolution(12); // ADC Resolution 0 - 4095

  Serial.println("=========================================");
  Serial.println("     PROJECT: QUANTUM EQUILIBRIUM        ");
  Serial.println("=========================================");

  setupWifi();
  client.setServer(mqttServer, mqttPort);

  Serial.println("Monitoring Started (1s check, 1s publish)...\n");
}

void loop() {
  // 1. Maintain WiFi & MQTT connections
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
  }

  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  unsigned long currentMillis = millis();

  // 2. Perform difference checks EVERY 1 SECOND
  if (currentMillis - lastCheckTime >= 1000) {
    lastCheckTime = currentMillis;

    // Read analog values from Wires
    latestNilaiA = analogRead(pinKawatA);
    latestNilaiB = analogRead(pinKawatB);

    // Calculate absolute difference dan simpan ke variabel global
    latestSelisih = abs(latestNilaiA - latestNilaiB);
    bool overThreshold = (latestSelisih > thresholdVal);

    // Printout nilai Kawat A, Kawat B, dan NILAI SELISIH ke Serial Monitor
    Serial.print("[CHECK] Wire A: "); Serial.print(latestNilaiA);
    Serial.print(" | Wire B: "); Serial.print(latestNilaiB);
    Serial.print(" | Selisih: "); Serial.print(latestSelisih);
    Serial.print(" | LED State: "); Serial.println(isLedActive ? "ON" : "OFF");

    // Kondisi Abaikan: Jika kedua kawat bernilai 4095 atau bernilai 0
    if (latestNilaiA == 4095 && latestNilaiB == 4095) {
      Serial.println("--> Both wires read 4095. Ignoring LED alert trigger.");
    } else if (latestNilaiA == 0 && latestNilaiB == 0) {
      Serial.println("--> Both wires read 0. Ignoring LED alert trigger.");
    } 
    // Lampu akan menyala/tetap menyala jika selisih > 200
    else if (overThreshold) {
      digitalWrite(pinLED, HIGH);
      isLedActive = true;
      ledStartTime = currentMillis; 
      Serial.print("--> Selisih melebihi 200 (");
      Serial.print(latestSelisih);
      Serial.println(")! LED turned ON (Timer set for 5s).");
    }
  }

  // 3. LED Timeout check (Turn OFF after 5 seconds of inactivity)
  if (isLedActive && (currentMillis - ledStartTime >= 5000)) {
    digitalWrite(pinLED, LOW);
    isLedActive = false;
    Serial.println("--> 5-second LED duration expired. LED turned OFF.");
  }

  // 4. Publish to MQTT EVERY 1 SECOND
  if (currentMillis - lastPublishTime >= 1000) {
    lastPublishTime = currentMillis;

    // Kondisi Abaikan MQTT
    if (latestNilaiA == 4095 && latestNilaiB == 4095) {
      Serial.println("[MQTT SKIP] Wire A and Wire B are both 4095. Skipping publication.");
    } 
    else if (latestNilaiA == 0 && latestNilaiB == 0) {
      Serial.println("[MQTT SKIP] Wire A and Wire B are both 0. Skipping publication.");
    } 
    else if (latestNilaiA > 0 && latestNilaiB > 0) {
      // Build JSON payload dengan menambahkan data "selisih"
      String payload = "{";
      payload += "\"wire_a\":" + String(latestNilaiA) + ",";
      payload += "\"wire_b\":" + String(latestNilaiB) + ",";
      payload += "\"selisih\":" + String(latestSelisih) + ","; // Memasukkan nilai selisih ke JSON
      payload += "\"threshold\":" + String(thresholdVal) + ",";
      payload += "\"status\":" + String(isLedActive ? "true" : "false");
      payload += "}";

      Serial.print("[MQTT PUBLISH] Payload: ");
      Serial.println(payload);
      client.publish(mqttTopic, payload.c_str());
    } 
    else {
      Serial.print("[MQTT SKIP] One of the wires is 0 (Wire A: ");
      Serial.print(latestNilaiA);
      Serial.print(", Wire B: ");
      Serial.print(latestNilaiB);
      Serial.println("). Skipping publication.");
    }
  }
}