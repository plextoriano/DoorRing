/* This example demonstrates the different low-power modes of the ESP8266

  The initial setup was a WeMos D1 Mini with 3.3V connected to the 3V3 pin through a meter
  so that it bypassed the on-board voltage regulator and USB chip.  There's still about
  0.3 mA worth of leakage amperage due to the unpowered chips.  These tests should work with
  any module, although on-board components will affect the actual current measurement.
  While the modem is turned on the amperage is > 67 mA or changing with a minimum value.
  To verify the 20 uA Deep Sleep amperage the voltage regulator and USB chip were removed.

  This test series requires an active WiFi connection to illustrate two tests.  If you
  have problems with WiFi, uncomment the #define DEBUG for additional WiFi error messages.
  The test requires a pushbutton switch connected between D3 and GND to advance the tests.
  You'll also need to connect D0/GPIO16 to RST for the Deep Sleep tests.  If you forget to
  connect D0 to RST it will hang after the first Deep Sleep test. D0 is driven high during
  Deep Sleep, so you should use a Schottky diode between D0 and RST if you want to use a
  reset switch; connect the anode of the diode to RST, and the cathode to D0.

  Additionally, you can connect an LED from any free pin through a 1K ohm resistor to the
  3.3V supply, though preferably not the 3V3 pin on the module or it adds to the measured
  amperage.  When the LED blinks you can proceed to the next test.  When the LED is lit
  continuously it's connecting WiFi, and when it's off the CPU is asleep.  The LED blinks
  slowly when the tests are complete.  Test progress can also be shown on the serial monitor.

  WiFi connections will be made over twice as fast if you can use a static IP address.

  This example is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This example is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this example; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA  */

#include <ESP8266WiFi.h>
#include <coredecls.h>  // crc32()
#include <PolledTimeout.h>
#include <include/WiFiState.h>  // WiFiState structure details
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Time.h>
#include <TimeLib.h>

#define DEBUG  // prints WiFi connection info to serial, uncomment if you want WiFi messages
#ifdef DEBUG
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT(x) Serial.print(x)
#else
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT(x)
#endif

#define WAKE_UP_PIN 0  // D3/GPIO0, can also force a serial flash upload with RESET
#define WIFI_STRENGTH 20 // PERCENT POWER WIFI
// you can use any GPIO for WAKE_UP_PIN except for D0/GPIO16 as it doesn't support interrupts

// uncomment one of the two lines below for your LED connection (optional)
//#define LED 5  // D1/GPIO5 external LED for modules with built-in LEDs so it doesn't add amperage
#define LED 2  // D4/GPIO2 LED for ESP-01,07 modules; D4 is LED_BUILTIN on most other modules
// you can use LED_BUILTIN, but it adds to the measured amperage by 0.3mA to 6mA.
//#define LED_BUILTIN D4

// Telegram BOT Token (Get from Botfather)
#define BOT_TOKEN "---myBotfatherToken---"

// Use @myidbot (IDBot) to find out the chat ID of an individual or a group
// Also note that you need to click "start" on a bot before it can
// message you
#define CHAT_ID "---myChatID---"

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);
volatile bool telegramButtonPressedFlag = false;

ADC_MODE(ADC_VCC);  // allows you to monitor the internal VCC level; it varies with WiFi load
// don't connect anything to the analog input pin(s)!

// enter your WiFi configuration below
const char* AP_SSID = "---mySSID---";      // your router's SSID here
const char* AP_PASS = "---mySSIDPassword---";  // your router's password here
IPAddress staticIP(0, 0, 0, 0);    // parameters below are for your static IP address, if used
IPAddress gateway(0, 0, 0, 0);
IPAddress subnet(0, 0, 0, 0);
IPAddress dns1(0, 0, 0, 0);
IPAddress dns2(0, 0, 0, 0);
uint32_t timeout = 60E3;  // 30 second timeout on the WiFi connection

/*
//#define TESTPOINT  //  used to track the timing of several test cycles (optional)
#ifdef TESTPOINT
#define testPointPin 4  // D2/GPIO4, you can use any pin that supports interrupts
#define testPoint_HIGH digitalWrite(testPointPin, HIGH)
#define testPoint_LOW digitalWrite(testPointPin, LOW)
#else
#define testPoint_HIGH
#define testPoint_LOW
#endif
*/

esp8266::polledTimeout::oneShotMs wifiTimeout(timeout);   // 30 second timeout on WiFi connection

void wakeupCallback() {  // unlike ISRs, you can do a print() from a callback function
  //testPoint_LOW;         // testPoint tracks latency from WAKE_UP_PIN LOW to testPoint LOW
  printMillis();         // show time difference across sleep; millis is wrong as the CPU eventually stops
  Serial.println(F("Wake Up from Light Sleep - this is the callback"));
}

void setup() {
#ifdef TESTPOINT
  pinMode(testPointPin, OUTPUT);  // test point for Light Sleep and Deep Sleep tests
  testPoint_LOW;                  // Deep Sleep reset doesn't clear GPIOs, testPoint LOW shows boot time
#endif
  //pinMode(LED, OUTPUT);                // activity and status indicator
  pinMode(WAKE_UP_PIN, INPUT_PULLUP);  // polled to advance tests, interrupt for Forced Light Sleep
  Serial.begin(115200);
  Serial.println();
  Serial.print(F("\nReset reason = "));
  String resetCause = ESP.getResetReason();
  Serial.println(resetCause);
  if ((resetCause == "External System") || (resetCause == "Power on")) { 
    Serial.println(F("I'm awake and starting the Low Power tests")); 
  }
  printMillis();  
}  // end of setup()

