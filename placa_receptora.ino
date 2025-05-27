#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1  
#define SCREEN_ADDRESS 0x3C

#define SCK     5    
#define MISO    19   
#define MOSI    27   
#define SS      18   
#define RST     14   
#define DI0     26   

#define FREQUENCY 915E6  

#define DEBUG_MODE true

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long lostPackets = 0;
unsigned long expectedSequence = 0;  
float distance = 0.0;
float lastDistance = -1.0; 
float rssi = 0;
float snr = 0;
float packetLossRate = 0.0;
unsigned long totalPacketsSent = 0;  
unsigned long lastDisplayUpdateTime = 0;
unsigned long currentPacketId = 0;  
unsigned long cumulativeReceived = 0;  

unsigned long lastPacketTime = 0;
bool receivedFirstPacket = false;

void setup() {
  Serial.begin(115200);
  delay(1000); 

  if (DEBUG_MODE) {
    Serial.println("LILYGO LoRa Distance Receiver");
  }
  Wire.begin(); 
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    if (DEBUG_MODE) Serial.println("SSD1306 allocation failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("LoRa Distance RX");
    display.println("Initializing...");
    display.display();
  }

  SPI.begin(SCK, MISO, MOSI, SS);

  LoRa.setPins(SS, RST, DI0);

  unsigned long startAttemptTime = millis();
  bool loraInitialized = false;
  while ((millis() - startAttemptTime) < 5000 && !loraInitialized) {
    if (LoRa.begin(FREQUENCY)) {
      loraInitialized = true;
    } else {
      if (DEBUG_MODE) Serial.print(".");
      delay(500);
    }
  }

  if (!loraInitialized) {
    if (DEBUG_MODE) Serial.println("Starting LoRa failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa init failed!");
    display.display();
  } else {
    if (DEBUG_MODE) Serial.println("LoRa initialized successfully!");
    // Configure LoRa parameters
    LoRa.setSpreadingFactor(11);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(8);
    LoRa.setPreambleLength(12);
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LoRa Ready!");
  display.println("Waiting for packets...");
  display.display();

  lastDisplayUpdateTime = millis();
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    processPacket(packetSize);
  }

  unsigned long currentTime = millis();
  if (currentTime - lastDisplayUpdateTime >= 100) {
    updateDisplay();
    lastDisplayUpdateTime = currentTime;
  }

  delay(1);
}

void processPacket(int packetSize) {
  String receivedData = "";
  while (LoRa.available()) {
    receivedData += (char)LoRa.read();
  }
  
  rssi = LoRa.packetRssi();
  snr = LoRa.packetSnr();

  unsigned long transmittedPacketId = extractValue(receivedData, "seq").toInt();
  distance = extractValue(receivedData, "dist").toFloat();
  int newTotal = extractValue(receivedData, "total").toInt();

  if (lastDistance == -1.0 || distance > lastDistance) {
    if (DEBUG_MODE) {
      Serial.print("New session detected: resetting counters. (Old Distance: ");
      Serial.print(lastDistance);
      Serial.print(" -> New Distance: ");
      Serial.print(distance);
      Serial.println(")");
    }
    cumulativeReceived = 0;
    lostPackets = 0;
    totalPacketsSent = 0;
    expectedSequence = transmittedPacketId + 1;
    receivedFirstPacket = true;
    lastDistance = distance;
  }
  
  if (transmittedPacketId == 0 && expectedSequence != 0) {
    if (DEBUG_MODE) {
      Serial.println("Received anomalous packet with sequence 0; logging as new DATA and incrementing expectedSequence.");
    }
    
    currentPacketId = 0;
    cumulativeReceived++;
    totalPacketsSent = newTotal;
    if (newTotal > 0) {
      packetLossRate = 100.0 * lostPackets / newTotal;
    }
    outputLoggableData();
    updateDisplay();
    lastPacketTime = millis();
    
    expectedSequence++;
    return;
  }

  if (transmittedPacketId >= expectedSequence) {
    
    if (transmittedPacketId > expectedSequence) {
      unsigned long gap = transmittedPacketId - expectedSequence;
      for (unsigned long missing = expectedSequence; missing < transmittedPacketId; missing++) {
         Serial.print("LOST: Packet ");
         Serial.println(missing);
      }
      lostPackets += gap;
    }
    expectedSequence = transmittedPacketId + 1;
  } else {
    
    if (DEBUG_MODE) {
      Serial.print("Out-of-order packet: received ");
      Serial.println(transmittedPacketId);
    }
  }

  cumulativeReceived++;
  currentPacketId = transmittedPacketId;
  totalPacketsSent = newTotal;

  if (newTotal > 0) {
    packetLossRate = 100.0 * lostPackets / newTotal;
  }

  outputLoggableData();
  updateDisplay();

  lastPacketTime = millis();
}

String extractValue(String data, String key) {
  String keyStr = "\"" + key + "\":";
  int keyIndex = data.indexOf(keyStr);
  if (keyIndex == -1) return "0";
  int valueStart = keyIndex + keyStr.length();
  int valueEnd = data.indexOf(",", valueStart);
  if (valueEnd == -1) {
    valueEnd = data.indexOf("}", valueStart);
  }
  if (valueEnd == -1) return "0";
  return data.substring(valueStart, valueEnd);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.print("Msg: ");
  display.println(currentPacketId);

  display.setCursor(0, 12);
  display.print("Dist: ");
  display.print(distance, 2);
  display.println(" m");

  display.setCursor(0, 24);
  display.print("Rcved/Lost: ");
  display.print(cumulativeReceived);
  display.print("/");
  display.println(lostPackets);

  display.setCursor(0, 36);
  display.print("RSSI: ");
  display.print(rssi);
  display.println(" dBm");

  display.setCursor(0, 48);
  display.print("Loss: ");
  display.print(packetLossRate, 1);
  display.println("%");

  display.display();
}

void outputLoggableData() {
  Serial.print("DATA,");
  Serial.print(millis());
  Serial.print(",");
  Serial.print(currentPacketId);
  Serial.print(",");
  Serial.print(distance);
  Serial.print(",");
  Serial.print(cumulativeReceived);
  Serial.print(",");
  Serial.print(lostPackets);
  Serial.print(",");
  Serial.print(rssi);
  Serial.print(",");
  Serial.print(snr);
  Serial.print(",");
  Serial.print(totalPacketsSent);
  Serial.print(",");
  Serial.println(packetLossRate, 1);
}


