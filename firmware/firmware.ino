/*
 * @autor: nikzin
 *
 * CAD Files and original Code this is based on: https://www.instructables.com/Hollow-Clock-4/
 *
 * Based on the great work of shiura. Thank your for this great project.
 *
 * Instead of the Arduino Nano in the original Version from shiura, I used an ESP8266 D1 Mini, this also (barely) fits in the provided case.
 * By using the ESP I can get the real time from an NTP server. I also added daytime saving (since in Germany we still have that). I have not tested this function though.
 *
 * When you start up the clock please set the time to 12 o`clock and wait until the it connects to WiFi and sets itself.
 *
 * After that it updates the time every 10 seconds from the server.
 *
 * Powerconsumption is about 50mA on average (including WiFi communication and motor running every minute)
 *
 * Have fun with this version of the Hollow Clock 4!
 *
 * after installingThe ESP8266 Starts a Wifi Called HollowClock4 where you can Configure everything
 * if the Website of the Clock Dosent Open Automaticly Please Visit 10.10.10.10 in your browser
 *
 */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <sys/time.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>


DNSServer dnsServer;
ESP8266WebServer server(80);


//save int in EEPROM
void saveInt(int address, int value) {
  byte low = (value & 0xFF);
  byte high = ((value >> 8) & 0xFF);
  EEPROM.write(address, low);
  EEPROM.write(address + 1, high);
  EEPROM.commit();
}

// read int from EEPROM
int readInt(int address) {
  byte low = EEPROM.read(address);
  byte high = EEPROM.read(address + 1);
  return (high << 8) | low;
}

// save string in EEPROM
void saveString(int address, String string) {
  char charBuf[string.length() + 1];
  string.toCharArray(charBuf, string.length() + 1);
  for (int i = 0; i < string.length(); i++) {
    EEPROM.write(address + i, charBuf[i]);
  }
  EEPROM.write(address + string.length(), '\0');
  EEPROM.commit();
}

// read string from EEPROM
String readString(int address) {
  String string;
  char charBuf[100];
  int i;
  for (i = 0; EEPROM.read(address + i) != '\0'; i++) {
    charBuf[i] = EEPROM.read(address + i);
  }
  charBuf[i] = '\0';
  string = charBuf;
  return string;
}

// save bool in EEPROM
void saveBool(int address, bool value) {
  EEPROM.write(address, value);
  EEPROM.commit();
}

// read bool from EEPROM
bool readBool(int address) {
  return EEPROM.read(address);
}

// check if EEPROM is from Hollow Clock 4
bool isHC4() {
  char charBuf[4];
  for (int i = 0; i < 4; i++) {
    charBuf[i] = EEPROM.read(i);
  }
  String string = charBuf;
  return string == "HC4";
}

void readEEPROM() {
  Serial.println("EEPROM:");
  for (int i = 0; i < 512; i++) {
    Serial.print(EEPROM.read(i));
    Serial.print(" ");
  }
  Serial.println();
}

String ssid;
String password;
bool flipRotation;
int STEPS_PER_ROTATION;
String MY_TZ;
String MY_NTP_SERVER;
int delaytime;
int port[4];
bool setupmode = false;

