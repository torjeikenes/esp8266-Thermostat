/*
 Basic ESP8266 MQTT example
 This sketch demonstrates the capabilities of the pubsub library in combination
 with the ESP8266 board/library.
 It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic" every two seconds
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary
  - If the first character of the topic "inTopic" is an 1, switch ON the ESP Led,
    else switch it off
 It will reconnect to the server if the connection is lost using a blocking
 reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
 achieve the same result without blocking the main loop.
 To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"
*/
#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Adafruit_SSD1306.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
// Update these with values suitable for your network.

// Defined in config.h
const char *ssid = SSID;
const char *password = PASS;
const char *mqtt_server = MQTT_IP;
const char *mqtt_user = MQTT_U;
const char *mqtt_password = MQTT_P;

#define CURRENT_TEMP_TOPIC "/Stue/thermostat/currentTemp"
#define TARGET_TEMP_TOPIC "/Stue/thermostat/targetTemp"
#define TARGET_TEMP_CHANGE_TOPIC "/Stue/thermostat/targetTempChange"
#define OUTSIDE_TEMP_TOPIC "/Stue/thermostat/outsideTemp"
#define OUTSIDE_TEMP_TOPIC "/Stue/thermostat/outsideTemp"

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

#define DHTPIN 14 // D5
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 oled(128, 64);

#define ROT_A_PIN 12 // D6
#define ROT_B_PIN 13 // D7

char TargetTempStr[] = "00.0";
char OutsideTempStr[] = "00.0";
float CurrentTemp = 0.0;

uint8_t TempUpdated = 0;
uint8_t RotChanged = 0;

void setup_wifi()
{

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (!strcmp(topic, TARGET_TEMP_TOPIC)) // If no difference
  {
    strncpy(TargetTempStr, (char *)payload, length);
    TempUpdated = 1;
  }
  else if (!strcmp(topic, OUTSIDE_TEMP_TOPIC))
  {
    strncpy(OutsideTempStr, (char *)payload, length);
    TempUpdated = 1;
  }
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      // client.publish("/esp8266/currentTemp", "22");
      // ... and resubscribe
      client.subscribe(TARGET_TEMP_TOPIC);
      client.subscribe(OUTSIDE_TEMP_TOPIC);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void displayString(char *string, uint8_t size, uint16_t color, uint8_t x, uint8_t y)
{
  oled.setTextSize(size);   // Normal 1:1 pixel scale
  oled.setTextColor(color); // Draw white text
  oled.setCursor(x, y);     // Start at top-left corner
  // oled.cp437(true);         // Use full 256 char 'Code Page 437' font

  for (int i = 0; string[i] != '\0'; i++)
  {
    oled.write(string[i]);
  }
}

void displayTemp(char *string, uint8_t size, uint16_t color, uint8_t x, uint8_t y)
{
  displayString(string, size, color, x, y);
  oled.write(248); // Degree symbol
  oled.write('C');
}

void displayTemp(float temp, uint8_t size, uint16_t color, uint8_t x, uint8_t y)
{
  char tmpString[6];
  snprintf(tmpString, 6, "%.1f", temp);
  displayTemp(tmpString, size, color, x, y);
}

void renderDisplay()
{
  oled.clearDisplay();
  displayTemp(CurrentTemp, 2, SSD1306_WHITE, 0, 0);
  displayTemp(TargetTempStr, 2, SSD1306_WHITE, 0, 25);
  displayTemp(OutsideTempStr, 2, SSD1306_WHITE, 0, 50);
  oled.display();
}

#define BM_ROT_A (1 << 0)
#define BM_ROT_B (1 << 1)
#define BM_CW (1 << 0)
#define BM_CCW (1 << 1)

// Rotary encoder interrupt function.
// IRAM_ATTR keeps function in internal RAM
void IRAM_ATTR rotEncInterrupt()
{
  // Serial.println("ROT");

  uint8_t rotBitmask = digitalRead(ROT_A_PIN);
  rotBitmask |= digitalRead(ROT_B_PIN) << 1;

  // if ((rotBitmask & BM_ROT_A) != (rotBitmask & (BM_ROT_B)))
  if (rotBitmask == 0x03 || rotBitmask == 0x00) // If both lsb are equal
  {
    // CCW
    RotChanged = BM_CCW;
  }
  else
  {
    // CW
    // Serial.println("CW");
    RotChanged = BM_CW;
  }
}

void setup()
{
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  oled.display();
  pinMode(LED_BUILTIN, OUTPUT); // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);

  dht.begin();
  oled.clearDisplay();

  pinMode(ROT_A_PIN, INPUT_PULLUP);
  pinMode(ROT_B_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ROT_A_PIN), rotEncInterrupt, CHANGE);
}

void loop()
{

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 10000)
  {
    lastMsg = now;
    CurrentTemp = dht.readTemperature();
    TempUpdated = 1;

    // Serial.print(temperature);

    // oled.clearDisplay();
    // displayTemp(temperature, 2, SSD1306_WHITE, 0, 0);
    // oled.display();

    //++value;
    // snprintf(msg, MSG_BUFFER_SIZE, "%ld", value);
    snprintf(msg, MSG_BUFFER_SIZE, "%.1f", CurrentTemp);
    Serial.print("Publish message: ");
    Serial.println(msg);
    client.publish(CURRENT_TEMP_TOPIC, msg);
  }

  if (RotChanged)
  {
    Serial.printf("Rot %d\n", RotChanged);
    if (RotChanged & BM_CW)
    {
      client.publish(TARGET_TEMP_CHANGE_TOPIC, "inc");
    }
    else
    {
      client.publish(TARGET_TEMP_CHANGE_TOPIC, "dec");
    }
    RotChanged = 0;
  }

  if (TempUpdated)
  {
    TempUpdated = 0;
    renderDisplay();
  }
}
