/*
  Spacetime using ESP32

  Modified by Tiffany Tseng for esp32 Arduino Board Definition 3.0+ 
  Originally created by Mark Santolucito for Barnard COMS 3930
  Based on DroneBot Workshop 2022 ESP-NOW Multi Unit Demo
*/

// Include Libraries
#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>  // Graphics and font library for ST7735 driver chip
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h

String cmd1 = "";
String cmd2 = "";
String cmd3 = "";
volatile bool scheduleCmd1Send = false;
volatile bool scheduleCmd2Send = false;
volatile bool scheduleCmd3Send = false;

String cmdRecvd = "";
const String waitingCmd = "Wait for cmds";
bool redrawCmdRecvd = false;

// for drawing progress bars
int progress = 0;
bool redrawProgress = true;
int lastRedrawTime = 0;

// border variables
bool flashGreenBorder = false;
bool flashRedBorder = false;
unsigned long lastFlashTime = 0;
bool borderState = false;
const int flashInterval = 250;

//we could also use xSemaphoreGiveFromISR and its associated fxns, but this is fine
volatile bool scheduleCmdAsk = true;
hw_timer_t *askRequestTimer = NULL;
volatile bool askExpired = false;
hw_timer_t *askExpireTimer = NULL;
int expireLength = 35;

#define ARRAY_SIZE 10
const String commandVerbs[ARRAY_SIZE] = {"Engage", "Jingle", "Press", "Play", "Tickle", "Randomize", "Flourish", "Jumpstart", "Pivot", "Tinker" };
const String commandNounsFirst[ARRAY_SIZE] = {"Portal", "Flopper", "Blink", "Spot", "Cable", "Wire", "Flower", "Hyper", "Giga", "Mega"};
const String commandNounsSecond[ARRAY_SIZE] = {"pins", "nobs", "bells", "pops", "spirals", "coils", "springs", "cogs", "screws", "bolts"};

int lineHeight = 20;

const int obstructionThresholdOne = 10;
const int obstructionThresholdTwo = 5;
bool obstructionActive = false; 



// Define LED and pushbutton pins
#define BUTTON1_PIN 25
#define BUTTON2_PIN 13
#define SWITCH_PIN1 15
#define SWITCH_PIN2 2
#define BACKLIGHT_PIN 32


// border fns
void drawBorder(uint16_t color) {
  tft.drawRect(0, 0, tft.width(), tft.height(), color);
  tft.drawRect(1, 1, tft.width()-2, tft.height()-2, color);
}