void EEPROM_init() {
  if (isHC4()) { //  check if EPROM is From Hollow Clock 4
    Serial.println("EEPROM is from Hollow Clock 4");
    readEEPROM();
  }
  else {
    Serial.println("EEPROM is not from Hollow Clock 4");

    // clear EEPROM
    for (int i = 0; i < 512; i++) {
      EEPROM.write(i, 0);
      yield();
    }
    EEPROM.commit();

    saveString(0, "HC4");

    saveInt(5, 0); //  last saved hour
    saveInt(7, 0); //  last saved minute

    // save default values
    saveString(10, "SSID"); //  WiFi SSID max 32 chars
    saveString(50, "PASSWORD"); //  WiFi PASSWORD max 63 chars

    saveBool(150, false); //  Flip rotation
    saveInt(151, 30720); //  STEPS_PER_ROTATION  
    saveInt(153, 2); //  delaytime 
    saveString(200, "CET-1CEST,M3.5.0/02,M10.5.0/03"); //  MY_TZ  https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm max 49 chars
    saveString(250, "pool.ntp.org"); //  MY_NTP_SERVER  max 49 chars
  }

  Serial.println(readString(0));
  Serial.println(readString(10));
  Serial.println(readString(50));
  Serial.println(readBool(150));
  Serial.println(readInt(151));
  Serial.println(readInt(153));
  Serial.println(readString(200));
  Serial.println(readString(250));

  // read values from EEPROM
  ssid = readString(10);
  password = readString(50);
  flipRotation = readBool(150);
  STEPS_PER_ROTATION = readInt(151);
  delaytime = readInt(153);
  MY_TZ = readString(200);
  MY_NTP_SERVER = readString(250);


  if (flipRotation == true) {
    port[0] = D5;
    port[1] = D6;
    port[2] = D7;
    port[3] = D8;

  }
  else {
    port[0] = D8;
    port[1] = D7;
    port[2] = D6;
    port[3] = D5;
  }

}



time_t now;                         // this is the epoch
tm tm;                              // the structure tm holds time information in a more convient way

// sequence of stepper motor control
int seq[8][4] = {
  {  LOW, HIGH, HIGH,  LOW},
  {  LOW,  LOW, HIGH,  LOW},
  {  LOW,  LOW, HIGH, HIGH},
  {  LOW,  LOW,  LOW, HIGH},
  { HIGH,  LOW,  LOW, HIGH},
  { HIGH,  LOW,  LOW,  LOW},
  { HIGH, HIGH,  LOW,  LOW},
  {  LOW, HIGH,  LOW,  LOW}
};

void setTimezone(String timezone) {
  setenv("TZ", timezone.c_str(), 1);  //  Now adjust the time zone
  tzset();
}

// Variables to save date and time and other needed parameters
int Year, Minute, Hour, currHour, currMinute, hourDiff, minuteDiff, stepsToGo;

bool ntpError = false;


void rotate(int step) { // original function from shiura
  static int phase = 0;
  int i, j;
  int delta = (step > 0) ? 1 : 7;
  int dt = 20;

  step = (step > 0) ? step : -step;
  for (j = 0; j < step; j++) {
    phase = (phase + delta) % 8;
    for (i = 0; i < 4; i++) {
      digitalWrite(port[i], seq[phase][i]);
    }
    delay(dt);
    server.handleClient();
    if (dt > delaytime) dt--;
  }
  // power cut
  for (i = 0; i < 4; i++) {
    digitalWrite(port[i], LOW);
  }
}

void rotateFast(int step) { // this is just to rotate to the current time faster, when clock is started
  static int phase = 0;
  int i, j;
  int delta = (step > 0) ? 1 : 7;
  int dt = 1;

  step = (step > 0) ? step : -step;
  for (j = 0; j < step; j++) {
    phase = (phase + delta) % 8;
    for (i = 0; i < 4; i++) {
      digitalWrite(port[i], seq[phase][i]);
    }
    delay(dt);
    server.handleClient();
    if (dt > delaytime) dt--;
  }
  // power cut
  for (i = 0; i < 4; i++) {
    digitalWrite(port[i], LOW);
  }
}


void updateTime() {

  getLocalTime(&tm);

  Hour = tm.tm_hour;
  Minute = tm.tm_min;
  Year = tm.tm_year + 1900;

}

void movetocurrtime(bool fastmode) {
  updateTime();

  if (Year < 2000) {
    Serial.println("NTP Server not reachable!!");
    ntpError = true;
    return;
  }

  if (Hour != currHour || Minute != currMinute) {

    Serial.print("Current time: ");
    Serial.print(Hour);
    Serial.print(":");
    Serial.println(Minute);

    if (Hour > 12) {
      Hour -= 12;
    }

    hourDiff = Hour - currHour;
    minuteDiff = Minute - currMinute;

    if ( hourDiff < 0) {
      hourDiff += 12;
    }

    if (fastmode == true) {
      rotateFast((STEPS_PER_ROTATION * hourDiff)+((minuteDiff * STEPS_PER_ROTATION) / 60));
    }
    else {
      rotate((STEPS_PER_ROTATION * hourDiff)+((minuteDiff * STEPS_PER_ROTATION) / 60));
    }

    currHour = Hour;
    currMinute = Minute;
  }

}

