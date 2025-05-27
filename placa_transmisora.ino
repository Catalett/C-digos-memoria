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
#define TX_POWER  20     

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
String inputString = "";
boolean stringComplete = false;
float distance = 0.0;
unsigned long packetCounter = 0;
unsigned long totalPacketsSent = 0;
unsigned long startTime = 0;
unsigned long lastPacketTime = 0;
unsigned long lastDisplayUpdateTime = 0;
unsigned long elapsedTime = 0;
bool transmissionActive = false;

void setup() {
  Serial.begin(115200);
  delay(1000); 
  
  Serial.println("LILYGO LoRa Distance Transmitter");
  
  Wire.begin(); 
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("LoRa Distance TX");
    display.println("Initializing...");
    display.display();
  }
  
  Serial.println("Setting up LoRa...");
  
  SPI.begin(SCK, MISO, MOSI, SS);
  
  LoRa.setPins(SS, RST, DI0);
  
  Serial.println("LoRa pins set");
  
  unsigned long startAttemptTime = millis();
  bool loraInitialized = false;
  
  while ((millis() - startAttemptTime) < 5000 && !loraInitialized) {
    if (LoRa.begin(FREQUENCY)) {
      loraInitialized = true;
    } else {
      Serial.println(".");
      delay(500);
    }
  }
  
  if (!loraInitialized) {
    Serial.println("Starting LoRa failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa init failed!");
    display.display();
    
  } else {
    Serial.println("LoRa initialized successfully!");
    
    LoRa.setTxPower(TX_POWER);
    
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.setPreambleLength(12);
    Serial.println("LoRa configured OK!");
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LoRa Ready!");
  display.println("Enter distance in meters");
  display.println("via serial console...");
  display.display();
  
  Serial.println("Enter distance in meters and press Enter:");
}

void loop() {
  
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    
    
    if (inChar == '\n' || inChar == '\r') {
      if (inputString.length() > 0) {
        stringComplete = true;
      }
    } else {
      inputString += inChar;
    }
  }
  
  
  if (stringComplete) {
    distance = inputString.toFloat();
    
    if (distance > 0) {
      Serial.print("Setting distance to: ");
      Serial.print(distance);
      Serial.println(" meters");
      
      packetCounter = 0;
      totalPacketsSent = 0;
      startTime = millis();
      lastPacketTime = 0;
      transmissionActive = true;
      
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Starting transmission");
      display.print("Distance: ");
      display.print(distance);
      display.println(" m");
      display.display();
    } else {
      Serial.println("Invalid distance. Please enter a positive number.");
      
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("ERROR:");
      display.println("Invalid distance");
      display.println("Enter a positive number");
      display.display();
    }
    
    inputString = "";
    stringComplete = false;
  }
  
  if (transmissionActive) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastPacketTime >= 1000) {
      sendPacket();
      lastPacketTime = currentTime;
    }
    
    if (currentTime - lastDisplayUpdateTime >= 250) {
      updateDisplay();
      lastDisplayUpdateTime = currentTime;
    }
  }
  
  delay(10);
}

void sendPacket() {
  unsigned long currentTime = millis();
  elapsedTime = currentTime - startTime;
  
  LoRa.beginPacket();
  
  String packet = "{";
  packet += "\"seq\":" + String(packetCounter) + ",";
  packet += "\"dist\":" + String(distance) + ",";
  packet += "\"total\":" + String(totalPacketsSent) + ",";
  packet += "\"timestamp\":" + String(currentTime);
  packet += "}";
  
  LoRa.print(packet);
  
  LoRa.endPacket();
  
  packetCounter++;
  totalPacketsSent++;
  
  Serial.print("Packet sent [");
  Serial.print(packetCounter);
  Serial.print("]: ");
  Serial.println(packet);
  
  if (Serial.available() > 0) {
    Serial.println("\nNew input detected. Stopping transmission.");
    Serial.println("Enter distance in meters and press Enter:");
    
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Transmission stopped");
    display.println("Enter new distance...");
    display.display();
    
    transmissionActive = false;
  }
}

void updateDisplay() {
  display.clearDisplay();
  
  display.setTextSize(1); 
  display.setCursor(0, 0);
  display.print("Msg: ");
  display.println(packetCounter);
  
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Dist: ");
  display.print(distance, 1);
  display.println("m");
  
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.print("Time: ");
  
  unsigned long seconds = elapsedTime / 1000;
  unsigned long minutes = seconds / 60;
  
  if (minutes < 10) display.print("0");
  display.print(minutes % 60);
  display.print(":");
  
  if ((seconds % 60) < 10) display.print("0");
  display.print(seconds % 60);
  
  display.display();
}