void formatMacAddress(const uint8_t *macAddr, char *buffer, int maxLength)
// Formats MAC Address
{
  snprintf(buffer, maxLength, "%02x:%02x:%02x:%02x:%02x:%02x", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
}

void startExpireTimer() {
  timerStop(askExpireTimer);
  timerWrite(askExpireTimer, 0);
  timerStart(askExpireTimer);
}

void receiveCallback(const esp_now_recv_info_t *macAddr, const uint8_t *data, int dataLen)
/* Called when data is received
   You can receive 3 types of messages
   1) a "ASK" message, which indicates that your device should display the cmd if the device is free
   2) a "DONE" message, which indicates the current ASK? cmd has been executed
   3) a "PROGRESS" message, indicating a change in the progress of the spaceship
   
   Messages are formatted as follows:
   [A/D]: cmd
   For example, an ASK message for "Twist the wutangs":
   A: Twist the wutangs
   For example, a DONE message for "Engage the devnobs":
   D: Engage the devnobs
   For example, a PROGESS message for 75% progress
   P: 75
*/

{
  // Only allow a maximum of 250 characters in the message + a null terminating byte
  char buffer[ESP_NOW_MAX_DATA_LEN + 1];
  int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
  strncpy(buffer, (const char *)data, msgLen);

  // Make sure we are null terminated
  buffer[msgLen] = 0;
  String recvd = String(buffer);
  Serial.println(recvd);
  // Format the MAC address
  char macStr[18];
  // formatMacAddress(macAddr, macStr, 18);

  // Send Debug log message to the serial port
  Serial.printf("Received message from: %s \n%s\n", macStr, buffer);
  if (recvd[0] == 'A' && cmdRecvd == waitingCmd && random(100) < 30)  //only take an ask if you don't have an ask already and only take it XX% of the time
  {
    recvd.remove(0, 3);
    cmdRecvd = recvd;
    redrawCmdRecvd = true;
    startExpireTimer();
  } else if (recvd[0] == 'D' && recvd.substring(3) == cmdRecvd) {
    timerStop(askExpireTimer);
    timerWrite(askExpireTimer, 0);
    flashGreenBorder = true;
    delay(1000);
    flashGreenBorder = false;
    cmdRecvd = waitingCmd;
    progress = progress + 1;
    broadcast("P: " + String(progress));
    redrawCmdRecvd = true;

  } else if (recvd[0] == 'P') {
    recvd.remove(0, 3);
    progress = recvd.toInt();
    redrawProgress = true;
  }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
  char macStr[18];
  formatMacAddress(macAddr, macStr, 18);
  Serial.print("Last Packet Sent to: ");
  Serial.println(macStr);
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void broadcast(const String &message)
// Emulates a broadcast
{
  // Broadcast a message to every device in range
  uint8_t broadcastAddress[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_peer_info_t peerInfo = {};
  memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peerInfo);
  }
  // Send message
  Serial.print("Sent message: ");
  Serial.println(message);
  esp_err_t result = esp_now_send(broadcastAddress, (const uint8_t *)message.c_str(), message.length());
}

void IRAM_ATTR sendCmd1() {
  scheduleCmd1Send = true;
}

void IRAM_ATTR sendCmd2() {
  scheduleCmd2Send = true;
}

void IRAM_ATTR sendCmd3() {
  scheduleCmd3Send = true;
}

void IRAM_ATTR onAskReqTimer() {
  scheduleCmdAsk = true;
}

void IRAM_ATTR onAskExpireTimer() {
  askExpired = true;
}

void espnowSetup() {
  // Set ESP32 in STA mode to begin with
  delay(500);
  WiFi.mode(WIFI_STA);
  Serial.println("ESP-NOW Broadcast Demo");

  // Print MAC address
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Disconnect from WiFi
  WiFi.disconnect();

  // Initialize ESP-NOW
  if (esp_now_init() == ESP_OK) {
    Serial.println("ESP-NOW Init Success");
    esp_now_register_recv_cb(receiveCallback);
    esp_now_register_send_cb(sentCallback);
  } else {
    Serial.println("ESP-NOW Init Failed");
    delay(3000);
    ESP.restart();
  }
}

void buttonSetup() {
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(SWITCH_PIN1, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON1_PIN), sendCmd1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON2_PIN), sendCmd2, FALLING);
  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN1), sendCmd3, CHANGE);
}

void textSetup() {
  tft.init();
  tft.setRotation(0);

  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  drawControls();

  cmdRecvd = waitingCmd;
  redrawCmdRecvd = true;
}

void timerSetup() {
  // https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/api/timer.html
  askRequestTimer = timerBegin(1000000); // 1MHz
  timerAttachInterrupt(askRequestTimer, &onAskReqTimer);
  timerAlarm(askRequestTimer, 5 * 1000000, true, 0);  //send out an ask every 5 secs
  timerStart(askRequestTimer);

  askExpireTimer = timerBegin(1000000);
  timerAttachInterrupt(askExpireTimer, &onAskExpireTimer);
  timerAlarm(askExpireTimer, expireLength * 1000000, true, 0);
  timerStart(askExpireTimer);
}

void drawScreenObstructionOne() {
  for (int i = 0; i < 200; i++) { 
        int x = random(tft.width());
        int y = random(tft.height());
        tft.drawPixel(x, y, TFT_BLUE); 
    }
}
void drawScreenObstructionTwo() {
  for (int i = 0; i < 80; i++) { 
        int x = random(tft.width());
        int y = random(tft.height());
        tft.drawPixel(x, y, TFT_BLUE); 
    }
  for (int i = 0; i < 35; i++) { 
        int x1 = random(tft.width());
        int y1 = random(tft.height());
        int x2 = random(tft.width());
        int y2 = random(tft.height());
        tft.drawLine(x1, y1, x2, y2, TFT_YELLOW); 
    }
}