//Ticker update;
bool testWifi() {
  for (int i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(1000);
  }
  return false;
}

void initWifi() {

  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  Serial.println("");

  if (testWifi()) {
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    setupmode = false;
  }
  else {
    Serial.println("WiFi connect failed");
    WiFi.disconnect();
    setupmode = true;


    IPAddress apIP(10, 10, 10, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    dnsServer.start(53, "*", WiFi.softAPIP());

    WiFi.softAP("HollowClock4");
    Serial.println("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Please connect to AP and set WiFi credentials");
  }


  configTime(MY_TZ.c_str(), MY_NTP_SERVER.c_str());

}

void setup() {
  Serial.begin(115200);
  delay(1000);

  EEPROM.begin(512);

  Serial.println("Hollow Clock 4");
  Serial.println(isHC4());

  EEPROM_init();
  yield();

  initWifi();

  restServerRouting();
  server.begin();

  if (setupmode == false) {

    //at startup the clock expects it´s set to 12 o`clock

    int lasthour = readInt(5); //  this is the last saved hour
    int lastminute = readInt(7); //  this is the last saved minute
    if (lasthour != 0) {
      saveInt(5, 0);
    }
    if (lastminute != 0) {
      saveInt(7, 0);
    }

    currHour = lasthour;
    currMinute = lastminute;

    pinMode(port[0], OUTPUT);
    pinMode(port[1], OUTPUT);
    pinMode(port[2], OUTPUT);
    pinMode(port[3], OUTPUT);

    movetocurrtime(true);

  }

}

void rootPage() {
  String data = R"(
    <!DOCTYPE html>
<html lang="en">
    <head>
        <meta charset="UTF-8">
        <title>Holow Clock 4</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
    </head>
    <style>
        .button {
            height: 60px;
            width: 150px;
        }
        .row {
            text-align:center
        }
    </style>
    <body>
        <div class="row">
            <div class="cell">
                <h2>Settings</h2>
            </div>
            <button class="button"; onclick="window.location.href = '/wifi';">Wifi Settings</button><br><br>
            <button class="button"; onclick="window.location.href = '/settings';">Settings</button><br><br>

</html>
  )";


  server.send(200, "text/html", data);
}

void wifi() {
  String data = R"(
  <!DOCTYPE html>
<html lang="en">
    <head>
        <meta charset="UTF-8">
        <title>Holow Clock 4</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
    </head>
    <!DOCTYPE html>
    <meta name="viewport" content="width=device-width, initial-scale=1.0"> 
    <style>
        .button {
            height: 70px;
            width: 100px;
        }
        .row {
            text-align:center
        }
    </style>
    <body>
        <div class="row">
            <div class="cell">
                <h2> Wifi Settings</h2>
            </div>
            <div>
                <form action="/api/wifi" method="post">
                    <label for="ssid">SSID:</label>
                    <input type="text" id="ssid" name="ssid"><br><br>
                    <label for="password">Password:</label>
                    <input type="text" id="password" name="password"><br><br>
                    <input type="submit" value="Save">
                </form>
            </div>    
        </div>
    </body>
</html>
)";


  server.send(200, "text/html", data);
}

