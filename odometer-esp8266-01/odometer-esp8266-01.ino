// ESP8266 v3.1.2
//  4 seconds of rapid flashing after startup - starting with the reset button pressed or absence of login in memory.
// A WIFI network named Odometer is created, to which you must connect and set the login and Password of the home network,
// then perform a reboot.
//
//  4 seconds of slow flashing after startup - connecting to a previously set WIFI network.
// To reset, you need to start with the reset button pressed.
//
// Endless rapid and slow flashing after startup - unable to connect to WIFI.
// May be related to an incorrect login or password stored in the device's memory.

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <Ticker.h>

Ticker timer;

const byte LOGIN_ADDRESS = 0;
const byte LOGIN_ADDRESS_LENGTH = 20;
const byte PASSWORD_ADDRESS = 20;
const byte PASSWORD_ADDRESS_LENGTH = 20;
const byte HALL_SENSOR = 0;
const byte LED_PIN = 1;
const byte SETUP_BUTTON = 3;
const byte TRY_COUNT = 50;
const float SECTION_LENGTH_IN_KILOMETERS = 0.00005;
const float TIMER_INTERRUPT_IN_SECONDS = 1.0;

volatile unsigned long lastInterruptTime = 0;
volatile int counter = 0;
volatile float speed = 0;
volatile float distance = 0;

ESP8266WebServer server(80);


//////////////////////////////////////////////////
//                                              //
//                 Controllers                  //
//                                              //
//////////////////////////////////////////////////


void handleIndex() {
  String response = String("{\"distance\": ") + distance + String(", \"speed\": ") + speed + "}";
  server.send(200, "application/json", response);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for(uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send(404, "text/plain", message);
}

void signIn() {
  String index = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'/><style>body{background-color:darkslategray}input[type=text],[type=password],select{width:100%;padding:12px 20px;margin:8px 0;display:inline-block;border:1px solid #ccc;border-radius:4px;box-sizing:border-box}button{background-color:#4caf50;border:0;color:white;padding:20px;text-align:center;text-decoration:none;display:inline-block;font-size:16px;margin:4px 2px;cursor:pointer;border-radius:12px;width:100%;opacity:1}button:active{opacity:.5}</style></head><body><div><input id='login' type='text' maxlength='20' minlength='1' onkeyup='checkLetters(login)'/><input id='password' type='password' maxlength='20' minlength='1' onkeyup='checkLetters(password)'/><button id='signIn' onclick='signIn()'>Sign In</button></div></body><script>function signIn(){var e=document.getElementById('login'),n=document.getElementById('password');if(e&&n){var t=new XMLHttpRequest;t.open('GET','setWifiCredentials?login='+e.value+'&password='+n.value,!0),t.send()}}function checkLetters(e){e.value=e.value.replace(/[^a-z  0-9]/i,'')}</script></html>";
  sendDataByPart(index);
}

void setWifiCredentials() {
  String login = server.arg("login");
  String password = server.arg("password");

  writeStringToEEPROM(LOGIN_ADDRESS, login);
  writeStringToEEPROM(PASSWORD_ADDRESS, password);

  server.send(200, "text/plain", "");
}

void reset() {
  distance = 0;

  server.send(204, "text/plain", "");
}


//////////////////////////////////////////////////
//                                              //
//                  Other                       //
//                                              //
//////////////////////////////////////////////////


void sendDataByPart(String data) {
  int contentSize = data.length();
  int transferred = 0;
  int packSize = 500;

  server.setContentLength(contentSize);
  server.send(200, "text/html", data.substring(transferred, transferred + packSize));

  transferred = transferred + packSize < contentSize
    ? transferred + packSize
    : contentSize;

  do {
    server.sendContent(data.substring(transferred, transferred + packSize));

    transferred = transferred + packSize < contentSize
      ? transferred + packSize
      : contentSize;
  } while(transferred < contentSize);
}

void writeStringToEEPROM(int address, String data) {
  char buf[data.length() + 1];
  data.toCharArray(buf, data.length() + 1);

  for(int i = 0; i < data.length() + 1; i++) {
      EEPROM.write(address + i, buf[i]);
  }

  EEPROM.commit();
}

void readStringFromEEPROM(int address, char *buf, int bufLength) {
  for(int i = 0; i < bufLength; i++) {
      buf[i] = EEPROM.read(address + i);
  }
}

void clearEEPROM(byte address, byte bufLength) {
  for(int i = 0; i < bufLength; i++) {
      EEPROM.write(address + i, 0);
  }
}

bool isCleanEEPROM(byte address, byte bufLength) {
  for(byte i = 0; i < bufLength; i++) {
      byte value = EEPROM.read(address + i);

      if(value != 0) {
        return false;
      }
  }

  return true;
}


//////////////////////////////////////////////////
//                                              //
//                 Interrupts                   //
//                                              //
//////////////////////////////////////////////////


IRAM_ATTR void increaseCounter() {
  ++counter;
}

IRAM_ATTR void handleTimer() {
  unsigned long currentTime = millis();
  unsigned long timeDifference = currentTime - lastInterruptTime;

  if (timeDifference > 0 && counter > 0) {
    float distance_traveled = SECTION_LENGTH_IN_KILOMETERS * counter;
    distance += distance_traveled;
    speed = distance_traveled / (timeDifference / 3600000.0);
    counter = 0;
    lastInterruptTime = currentTime;
  }
  else
  {
    speed = 0;
  }
}


//////////////////////////////////////////////////
//                                              //
//                  Setup                       //
//                                              //
//////////////////////////////////////////////////


void fastBlink() {
  pinMode(LED_PIN, OUTPUT);

  for(int i = 0; i < 16; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(250);
  }
}

void slowBlink() {
  pinMode(LED_PIN, OUTPUT);
  
  for(int i = 0; i < 4; i++) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(1000);
  }
}