void setup() {
  Serial.begin(115200);

  textSetup();
  buttonSetup();
  espnowSetup();
  timerSetup();

  pinMode(BACKLIGHT_PIN, OUTPUT);  // Set the backlight pin as an output
  analogWrite(BACKLIGHT_PIN, 255);
}

String genCommand() {
  String verb = commandVerbs[random(ARRAY_SIZE)];
  String noun1 = commandNounsFirst[random(ARRAY_SIZE)];
  String noun2 = commandNounsSecond[random(ARRAY_SIZE)];
  return verb + " " + noun1 + noun2;
}

void drawControls() {

  cmd1 = genCommand();
  cmd2 = genCommand();
  cmd1.indexOf(' ');
  
  cmd3 = genCommand();
  cmd3 = "Toggle " + cmd3.substring(cmd3.indexOf(' ') + 1);
  tft.setTextSize(1);
  tft.drawString("B1: " + cmd1.substring(0, cmd1.indexOf(' ')), 3, 90, 2);
  tft.drawString(cmd1.substring(cmd1.indexOf(' ') + 1), 3, 90 + lineHeight, 2);
  tft.drawString("B2: " + cmd2.substring(0, cmd2.indexOf(' ')), 3, 140, 2);
  tft.drawString(cmd2.substring(cmd2.indexOf(' ') + 1), 3, 140 + lineHeight, 2);
  tft.drawString("S: " + cmd3.substring(0, cmd3.indexOf(' ')), 3, 190, 2);
  tft.drawString(cmd3.substring(cmd3.indexOf(' ') + 1), 3, 190 + lineHeight, 2);
}

void debugTimer() {
  Serial.print("timer read (us):");
  Serial.println(timerRead(askExpireTimer));
  Serial.print("reaminaing time:");
  Serial.println((expireLength * 1000000.0 - timerRead(askExpireTimer)) / 1000000.0);
}