void settings() {
  bool flip = readBool(150);
  STEPS_PER_ROTATION = readInt(151);
  delaytime = readInt(153);
  String strMY_TZ = readString(200);
  String strMY_NTP_SERVER = readString(250);
  String strflipRotation = "";
  if (flip == true) {
    strflipRotation = "checked";
  }

  String data = R"(
  <!DOCTYPE html>
<html lang="en">
    <head>
        <meta charset="UTF-8">
        <title>Holow Clock 4</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
    </head>
    <!DOCTYPE html>
    <meta name="viewport" content="width=device-width, initial-scale=1.0"> 
    <style>
        .button {
            height: 70px;
            width: 100px;
        }
        .row {
            text-align:center
        }
    </style>
    <body>
        <div class="row">
            <div class="cell">
                <h2>Settings</h2>
            </div>
            <div>
                <form action="/api/settings" method="post">
                    <label for="flipRotation"> Flip Rotation</label>
                    <input type="checkbox" id="flipRotation" name="flipRotation" value="1" )" + strflipRotation + R"( ><br>
                    if your motor rotate to the opposite direction<br>
                    <br>
                    <label for="STEPS_PER_ROTATION">STEPS_PER_ROTATION:</label>
                    <input type="text" id="STEPS_PER_ROTATION" name="STEPS_PER_ROTATION" value=")" + STEPS_PER_ROTATION + R"("><br>
                    adjusted steps for a full turn of minute rotor<br>
                    <br>
                    <label for="delaytime">delaytime:</label>
                    <input type="text" id="delaytime" name="delaytime" value=")" + delaytime + R"(" ><br>
                    wait for a single step of stepper<br>
                    <br>
                    <label for="MY_TZ">time zone:</label>
                    <input type="text" id="MY_TZ" name="MY_TZ" value=")" + strMY_TZ + R"("><br>
                    just set the right string for your time zone: <br>
                    https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv<br>
                    <br>
                    <label for="MY_NTP_SERVER">NTP_SERVER:</label>
                    <input type="text" id="MY_NTP_SERVER" name="MY_NTP_SERVER" value=")" + strMY_NTP_SERVER + R"("><br><br>

                    <div style="color: red" >WARNING: wait until the Clock is not Moving</div>  <br>
                    
                    <input type="submit" value="Save">
                </form>
            </div>    
        </div>
    </body>
</html>
  )";
  server.send(200, "text/html", data);
}

void apiWifi() {
  String new_ssid = server.arg("ssid");
  String new_password = server.arg("password");

  saveString(10, new_ssid);
  saveString(50, new_password);


  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "");

  saveInt(5, currHour);
  saveInt(7, currMinute);

  ESP.restart();
}

void apisettings() {
  bool flipRotation = (server.arg("flipRotation") == "1");
  int STEPS_PER_ROTATION = server.arg("STEPS_PER_ROTATION").toInt();;
  int delaytime = server.arg("delaytime").toInt();;
  String MY_TZ = server.arg("MY_TZ");
  String MY_NTP_SERVER = server.arg("MY_NTP_SERVER");


  saveBool(150, flipRotation);
  saveInt(151, STEPS_PER_ROTATION);
  saveInt(153, delaytime);
  saveString(200, MY_TZ);
  saveString(250, MY_NTP_SERVER);

  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "");

  saveInt(5, currHour);
  saveInt(7, currMinute);

  ESP.restart();
}


void restServerRouting() {
  server.on(F("/"), HTTP_GET, rootPage);
  server.on(F("/wifi"), HTTP_GET, wifi);
  server.on(F("/settings"), HTTP_GET, settings);

  server.on(F("/api/settings"), HTTP_POST, apisettings);
  server.on(F("/api/wifi"), HTTP_POST, apiWifi);
  server.onNotFound(wifi);
}



void loop() {
  server.handleClient();
  if (setupmode == true) {
    dnsServer.processNextRequest();
    return;
  }
  else if (ntpError == true) {
    for (int i = 0; i < 600; i++) {
    Serial.print("NTP Server Was not reachable!! Retrying in ");
    Serial.print((600 - i)/10);
    Serial.println(" Seconds");

    server.handleClient();
    delay(100);
    }
    ntpError = false;
    return;
  }
  else {
    movetocurrtime(false);
  }

  for (int i = 0; i < 10; i++) {
    server.handleClient();
    delay(100);
  }

}