void createPointAccessPoint() {
  fastBlink();

  WiFi.softAPdisconnect(true);
  WiFi.softAP("Odometer");
  
  server.on("/", signIn);
  server.on("/setWifiCredentials", setWifiCredentials);
  server.onNotFound(handleNotFound);
  server.begin();
}

void connectToOtherAccessPoint() {
  slowBlink();

  char login[LOGIN_ADDRESS_LENGTH];
  readStringFromEEPROM(LOGIN_ADDRESS, login, LOGIN_ADDRESS_LENGTH);
  char password[PASSWORD_ADDRESS_LENGTH];
  readStringFromEEPROM(PASSWORD_ADDRESS, password, PASSWORD_ADDRESS_LENGTH);
  
  WiFi.begin(login, password );

  byte tryCount = 0;

  while(WiFi.status() != WL_CONNECTED && tryCount < TRY_COUNT) {
    ++tryCount;

    fastBlink();
    slowBlink();
  }

  server.on("/", handleIndex);
  server.on("/reset", reset);
  server.onNotFound(handleNotFound);
  server.begin();

  WiFi.softAPdisconnect(true);
}

void setup( void ) {
  EEPROM.begin(512);
  pinMode(SETUP_BUTTON, INPUT_PULLUP);

  int setupButton = digitalRead(SETUP_BUTTON);

  if(setupButton == LOW) {
    clearEEPROM(LOGIN_ADDRESS, LOGIN_ADDRESS_LENGTH);
    clearEEPROM(PASSWORD_ADDRESS, PASSWORD_ADDRESS_LENGTH);

    createPointAccessPoint();
  }
  else {
    bool loginIsEmpty = isCleanEEPROM(LOGIN_ADDRESS, LOGIN_ADDRESS_LENGTH);
    bool passwordIsEmpty = isCleanEEPROM(PASSWORD_ADDRESS, PASSWORD_ADDRESS_LENGTH);

    if(loginIsEmpty || passwordIsEmpty) {
      createPointAccessPoint();

      return;
    }

    connectToOtherAccessPoint();
  }

  pinMode(HALL_SENSOR, INPUT);
  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR), increaseCounter, RISING);
  timer.attach(TIMER_INTERRUPT_IN_SECONDS, handleTimer);
}

void loop(void) {
	server.handleClient();
}