void loop() {
  //digitalWrite(LED, HIGH);              // turn on the LED
  runTest6();
  initWiFi();
  //digitalWrite(LED, LOW);
  reset();
  Serial.println("No se debería haber escrito");
}  // end of loop()

void runTest6() {
  
  Serial.println(F("\nForced Light Sleep, wake with GPIO interrupt"));
  Serial.flush();
  WiFi.mode(WIFI_OFF);      // you must turn the modem off; using disconnect won't work
  readVoltage();            // read internal VCC
  Serial.println(F("CPU going to sleep, pull WAKE_UP_PIN low to wake it (press the switch)"));
  printMillis();   // show millis() across sleep, including Serial.flush()
  //testPoint_HIGH;  // testPoint tracks latency from WAKE_UP_PIN LOW to testPoint LOW in callback
  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);
  gpio_pin_wakeup_enable(GPIO_ID_PIN(WAKE_UP_PIN), GPIO_PIN_INTR_LOLEVEL);
  // only LOLEVEL or HILEVEL interrupts work, no edge, that's an SDK or CPU limitation
  wifi_fpm_set_wakeup_cb(wakeupCallback);  // Set wakeup callback (optional)
  wifi_fpm_open();
  wifi_fpm_do_sleep(0xFFFFFFF);   // only 0xFFFFFFF, any other value and it won't disconnect the RTC timer
  delay(10);                      // it goes to sleep during this delay() and waits for an interrupt
  Serial.println(F("Wake up!"));  // the interrupt callback hits before this is executed*/
}

void reset() {
  readVoltage();  // read internal VCC
  Serial.println(F("\nRF_DISABLED, press the switch to do an ESP.restart()"));
  waitPushbutton(false, 1000);
  ESP.restart();
}

void waitPushbutton(bool usesDelay, unsigned int delayTime) {  // loop until they press the switch
  delay(50);                           // debounce time for the switch, pushbutton pressed
  while (!digitalRead(WAKE_UP_PIN)) {  // now wait for them to release the pushbutton
    delay(10);
  }
  delay(50);  // debounce time for the switch, pushbutton released
}

void readVoltage() {  // read internal VCC
  float volts = ESP.getVcc();
  Serial.printf("The internal VCC reads %1.2f volts\n", volts / 1000);
}

void printMillis() {
  Serial.print(F("millis() = "));  // show that millis() isn't correct across most Sleep modes
  Serial.println(millis());
  Serial.flush();  // needs a Serial.flush() else it may not print the whole message before sleeping
}

void initWiFi() {
  uint32_t wifiBegin = millis();  // how long does it take to connect
  
  /* Explicitly set the ESP8266 as a WiFi-client (STAtion mode), otherwise by default it
    would try to act as both a client and an access-point and could cause network issues
    with other WiFi devices on your network. */
  WiFi.persistent(false);  // don't store the connection each time to save wear on the flash
  WiFi.mode(WIFI_STA);
  WiFi.setOutputPower(WIFI_STRENGTH);                 // reduce RF output power, increase if it won't connect
  WiFi.config(staticIP, gateway, subnet);  // if using static IP, enter parameters at the top
  Serial.print(F("connecting to WiFi: "));
  Serial.println(AP_SSID);
  WiFi.begin(AP_SSID, AP_PASS);
  secured_client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org
  DEBUG_PRINT(F("my MAC: "));
  DEBUG_PRINTLN(WiFi.macAddress());

  wifiTimeout.reset(timeout);
  
  while (((!WiFi.localIP()) || (WiFi.status() != WL_CONNECTED)) && (!wifiTimeout)) { yield(); }

  if ((WiFi.status() == WL_CONNECTED) && WiFi.localIP()) {
    
    DEBUG_PRINTLN(F("WiFi connected"));
    Serial.print(F("WiFi connect time = "));
    float reConn = (millis() - wifiBegin);
    Serial.printf("%1.2f seconds\n", reConn / 1000);
    DEBUG_PRINT(F("WiFi Gateway IP: "));
    DEBUG_PRINTLN(WiFi.gatewayIP());
    DEBUG_PRINT(F("my IP address: "));
    DEBUG_PRINTLN(WiFi.localIP());
    getNTPTime();
    if(SendTelegramMessage()){
      Serial.println("TELEGRAM Successfully sent");
    }
  } else {
    Serial.println(F("WiFi timed out and didn't connect"));
  }
  WiFi.setAutoReconnect(true);  
}

void getNTPTime(){
  Serial.print("Retrieving time: ");
  configTime(0, 0, "pool.ntp.org"); // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
    Serial.print(".");
    delay(100);
    now = time(nullptr);
  }
  Serial.println("");
  Serial.print("Time synchronized: ");
  //Serial.println(now);
  printDate(now);
}

void printDate(time_t now){
  // Imprimimos la hora
  Serial.print(day(now));
  Serial.print("/");
  Serial.print(month(now));
  Serial.print("/");
  Serial.print(year(now));   
  Serial.print("  ");
  Serial.print(hour(now));
  Serial.print(":");
  Serial.print(minute(now));
  Serial.print(":");
  Serial.println(second(now));
}

bool SendTelegramMessage() {
  return bot.sendMessage(CHAT_ID, "Timbre Casa", "");  
}