void loop() {

  if (scheduleCmd1Send) {
    broadcast("D: " + cmd1);
    scheduleCmd1Send = false;
  }
  if (scheduleCmd2Send) {
    broadcast("D: " + cmd2);
    scheduleCmd2Send = false;
  }
  if (scheduleCmd3Send) {
    broadcast("D: " + cmd3);
    scheduleCmd3Send = false;
  }
  if (scheduleCmdAsk) {
    String cmdList[] = {cmd1, cmd2, cmd3};
    String cmdAsk = cmdList[(int)random(3)];
    broadcast("A: " + cmdAsk);
    scheduleCmdAsk = false;
  }
  if (askExpired) {
    progress = max(0, progress - 1);
    broadcast(String(progress));
    //tft.fillRect(0, 0, 135, 90, TFT_RED);
    cmdRecvd = waitingCmd;
    redrawCmdRecvd = true;
    askExpired = false;


    // End game if progress reaches 0
    if (progress == 0) {
        tft.fillScreen(TFT_RED);
        tft.setTextSize(3);
        tft.setTextColor(TFT_WHITE, TFT_RED);
        tft.drawString("GAME", 35, 50, 1);
        tft.drawString("OVER", 35, 100, 1);
        delay(5000); // Display the game-over screen for 5 seconds
        ESP.restart(); // Restart the device or handle any other end-game logic
    }
  }
  //as timer runs out, add obstruction
  float remainingTime = (expireLength * 1000000.0 - timerRead(askExpireTimer)) / 1000000.0;
  if (remainingTime < obstructionThresholdTwo && !obstructionActive) {
    drawScreenObstructionTwo();
    obstructionActive = true;
  } else if (remainingTime < obstructionThresholdOne && !obstructionActive) {
    drawScreenObstructionOne();
    obstructionActive = true;
  }

  if (flashGreenBorder && millis() - lastFlashTime > flashInterval) {
    borderState = !borderState;
    if (borderState) {
      drawBorder(TFT_GREEN);
    } else {
      drawBorder(TFT_BLACK);
    }
    lastFlashTime = millis();
  }

  // progress logic
  if ((millis() - lastRedrawTime) > 50) {
    tft.fillRect(15, lineHeight * 2 + 14, 100, 6, TFT_GREEN);
    float progressWidth = (((expireLength * 1000000.0) - timerRead(askExpireTimer)) / (expireLength * 1000000.0)) * 98;
    tft.fillRect(16, lineHeight * 2 + 14 + 1, progressWidth, 4, TFT_RED);

    debugTimer();
    Serial.println(progressWidth);

    if (progressWidth < 49.0 && progressWidth > 25.0 ) {
      analogWrite(BACKLIGHT_PIN, 128); // decrease brightness to 1/2
      drawBorder(TFT_RED);
      drawBorder(TFT_BLACK);
    } else if (progressWidth < 24.9 && progressWidth >= 0) {
      analogWrite(BACKLIGHT_PIN, 64); // decrease brightness to 1/4
      drawBorder(TFT_RED);
      drawBorder(TFT_BLACK);
  
      for (int i = 0; i < 50; i++) { 
        int x = random(tft.width());
        int y = random(tft.height());
        tft.drawPixel(x, y, TFT_RED); // Draw a black dot
      }
    } else {
      analogWrite(BACKLIGHT_PIN, 255); // brightness back to default
      drawBorder(TFT_BLACK);
    }

    if (progressWidth <= 0.5) {
      progress = progress - 1;
    }

    lastRedrawTime = millis();
  }

  if ((millis() - lastRedrawTime) > 50) {
    tft.fillRect(15, lineHeight * 2 + 14, 100, 6, TFT_GREEN);
    tft.fillRect(16, lineHeight * 2 + 14 + 1, (((expireLength * 1000000.0) - timerRead(askExpireTimer)) / (expireLength * 1000000.0)) * 98, 4, TFT_RED);

    lastRedrawTime = millis();
  }

  if (redrawCmdRecvd || redrawProgress) {
    // clearing obstruction when task is completed
    if (obstructionActive) {
      tft.fillRect(0, 0, tft.width(), tft.height(), TFT_BLACK); 
      obstructionActive = false;
    }

    tft.fillRect(0, 0, 135, 90, TFT_BLACK);
    tft.drawString(cmdRecvd.substring(0, cmdRecvd.indexOf(' ')), 3, 0, 2);
    tft.drawString(cmdRecvd.substring(cmdRecvd.indexOf(' ') + 1), 3, 0 + lineHeight, 2);

      cmd3 = "Toggle " + cmd3.substring(cmd3.indexOf(' ') + 1);
      tft.setTextSize(1);
      tft.drawString("B1: " + cmd1.substring(0, cmd1.indexOf(' ')), 3, 90, 2);
      tft.drawString(cmd1.substring(cmd1.indexOf(' ') + 1), 3, 90 + lineHeight, 2);
      tft.drawString("B2: " + cmd2.substring(0, cmd2.indexOf(' ')), 3, 140, 2);
      tft.drawString(cmd2.substring(cmd2.indexOf(' ') + 1), 3, 140 + lineHeight, 2);
      tft.drawString("S: " + cmd3.substring(0, cmd3.indexOf(' ')), 3, 190, 2);
      tft.drawString(cmd3.substring(cmd3.indexOf(' ') + 1), 3, 190 + lineHeight, 2);

    redrawCmdRecvd = false;

    if (progress >= 10) {
      tft.fillScreen(TFT_BLUE);
      tft.setTextSize(3);
      tft.setTextColor(TFT_WHITE, TFT_BLUE);
      tft.drawString("GO", 45, 20, 2);
      tft.drawString("COMS", 20, 80, 2);
      tft.drawString("3930!", 18, 130, 2);
      delay(6000);
      ESP.restart();
    } else {
      tft.fillRect(15, lineHeight * 2 + 5, 100, 6, TFT_GREEN);
      tft.fillRect(16, lineHeight * 2 + 5 + 1, progress, 4, TFT_BLUE);
    }
    redrawProgress = false;
  }
}
