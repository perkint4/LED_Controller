/* LED Controller
  2016 Feb Tim Perkins
  Code written to control LEDs for a reef tank
    Designed to be scalable for varying numbers of channels (needs minor changes to code) and time points (no code changes required)
    Supports auto acclimatisation - slowly increasing max PWM over user specified number of days
    Can control fans with on/off and PWM control based on temp
    Uses the built in watchdog timer - if using a standard Nano (without changing it to the Optiboot loader) this will not work, but
      shouldn't cause any grief if you leave it in. There's a bug in the default boot loader on the nano's that prevents the watchdog
      from being able to reset properly, but since the arduino has crashed for it to be triggered, it doesn't save you much! If you
      want to remove it, just comment out the following three lines where you find them (each only occurs once)
        #include <avr/wdt.h>
        wdt_disable();
        wdt_enable(WDTO_4S);
        wdt_reset();
  Hardware:
    Arduino (Designed to be usable with Nano v3 but larger ones will allow more channels and/or time slots)
    DS18B20 temp probes
    Rotary Encoder (with push button)
    I2C 2004 LCD
    I2C PCA9685 PWM
    I2C RTC (DS3231 preferably)
  Libraries
    LEDBloke's PCA9685_Lite
    FM NewLiquidCrystal v1.3.4 (https://bitbucket.org/fmalpartida/new-liquidcrystal/wiki/Home)
    Dallas OneWire v2.3.2 (http://playground.arduino.cc/Learning/OneWire)
    Wire (part of Arduino IDE)
    EEPROM (part of Arduino IDE)
  IDE
    Arduino IDE v1.6.8
*/
// ************* Library includes ************* //
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <PCA9685_Lite.h>
#include <EEPROM.h>
#include <avr/wdt.h>
// ************* Defines ************* //
// ************* Constants ************* //
const int  const_version_ID                         = 01;              // Gives me something to check to ensure the EEPROM contains the right settings! (Used with const_LED_channels)
const byte const_LED_channels                       = 10;
const byte const_time_slots                         = 10;              // maximum number of support time slots in a day
const byte const_temp_probes                        = 8;               // if changing this, need to supply the addresses in DS18B20_addr and update the array
const int  const_UIIntDelay                         = 100;             // milliseconds buffer over UI interrupt processing
const long const_UILightDelay                       = 300000;          // milliseconds delay to turn off LCD light
const long const_UICursorDelay                      = 120000;          // milliseconds delay to turn off LCD Blink cursor
const int  const_LEDFadePeriod                      = 10000;           // milliseconds to take over fading in the LEDs if powered on when they should be lit
const int  const_TempChangeDelay                    = 5000;            // milliseconds buffer over temp checks
const byte const_TempFadeDown                       = 5;               // LEDs will fade to 0 as temp increases from savedSettings.temp_max to this much over
const byte const_RE_btn_PIN                         = 2;               // D2 is Int0
const byte const_RE_a_PIN                           = 3;               // D3 is Int1
const byte const_RE_b_PIN                           = 4;
const byte const_SCW_pwr_PIN                        = 8;
const byte const_FAN_PWM_PIN                        = 9;               // D9 runs on Timer1 so can be adjusted without affecting millis if need to change frequency
const byte const_OneWire_PIN                        = 17;              // A3
const byte const_I2C_LCD_addr                       = 0x27;
const byte const_I2C_PCA9685_addr                   = 0x40;
const int  const_I2C_DS3231_addr                    = 0x68;
const byte const_DS18B20_addr[const_temp_probes][8] = {0x28, 0x04, 0xFA, 0x14, 0x07, 0x00, 0x00, 0x95,         // LBO
                                                       0x28, 0xEC, 0x21, 0x15, 0x07, 0x00, 0x00, 0xBC,         // LBM
                                                       0x28, 0xA6, 0xC3, 0x14, 0x07, 0x00, 0x00, 0xD0,         // LFO
                                                       0x28, 0xB2, 0x67, 0x13, 0x06, 0x00, 0x00, 0x56,         // LFM
                                                       0x28, 0xB0, 0x08, 0x15, 0x07, 0x00, 0x00, 0xE4,         // RBM
                                                       0x28, 0x92, 0x5D, 0x15, 0x07, 0x00, 0x00, 0x7B,         // RBO
                                                       0x28, 0xB7, 0x08, 0x15, 0x07, 0x00, 0x00, 0x61,         // RFM
                                                       0x28, 0x93, 0x32, 0x15, 0x07, 0x00, 0x00, 0x32          // RFO
                                                      };
// ************* Object definitions ************* //
LiquidCrystal_I2C lcd(const_I2C_LCD_addr, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
OneWire           ds(const_OneWire_PIN);
PCA9685_Lite      pwm = PCA9685_Lite();
struct Settings {
  int           ID;                          // Just a number to recognise if settings are stored on EEPROM & program version
  byte          time_slots;                  // Programmed time slots
  byte          max_pct;                     // Max PWM
  byte          acclim_pct;                  // Current Acclimatisation PWM level
  byte          acclim_days;                 // Days left on Acclim
  byte          last_day_change;             // Last day of month the Acclim values changed
  byte          fan_on;                      // Fan on temp
  byte          fan_full;                    // Fan on full PWM temp
  byte          temp_max;                    // LEDs start fading off at this temp
};
struct LED_Timings {
  unsigned int  start_time;                  // Time slot starts - sort of in seconds
  byte          fade_mins;                   // Mins to fade from current to target
  unsigned int  level[const_LED_channels];   // Array of target PWMs for each string
};
// ************* Volatiles ************* //
volatile char          RE_btnState                         = ' ';
volatile char          RE_Dirn                             = ' ';
volatile unsigned long lastUIInt                           = 0;
// ************* Global variables ************* //
unsigned long          lastUITime                          = 0;
unsigned long          lastTempChange                      = 0;
String                 hhmm                                = " ";
unsigned int           timeOfDay                           = 0;
unsigned int           updateTimeOfDay                     = 0;
unsigned long          currentMillis                       = 0;
unsigned long          startFadeMillis                     = 0;
unsigned long          endFadeMillis                       = 0;
unsigned long          lastRTCMillis                       = 0;
int                    EEPROM_address                      = 0;
byte                   temp[const_temp_probes];
float                  highestReadTemp                     = 0;
byte                   dayChange                           = 'N';
char                   tempReadError                       = 'Y';
int                    currentLEDLevel[const_LED_channels];
int                    lastLEDLevel[const_LED_channels];
int                    targetLEDLevel[const_LED_channels];
byte                   fanPWM                              = 0;
float                  currentLEDPct;
float                  targetLEDPct;
byte                   UIPosn[3]                           = {0, 0, 1};
char                   UIStatus                            = ' ';
byte                   UICursorPosn[2]                     = {0, 0};
byte                   UITimeSlot                          = 0;
byte                   UIChannel                           = 0;
byte                   currentTimeSlot                     = 0;
// The below structures will be overwritten with settings from EEPROM if there are any
Settings               savedSettings;
LED_Timings            LT[const_time_slots];
// Copies of the structures to use whilst updating or manual PWM setting
Settings               updateSettings;
LED_Timings            updateLT;
// ************* Set-up ************* //
void setup() {
  wdt_disable();
  setHardware();                                                // Set output/input/pullup etc
  printStartup();                                               // Print a start up message to the LCD
  readSettings();                                               // Get the settings from EEPROM if they exist
  getTheTime();                                                 // Read the RTC and format the time
  readTemps();                                                  // Read the temps from all the temp probes
  catchUpLEDs();                                                // If the lights should already be on, fast ramp up to expected level
  displayHomeScreen();
  wdt_enable(WDTO_4S);                                          // Turn on the watchdog timer
  setInts();                                                    // Setting the interrupts after reading the EEPROM stops the read being disrupted
}
// ************* Main Loop ************* //
void loop() {
  wdt_reset();
  setLEDs();
  currentMillis = millis() - lastUITime;
  if ((RE_Dirn != ' ') || (RE_btnState == 'Y')) {               // If there is any input from the Rotary Encoder, go work out what to do with it
    processUI();
    lastUITime = millis();
  } else if (currentMillis > const_UILightDelay) {              // If it's been a while since the last UI, turn off the screen
    lcd.noBacklight();
    UIStatus  = 'O';
  } else if (currentMillis > const_UICursorDelay) {             // If it's been a (shorter) while since the last UI, turn off the cursor
    if (UIStatus == 'N') {
      displayHomeScreen();
    }
  }
  getTheTime();                                                 // Read the RTC and format the time
  if ((millis() - lastTempChange) > const_TempChangeDelay) {    // Only check for temp changes every few seconds
    lastTempChange = millis();
    readTemps();                                                // Read the temps from all the temp probes
  }
}
// ************* Subroutines and functions ************* //
void setHardware() {
  // Set up any hardware initialisations, PINs, interupts, etc
  //  Serial.begin(38400);
  Wire.begin();
  pinMode(const_RE_btn_PIN, INPUT_PULLUP);
  pinMode(const_RE_a_PIN, INPUT_PULLUP);
  pinMode(const_RE_b_PIN, INPUT_PULLUP);
  pinMode(const_SCW_pwr_PIN, OUTPUT);
  pinMode(const_FAN_PWM_PIN, OUTPUT);
  digitalWrite(const_SCW_pwr_PIN, LOW);
  digitalWrite(const_FAN_PWM_PIN, LOW);
  lcd.begin(20, 4);
  lcd.backlight();
  lcd.noCursor();
  lcd.clear();
  pwm.begin();
}
void setInts() {
  attachInterrupt(0, intReadBtn, CHANGE);
  attachInterrupt(1, intReadEncoder, RISING);
}
void readSettings() {
  EEPROM_address = 0;
  EEPROM.get(EEPROM_address, savedSettings);
  if (savedSettings.ID != (
        const_LED_channels * 100 + const_version_ID)
     ) {
    savedSettings.ID               = const_LED_channels * 100 + const_version_ID;
    savedSettings.time_slots       = 0;
    savedSettings.max_pct          = 100;
    savedSettings.acclim_pct       = 100;
    savedSettings.acclim_days      = 0;
    savedSettings.last_day_change  = 0;
    savedSettings.fan_on           = 30;
    savedSettings.fan_full         = 50;
    savedSettings.temp_max         = 60;
  } else {
    EEPROM_address = sizeof(Settings);
    for (byte i = 0; i < savedSettings.time_slots; i++) {
      EEPROM.get(EEPROM_address, LT[i]);
      EEPROM_address = EEPROM_address + sizeof(LED_Timings);
    }
  }
}
void saveSettings() {
  //  serialPrintSettings();
  EEPROM_address = 0;
  cli();                                                // Disable the interrupts briefly to prevent disrupting the EEPROM write
  EEPROM.put(EEPROM_address, savedSettings);
  EEPROM_address = sizeof(Settings);
  for (byte i = 0; i < savedSettings.time_slots; i++) {
    EEPROM.put(EEPROM_address, LT[i]);
    EEPROM_address = EEPROM_address + sizeof(LED_Timings);
  }
  sei();                                                // Re-enable the interrupts
}
void readTime(byte * hour, byte * minute, byte * dayOfWeek) {
  Wire.beginTransmission(const_I2C_DS3231_addr);
  Wire.write(0);                                        // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(const_I2C_DS3231_addr, int(4));      // request four bytes of data from DS3231 starting from register 00h
  Wire.read();                                          // Not bothered about seconds, so read and dump the value
  //  second  = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
  *dayOfWeek = bcdToDec(Wire.read());
}
void setTime(byte hour, byte minute) {
  Wire.beginTransmission(const_I2C_DS3231_addr);
  Wire.write(0);                                        // set DS3231 register pointer to 00h
  Wire.write(decToBcd(byte(0)));                        // set seconds
  Wire.write(decToBcd(minute));                         // set minutes
  Wire.write(decToBcd(hour));                           // set hours
  Wire.endTransmission();
  lastRTCMillis = 0;                                    // makes the program read the RTC next time thru
}
void getTheTime() {
  String old_hhmm;
  byte minute, hour, dayOfWeek;
  old_hhmm      = hhmm;
  currentMillis = millis();
  // Only actually poll the RTC if it's been 15s or is startup
  if ( ((currentMillis - lastRTCMillis) > 15000) || (lastRTCMillis == 0) ) {
    lastRTCMillis = currentMillis;
    readTime(&hour, &minute, &dayOfWeek);
    timeOfDay = (hour * 60) + minute;
    hhmm      = chk2Digit(hour) + ':' + chk2Digit(minute);
    if (hhmm != old_hhmm) {
      printTime();
    }
    if (dayOfWeek != savedSettings.last_day_change) {
      dayChange = 'Y';
      // Update both updateSettings (so the update is applied if the settings are saved) and savedSettings to stop repeated checks
      updateSettings.last_day_change = dayOfWeek;
      savedSettings.last_day_change  = dayOfWeek;
    }
  }
}
String chk2Digit(byte numb) {
  String txt = "";
  if (numb < 10 ) {
    txt = '0';
  }
  txt = txt + numb;
  return (txt);
}
String chk3Digit(byte numb) {
  String txt = "";
  if (numb < 10 ) {
    txt = "00";
  } else if (numb < 100) {
    txt = "0";
  }
  txt = txt + numb;
  return (txt);
}
String chk4Digit(int numb) {
  String txt = "";
  if (numb < 10 ) {
    txt = "000";
  } else if (numb < 100) {
    txt = "00";
  } else if (numb < 1000) {
    txt = "0";
  }
  txt = txt + numb;
  return (txt);
}
byte bcdToDec(byte val) {
  return ( (val / 16 * 10) + (val % 16) );
}
byte decToBcd(byte val) {
  return ( (val / 10 * 16) + (val % 10) );
}
void intReadBtn() {
  if (RE_btnState != 'Y') {
    if ((millis() - lastUIInt) > const_UIIntDelay) {
      lastUIInt = millis();
      if (digitalRead(const_RE_btn_PIN) == LOW) {
        RE_btnState = 'Y';
      }
    }
  }
}
void intReadEncoder() {
  if (RE_Dirn == ' ') {
    delay(3);
    if ((millis() - lastUIInt) > const_UIIntDelay) {
      lastUIInt = millis();
      if (digitalRead(const_RE_a_PIN) == digitalRead(const_RE_b_PIN)) {
        RE_Dirn = 'C';
      } else {
        RE_Dirn = 'A';
      }
    }
  }
}
void processUI() {
  if (UIStatus != 'O') {
    if (UIStatus == ' ') {
      RE_Dirn     = ' ';
      RE_btnState = ' ';
    }
    switch (UIPosn[0]) {
      case 0:
        UIHomeScreenNav();
        break;
      case 1:
        UILED();
        break;
      case 2:
        UITime();
        break;
      case 3:
        UITemp();
        break;
      case 4:
        UIFan();
        break;
      case 5:
        UIPct();
        break;
    }
  }
  RE_Dirn     = ' ';
  RE_btnState = ' ';
  lcd.backlight();
  if (UIStatus == ' ') {
    lcd.noBlink();
    lcd.noCursor();
  }
  if (UIStatus == ' ') {
    UIStatus = 'N';
  } else if (UIStatus == 'O') {
    UIStatus = ' ';
  }
  setUICursor();
  // Manual PWM levels (with examples?)
  // Manual critter spotting
}
void readTemps() {
  // Loop thru each temp probe and tell it to get the temp.
  // Loop thru again reading them in turn (should hopefully give time for the read without adding a delay, if not you'll get the last read value)
  // If any attempts to read error (85°C), set the error flag.
  byte  data[12];
  float readTemp = 0;
  float oldHighestReadTemp = highestReadTemp;
  char  oldTempReadError   = tempReadError;
  for (byte i = 0; i < const_temp_probes; i++) {
    ds.reset();
    ds.select(const_DS18B20_addr[i]);
    ds.write(0x44, 1);  // Tell probe to get the temp
  }
  highestReadTemp = 0;
  tempReadError = 'N';
  for (byte i = 0; i < const_temp_probes; i++) {
    ds.reset();
    ds.select(const_DS18B20_addr[i]);
    ds.write(0xBE);   // Read Scratchpad
    for (byte j = 0; j < 9; j++) {
      data[j] = ds.read();
    }
    readTemp = ( (data[1] << 8) + data[0] ) * 0.0625;  // Convert the returned data into a degrees celcius figure
    if (readTemp == 85) {
      tempReadError = 'Y';
    } else {
      if (readTemp > highestReadTemp) {
        highestReadTemp = readTemp;
      }
      temp[i] = readTemp;
    }
  }
  if (highestReadTemp != oldHighestReadTemp) {
    printTemp();
  }
  if (tempReadError != oldTempReadError) {
    printTempFlag();
  }
}
void catchUpLEDs() {
  unsigned int currentFadeEnd = 0;
  byte         prevTimeSlot;
  currentTimeSlot = savedSettings.time_slots;
  endFadeMillis = 0;
  findCurrentTimeSlot();
  setLEDs();
  currentFadeEnd = LT[currentTimeSlot].start_time + LT[currentTimeSlot].fade_mins;
  for (byte i = 0; i < const_LED_channels; i++) {
    lastLEDLevel[i] = currentLEDLevel[i];
    if ((timeOfDay <= LT[currentTimeSlot].start_time) ||
        (timeOfDay > currentFadeEnd)) {
      targetLEDLevel[i] = LT[currentTimeSlot].level[i];
    } else {
      if (currentTimeSlot == 0) {
        prevTimeSlot = savedSettings.time_slots - 1;
      } else {
        prevTimeSlot = currentTimeSlot - 1;
      }
      targetLEDLevel[i] = map(timeOfDay,
                              LT[currentTimeSlot].start_time, currentFadeEnd,
                              LT[currentTimeSlot].level[prevTimeSlot], LT[currentTimeSlot].level[i]
                             );
    }
  }
  startFadeMillis = millis();
  endFadeMillis = const_LEDFadePeriod;
}
void setLEDs() {
  float oldLEDPct = currentLEDPct;
  currentMillis = millis() - startFadeMillis;
  calcMaxLEDPct();
  setFanPWM();
  setTempFade();
  if (currentLEDPct != oldLEDPct) {
    printLEDPct();
  }
  calcLEDPWMs();
  if (currentMillis > endFadeMillis) {
    for (byte i = 0; i < const_LED_channels; i++) {
      lastLEDLevel[i] = currentLEDLevel[i];
    }
    findCurrentTimeSlot();
  }
}
void calcMaxLEDPct() {
  if (savedSettings.acclim_days > 0) {
    if (dayChange == 'Y') {                   // If the day has changed, check to see if the auto acclimatisation settings need updating
      dayChange = 'N';
      savedSettings.acclim_days = savedSettings.acclim_days - 1;
      if (savedSettings.acclim_days == 0) {
        savedSettings.acclim_pct = savedSettings.max_pct;
      } else {
        savedSettings.acclim_pct = (savedSettings.acclim_pct +
                                    ((savedSettings.max_pct - savedSettings.acclim_pct)
                                     / savedSettings.acclim_days)
                                   );
      }
      saveSettings();
    }
    targetLEDPct = savedSettings.acclim_pct;
  } else {
    targetLEDPct = savedSettings.max_pct;
  }
}
void findCurrentTimeSlot() {
  // Settings should already be read in. Search thru looking for next time.
  byte oldTimeSlot;
  oldTimeSlot = currentTimeSlot;
  if (savedSettings.time_slots == 0) {
    return;
  }
  if ((timeOfDay < LT[0].start_time) || (timeOfDay > LT[(savedSettings.time_slots - 1)].start_time)) {
    currentTimeSlot = savedSettings.time_slots - 1;
  } else {
    for (int i = (savedSettings.time_slots - 1); i >= 0; i--) {
      if (timeOfDay >= LT[i].start_time) {
        currentTimeSlot = i;
        i = -1; // this just forces the for loop to end
      }
    }
  }
  for (byte i = 0; i < const_LED_channels; i++) {
    targetLEDLevel[i] = LT[currentTimeSlot].level[i];
  }
  if ( (currentTimeSlot != oldTimeSlot) ||
       (endFadeMillis == const_LEDFadePeriod) ) {
    // Fade duration is minutes from settings multiplied by 60000 to convert minutes to milliseconds
    // If there is any difference (in minutes) between the set start time and the current time subtract that from
    // the fade duration as that means we must have powered on during a slot time and we'll already be part way there!
    if (timeOfDay < LT[currentTimeSlot].start_time) {
      endFadeMillis = 0;
    } else if (timeOfDay >= (LT[currentTimeSlot].start_time + LT[currentTimeSlot].fade_mins)) {
      // We should already be at the right levels! endFadeMillis doesn't matter!
    }
    else if (timeOfDay > LT[currentTimeSlot].start_time) {
      endFadeMillis = (LT[currentTimeSlot].fade_mins -
                       (timeOfDay - LT[currentTimeSlot].start_time) )
                      * 60000;
    }
    else if (timeOfDay = LT[currentTimeSlot].start_time) {
      endFadeMillis = LT[currentTimeSlot].fade_mins * 60000;
    }
    //    Serial.print("endFadeMillis=");
    //    Serial.println(endFadeMillis);
    startFadeMillis = millis();
    printSlot();
  }
}
void setTempFade() {
  // if the temp exceeds temp_max ramp the overriding PWM %age down to zero.
  if (highestReadTemp < savedSettings.temp_max) {
    currentLEDPct = targetLEDPct;
  } else if ((highestReadTemp >= savedSettings.temp_max)                       &&
             (highestReadTemp < (savedSettings.temp_max + const_TempFadeDown))
            ) {
    currentLEDPct = map(highestReadTemp,
                        savedSettings.temp_max, (savedSettings.temp_max + const_TempFadeDown),
                        targetLEDPct, 0
                       );
  } else if (highestReadTemp > (savedSettings.temp_max + const_TempFadeDown)) {
    currentLEDPct = 0;
  }
}
void setFanPWM() {
  // if the temp exceeds fan_on ramp the fan PWM from 0 to max at a temp of fan_full
  byte oldFanPWM = fanPWM;
  if ((highestReadTemp > 0) && (highestReadTemp < savedSettings.fan_on)) {
    // set fans off
    digitalWrite(const_SCW_pwr_PIN, LOW);
    digitalWrite(const_FAN_PWM_PIN, LOW);
    fanPWM = 0;
  } else if ((highestReadTemp > savedSettings.fan_on)    &&
             (highestReadTemp < savedSettings.fan_full)
            ) {
    // PWM proportional to the temp
    fanPWM = map(highestReadTemp,
                 savedSettings.fan_on, savedSettings.fan_full,
                 0, 255
                );
    digitalWrite(const_SCW_pwr_PIN, HIGH);
    analogWrite(const_FAN_PWM_PIN, fanPWM);
  } else {
    // All other results (high temp or read error) means fans on max
    fanPWM = 255;
    digitalWrite(const_SCW_pwr_PIN, HIGH);
    analogWrite(const_FAN_PWM_PIN, fanPWM);
  }
  if (fanPWM != oldFanPWM) {
    printFanPWM();
  }
}
void calcLEDPWMs() {
  String oldPWMs = " ";
  String newPWMs = " ";
  int newLEDLevel;
  currentMillis = millis() - startFadeMillis;
  for (byte i = 0; i < const_LED_channels; i++) {
    oldPWMs = oldPWMs + currentLEDLevel[i];
    if (currentMillis > endFadeMillis) {
      newLEDLevel = targetLEDLevel[i] * (currentLEDPct / 100);
    } else {
      //      newLEDLevel = map(currentMillis,
      //                        0, endFadeMillis,
      //                        lastLEDLevel[i], (
      //                          targetLEDLevel[i] * (currentLEDPct / 100) )
      //                       );
      newLEDLevel = (float(currentMillis) / endFadeMillis) *
                    (targetLEDLevel[i] * (float(currentLEDPct) / 100) -
                     lastLEDLevel[i]) +
                    lastLEDLevel[i];
    }
    if (currentLEDLevel[i] != newLEDLevel) {
      currentLEDLevel[i] = newLEDLevel;
      sendPWM(i);
    }
    newPWMs = newPWMs + currentLEDLevel[i];
  }
  //  if (newPWMs != oldPWMs) {
  //    Serial.print("Time=");
  //    Serial.print(timeOfDay);
  //    Serial.print(" Start time=");
  //    Serial.print(LT[currentTimeSlot].start_time);
  //    Serial.print(" current millis=");
  //    Serial.print(currentMillis);
  //    Serial.print(" fade end=");
  //    Serial.print(endFadeMillis);
  //    for (byte i = 0; i < const_LED_channels; i++) {
  //      Serial.print(" ");
  //      Serial.print(i);
  //      Serial.print("=");
  //      Serial.print(targetLEDLevel[i]);
  //      Serial.print(",");
  //      Serial.print(lastLEDLevel[i]);
  //      Serial.print(",");
  //      Serial.print(currentLEDLevel[i]);
  //      Serial.print("     ");
  //    }
  //    Serial.println(" ");
  //  }
}
void sendPWM(byte i) {
  pwm.setPWM(i, currentLEDLevel[i]);
}
void displayHomeScreen() {
  lcd.clear();
  lcd.noCursor();
  lcd.noBlink();
  UIPosn[0] = 0;
  UIPosn[1] = 0;
  UIPosn[2] = 0;
  UICursorPosn[0] = 5;
  UICursorPosn[1] = 0;
  UIStatus  = ' ';
  printSlot();
  printTemp();
  printFanPWM();
  printLEDPct();
  printTime();
}
void printStartup() {
  lcd.setCursor(0, 0);
  lcd.print(" Starting up!");
}
void printSlot() {
  lcd.setCursor(0, 0);
  lcd.print("Slot:");
  if (UIPosn[0] == 1) {
    lcd.print(chk2Digit(UITimeSlot));
  } else {
    lcd.print(chk2Digit(currentTimeSlot));
  }
  setUICursor();
}
void printTemp() {
  lcd.setCursor(0, 3);
  if (highestReadTemp == 0) {
    lcd.print("Er");
  } else {
    int x = highestReadTemp;
    lcd.print(x);
    lcd.print(char(223));
    lcd.print('C');
  }
  setUICursor();
}
void printFanPWM() {
  lcd.setCursor(9, 3);
  lcd.print(chk3Digit(fanPWM));
  setUICursor();
}
void printLEDPct() {
  int x = currentLEDPct;
  lcd.setCursor(16, 3);
  lcd.print(chk3Digit(x));
  lcd.print("%");
  setUICursor();
}
void printTime() {
  lcd.setCursor(15, 0);
  lcd.print(hhmm);
  setUICursor();
}
void printTempFlag() {
  lcd.setCursor(6, 3);
  if (tempReadError == 'Y') {
    lcd.print("E");
  } else {
    lcd.print(' ');
  }
  setUICursor();
}
void printSlotLevelsDisplay(byte startPosn) {
  byte x;
  printFullLine(" ", 1);
  printFullLine(" ", 2);
  for (byte i = 0;
       (
         (i <= (const_LED_channels - startPosn))
         && (i < 4)
       );
       i++) {
    x = i * 5;
    lcd.setCursor(x, 1);
    if (i <= const_LED_channels - startPosn - 1) {
      lcd.print(startPosn + i);
      lcd.setCursor(x, 2);
      if (UIStatus == 'A' || UIStatus == 'E') {
        lcd.print(chk4Digit(updateLT.level[(startPosn + i)]));
      } else {
        lcd.print(chk4Digit(LT[UITimeSlot].level[(startPosn + i)]));
      }
    } else {
      lcd.print("Done");
    }
  }
}
void printFullLine(String txt, byte line) {
  String spaces = "                    ";
  txt = txt + spaces.substring(txt.length());
  lcd.setCursor(0, line);
  lcd.print(txt);
  setUICursor();
}
void setUICursor() {
  lcd.setCursor(UICursorPosn[0], UICursorPosn[1]);
}
void UIHomeScreenNav() {
  byte noOfOptions = 5;
  byte cursorPosns[2][5] = {5, 15, 0, 9, 16,
                            0,  0, 3, 3,  3
                           };
  lcd.blink();
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    UIPosn[0] = UIPosn[1] + 1;
    switch (UIPosn[0]) {
      case 1:
        UITimeSlot = 0;
        UILED();
        break;
      case 2:
        UIStatus = 'Y';
        UIPosn[1] = 0;
        UIPosn[2] = 0;
        UICursorPosn[0] = 5;
        UICursorPosn[1] = 1;
        updateTimeOfDay = timeOfDay;
        displayUpdateTime();
        lcd.noBlink();
        lcd.cursor();
        UITime();
        break;
      case 3:
        UIPosn[1] = 0;
        UIPosn[2] = 0;
        updateSettings = savedSettings;
        printFullLine(("Max temp=" +
                       String(updateSettings.temp_max) +
                       char(223) +
                       'C'),
                      1);
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 2;
        printFullLine("Edit Exit", 2);
        UITemp();
        break;
      case 4:
        UIPosn[1] = 0;
        UIPosn[2] = 0;
        updateSettings = savedSettings;
        printFullLine(("On=" +
                       String(updateSettings.fan_on) +
                       char(223) +
                       "C Full=" +
                       String(updateSettings.fan_full) +
                       char(223) +
                       "C"),
                      1);
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 2;
        printFullLine("Edit Exit", 2);
        UIFan();
        break;
      case 5:
        UIPosn[1] = 0;
        UIPosn[2] = 0;
        updateSettings = savedSettings;
        printFullLine(("Mx=" + chk3Digit(updateSettings.max_pct) +
                       " Ac=" + chk3Digit(updateSettings.acclim_pct) +
                       " Dys=" + chk2Digit(updateSettings.acclim_days)),
                      1);
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 2;
        printFullLine("Edit Exit", 2);

        UIPct();
        break;
    }
  } else {
    UIPosn[1] = scroll(UIPosn[1], noOfOptions);
    UICursorPosn[0] = cursorPosns[0][UIPosn[1]];
    UICursorPosn[1] = cursorPosns[1][UIPosn[1]];
    UIStatus = 'N';
    displayHomeScreenLabel();
  }
}
void displayHomeScreenLabel() {
  switch (UIPosn[1]) {
    case 0:
      printFullLine("LED time slots", 1);
      break;
    case 1:
      printFullLine("Time", 1);
      break;
    case 2:
      printFullLine("Temp", 1);
      break;
    case 3:
      printFullLine("Fan speed", 1);
      break;
    case 4:
      printFullLine("Max percent", 1);
      break;
  }
}
void UILED() {
  switch (UIPosn[1]) {
    case 0:
      UILEDTimeDisplay();
      break;
    case 1:
      UILEDTimeOptScroll();
      break;
    case 2:
      UILEDChannelDisplay();
      break;
    case 3:
      UILEDChangeOptScroll();
      break;
    case 4:
      UILEDTimeEntry();
      break;
    case 5:
      UILEDChannelSelect();;
      break;
    case 6:
      UILEDLevelChange();
      break;
    case 7:
      UILEDChangeSave();
      break;
  }
}
void UILEDTimeDisplay() {
  byte hh;
  byte mm;
  lcd.blink();
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    printFullLine("View Edit Exit", 2);
    UIPosn[1] = 1;
    UILEDTimeOptScroll();
  } else {
    if (savedSettings.time_slots != 0) {
      UITimeSlot = scroll(UITimeSlot, savedSettings.time_slots);
    }
    printSlot();
    hh = LT[UITimeSlot].start_time / 60;
    mm = LT[UITimeSlot].start_time - (hh * 60);
    printFullLine(("Start=" + chk2Digit(hh) + ":" + chk2Digit(mm) + " Fade=" + chk3Digit(LT[UITimeSlot].fade_mins)), 1);
  }
}
void UILEDTimeOptScroll() {
  byte noOfOptions = 3;
  byte cursorPosns[3] = {0, 5, 10};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        UIPosn[1] = 2;
        UIPosn[2] = 0;
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 1;
        UILEDChannelDisplay();
        break;
      case 1:
        printFullLine("Add Del Edit Exit", 2);
        UIPosn[1] = 3;
        UIPosn[2] = 0;
        UILEDChangeOptScroll();
        break;
      case 2:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void UILEDChannelDisplay() {
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    printFullLine(" ", 2);
    UIPosn[1] = 0;
    UIPosn[2] = 0;
    UICursorPosn[0] = 5;
    UICursorPosn[1] = 0;
    UILEDTimeDisplay();
  } else {
    UIPosn[2] = scroll(UIPosn[2], const_LED_channels);
    printSlotLevelsDisplay(UIPosn[2]);
  }
}
void UILEDChangeOptScroll() {
  byte noOfOptions = 4;
  byte cursorPosns[4] = {0, 4, 8, 13};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        if (savedSettings.time_slots == const_time_slots) {
          printFullLine("Already at max!!", 1);
        } else {
          UIStatus = 'A';
          updateLT = LT[UITimeSlot];
          updateSettings = savedSettings;
          UITimeSlot = updateSettings.time_slots;
          printSlot();
          UIPosn[1] = 4;
          UIPosn[2] = 0;
          lcd.noBlink();
          lcd.cursor();
          printFullLine(" ", 2);
          UICursorPosn[0] = 6;
          UICursorPosn[1] = 1;
          UILEDTimeEntry();
        }
        break;
      case 1:
        if (savedSettings.time_slots == 0) {
          printFullLine("None to delete!!", 1);
        } else {
          UIStatus = 'D';
          updateSettings = savedSettings;
          printFullLine("Save Exit", 2);
          UIPosn[1] = 7;
          UIPosn[2] = 0;
          lcd.blink();
          lcd.noCursor();
          UILEDChangeSave();
        }
        break;
      case 2:
        if (savedSettings.time_slots == 0) {
          printFullLine("None to edit!!", 1);
        } else {
          UIStatus = 'E';
          updateLT = LT[UITimeSlot];
          updateSettings = savedSettings;
          printSlot();
          UIPosn[1] = 4;
          UIPosn[2] = 0;
          lcd.noBlink();
          lcd.cursor();
          printFullLine(" ", 2);
          UICursorPosn[0] = 6;
          UICursorPosn[1] = 1;
          UILEDTimeEntry();
        }
        break;
      case 3:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void UILEDTimeEntry() {
  byte cursorPosns[7] = {6, 7, 9, 10, 17, 18, 19};
  byte hh;
  byte mm;
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
      case 1:
      case 2:
      case 3:
        if (updateLT.start_time > 1439) {
          updateLT.start_time = 1439;
        }
      case 4:
      case 5:
        UIPosn[2] = UIPosn[2] + 1;
        UICursorPosn[0] = cursorPosns[UIPosn[2]];
        UICursorPosn[1] = 1;
        break;
      case 6:
        UIPosn[1] = 5;
        UIPosn[2] = 0;
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 1;
        UILEDChannelSelect();
        break;
    }
  } else {
    switch (UIPosn[2]) {
      case 0:
        updateLT.start_time = changeTime(updateLT.start_time, 600);
        break;
      case 1:
        updateLT.start_time = changeTime(updateLT.start_time, 60);
        break;
      case 2:
        updateLT.start_time = changeTime(updateLT.start_time, 10);
        break;
      case 3:
        updateLT.start_time = changeTime(updateLT.start_time, 1);
        break;
      case 4:
        changeLEDFadeTime(100);
        break;
      case 5:
        changeLEDFadeTime(10);
        break;
      case 6:
        changeLEDFadeTime(1);
        break;
    }
    hh = updateLT.start_time / 60;
    mm = updateLT.start_time - (hh * 60);
    printFullLine(("Start=" + chk2Digit(hh) + ":" + chk2Digit(mm) + " Fade=" + chk3Digit(updateLT.fade_mins)), 1);
  }
}
unsigned int changeTime(unsigned int updateTime, unsigned int amount) {
  if (RE_Dirn == 'C') {
    if (1799 > (updateTime + amount)) {
      updateTime = updateTime + amount;
    }
  } else if (RE_Dirn == 'A') {
    if (updateTime >= amount) {
      updateTime = updateTime - amount;
    }
  }
  return updateTime;
}
void changeLEDFadeTime(unsigned int amount) {
  if (RE_Dirn == 'C') {
    if (999 > (updateLT.fade_mins + amount)) {
      updateLT.fade_mins = updateLT.fade_mins + amount;
    }
  } else if (RE_Dirn == 'A') {
    if (updateLT.fade_mins >= amount) {
      updateLT.fade_mins = updateLT.fade_mins - amount;
    }
  }
}
void UILEDChannelSelect() {
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    if (UIPosn[2] < const_LED_channels) {
      UIChannel = UIPosn[2];
      UIPosn[1] = 6;
      UIPosn[2] = 0;
      lcd.noBlink();
      lcd.cursor();
      UICursorPosn[0] = 0;
      UICursorPosn[1] = 2;
      UILEDLevelChange();
    } else {
      printFullLine("Save Exit", 2);
      UIPosn[1] = 7;
      UIPosn[2] = 0;
      lcd.blink();
      lcd.noCursor();
      UILEDChangeSave();
    }
  } else {
    lcd.blink();
    lcd.noCursor();
    UICursorPosn[0] = 0;
    UIPosn[2] = scroll(UIPosn[2], (const_LED_channels + 1));
    if (UIPosn[2] < const_LED_channels) {
      UICursorPosn[1] = 1;
      printSlotLevelsDisplay(UIPosn[2]);
    } else {
      UICursorPosn[0] = 5;
    }
  }
}
void UILEDLevelChange() {
  byte cursorPosns[4] = {0, 1, 2, 3};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
      case 1:
      case 2:
        if (updateLT.level[UIChannel] > 4095) {
          updateLT.level[UIChannel] = 4095;
        }
        UIPosn[2] = UIPosn[2] + 1;
        UICursorPosn[0] = cursorPosns[UIPosn[2]];
        UICursorPosn[1] = 2;
        break;
      case 3:
        if (updateLT.level[UIChannel] > 4095) {
          updateLT.level[UIChannel] = 4095;
        }
        lcd.blink();
        lcd.noCursor();
        UIPosn[1] = 5;
        UIPosn[2] = UIChannel;
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 1;
        UILEDChannelSelect();
        break;
    }
  } else {
    switch (UIPosn[2]) {
      case 0:
        changeLEDlevel(1000);
        break;
      case 1:
        changeLEDlevel(100);
        break;
      case 2:
        changeLEDlevel(10);
        break;
      case 3:
        changeLEDlevel(1);
        break;
    }
    printSlotLevelsDisplay(UIChannel);
  }
}
void changeLEDlevel(unsigned int amount) {
  if (RE_Dirn == 'C') {
    if (5000 > (updateLT.level[UIChannel] + amount)) {
      updateLT.level[UIChannel] = updateLT.level[UIChannel] + amount;
    }
  } else if (RE_Dirn == 'A') {
    if (updateLT.level[UIChannel] >= amount) {
      updateLT.level[UIChannel] = updateLT.level[UIChannel] - amount;
    }
  }
}
void UILEDChangeSave() {
  byte noOfOptions = 2;
  byte cursorPosns[3] = {0, 5};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        switch (UIStatus) {
          case 'A':
            addSlot();
            break;
          case 'D':
            deleteSlot();
            break;
          case 'E':
            if (LT[UITimeSlot].start_time == updateLT.start_time) {
              LT[UITimeSlot] = updateLT;
            } else {
              //              Serial.println("Before update");
              //              serialPrintSettings();
              deleteSlot();
              addSlot();
            }
            break;
        }
        savedSettings = updateSettings;
        saveSettings();
        //        Serial.println("After update");
        //        serialPrintSettings();
        findCurrentTimeSlot();
        UIStatus  = 'N';
        UIPosn[1] = 0;
        UIPosn[2] = 0;
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 1;
        printFullLine(" ", 2);
        UILEDTimeDisplay();
        break;
      case 1:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void addSlot() {
  byte greaterThan = 'N';
  UITimeSlot = 0;
  if (updateSettings.time_slots > 0) {
    for (int i = (updateSettings.time_slots - 1); i >= 0; i--) {
      if (updateLT.start_time > LT[i].start_time) {
        greaterThan = 'Y';
        UITimeSlot = i;
        i = 0;
      }
    }
    if (greaterThan == 'Y') {
      UITimeSlot = UITimeSlot + 1;
    }
    for (byte i = UITimeSlot; i < updateSettings.time_slots; i++) {
      LT[i + 1] = LT[i];
    }
  }
  LT[UITimeSlot] = updateLT;
  updateSettings.time_slots = updateSettings.time_slots + 1;
}
void deleteSlot() {
  //  Serial.println("Before deleting");
  //  serialPrintSettings();
  for (byte i = UITimeSlot; i < (updateSettings.time_slots - 1); i++) {
    LT[i] = LT[i + 1];
  }
  updateSettings.time_slots = updateSettings.time_slots - 1;
  if (UITimeSlot > 0) {
    UITimeSlot = UITimeSlot - 1;
  }
  updateSettings.time_slots = updateSettings.time_slots - 1;
  //  Serial.println("After deleting");
  //  serialPrintSettings();
}
byte scroll(byte current_val, byte max_val) {
  if (RE_Dirn == 'C') {
    if (current_val == (max_val - 1)) {
      current_val = 0;
    } else {
      current_val = current_val + 1;
    }
  } else if (RE_Dirn == 'A') {
    if (current_val == 0) {
      current_val = max_val - 1;
    } else {
      current_val = current_val - 1;
    }
  }
  return (current_val);
}
void UITime() {
  switch (UIPosn[1]) {
    case 0:
      UITimeEntry();
      break;
    case 1:
      UITimeOptScroll();
      break;
  }
}
void UITimeEntry() {
  byte cursorPosns[4] = {5, 6, 8, 9};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
      case 1:
      case 2:
        UIPosn[2]       = UIPosn[2] + 1;
        UICursorPosn[0] = cursorPosns[UIPosn[2]];
        UICursorPosn[1] = 1;
        break;
      case 3:
        if (updateTimeOfDay > 1439) {
          updateTimeOfDay = 1439;
        }
        UIPosn[1] = 1;
        UIPosn[2] = 0;
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 2;
        lcd.blink();
        lcd.noCursor();
        printFullLine("Save Exit", 2);
        UITimeOptScroll();
        break;
    }
  } else {
    switch (UIPosn[2]) {
      case 0:
        updateTimeOfDay = changeTime(updateTimeOfDay, 600);
        break;
      case 1:
        updateTimeOfDay = changeTime(updateTimeOfDay, 60);
        break;
      case 2:
        updateTimeOfDay = changeTime(updateTimeOfDay, 10);
        break;
      case 3:
        updateTimeOfDay = changeTime(updateTimeOfDay, 1);
        break;
    }
    displayUpdateTime();
  }
}
void displayUpdateTime() {
  byte hh;
  byte mm;
  hh = updateTimeOfDay / 60;
  mm = updateTimeOfDay - (hh * 60);
  printFullLine(("Time=" + chk2Digit(hh) + ":" + chk2Digit(mm)), 1);
}
void UITimeOptScroll() {
  byte noOfOptions = 2;
  byte cursorPosns[2] = {0, 5};
  byte hh;
  byte mm;
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        hh = updateTimeOfDay / 60;
        mm = updateTimeOfDay - (hh * 60);
        setTime(hh, mm);
      case 1:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void UITemp() {
  switch (UIPosn[1]) {
    case 0:
      UITempEditExit();
      break;
    case 1:
      UITempEntry();
      break;
    case 2:
      UISaveOptScroll();
      break;
  }
}
void UITempEditExit() {
  byte noOfOptions = 2;
  byte cursorPosns[2] = {0, 5};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        lcd.noBlink();
        lcd.cursor();
        UIPosn[1] = 1;
        UICursorPosn[0] = 9;
        UICursorPosn[1] = 1;
        UIStatus = 'Y';
        printFullLine(" ", 2);
        UITempEntry();
        break;
      case 1:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void UITempEntry() {
  //  String txt;
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    UIPosn[1] = 2;
    UIPosn[2] = 0;
    UICursorPosn[0] = 0;
    UICursorPosn[1] = 2;
    lcd.blink();
    lcd.noCursor();
    printFullLine("Save Exit", 2);
    UISaveOptScroll();
  } else {
    if (RE_Dirn == 'C') {
      if (updateSettings.temp_max < 100) {
        updateSettings.temp_max = updateSettings.temp_max + 1;
      }
    } else if (RE_Dirn == 'A') {
      if (updateSettings.temp_max > 0) {
        updateSettings.temp_max = updateSettings.temp_max - 1;
      }
    }
    //    txt = "Max temp=" + updateSettings.temp_max + char(223) + 'C';
    printFullLine(("Max temp=" +
                   String(updateSettings.temp_max) +
                   char(223) +
                   "C"),
                  1);
  }
}
void UISaveOptScroll() {
  byte noOfOptions = 2;
  byte cursorPosns[2] = {0, 5};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        savedSettings = updateSettings;
        saveSettings();
      case 1:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void UIFan() {
  switch (UIPosn[1]) {
    case 0:
      UIFanEditExit();
      break;
    case 1:
      UIFanEntry();
      break;
    case 2:
      UISaveOptScroll();
      break;
  }
}
void UIFanEditExit() {
  byte noOfOptions = 2;
  byte cursorPosns[2] = {0, 5};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        lcd.noBlink();
        lcd.cursor();
        UIPosn[1] = 1;
        UICursorPosn[0] = 3;
        UICursorPosn[1] = 1;
        UIStatus = 'Y';
        printFullLine(" ", 2);
        UIFanEntry();
        break;
      case 1:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void UIFanEntry() {
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    if (UIPosn[2] == 0) {
      UIPosn[2] = 1;
      UICursorPosn[0] = 13;
    } else {
      UIPosn[1] = 2;
      UIPosn[2] = 0;
      UICursorPosn[0] = 0;
      UICursorPosn[1] = 2;
      lcd.blink();
      lcd.noCursor();
      printFullLine("Save Exit", 2);
      UISaveOptScroll();
    }
  } else {
    switch (UIPosn[2]) {
      case 0:
        if (RE_Dirn == 'C') {
          if (updateSettings.fan_on < 100) {
            updateSettings.fan_on = updateSettings.fan_on + 1;
          }
        } else if (RE_Dirn == 'A') {
          if (updateSettings.fan_on > 10) {
            updateSettings.fan_on = updateSettings.fan_on - 1;
          }
        }
        break;
      case 1:
        if (RE_Dirn == 'C') {
          if (updateSettings.fan_full < 100) {
            updateSettings.fan_full = updateSettings.fan_full + 1;
          }
        } else if (RE_Dirn == 'A') {
          if (updateSettings.fan_full > 10) {
            updateSettings.fan_full = updateSettings.fan_full - 1;
          }
        }
        break;
    }
    printFullLine(("On=" +
                   String(updateSettings.fan_on) +
                   char(223) +
                   "C Full=" +
                   String(updateSettings.fan_full) +
                   char(223) +
                   "C"),
                  1);
  }
}
void UIPct() {
  switch (UIPosn[1]) {
    case 0:
      UIPctEditExit();
      break;
    case 1:
      UIPctEntry();
      break;
    case 2:
      UISaveOptScroll();
      break;
  }
}
void UIPctEditExit() {
  byte noOfOptions = 2;
  byte cursorPosns[2] = {0, 5};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
        lcd.noBlink();
        lcd.cursor();
        UIPosn[1] = 1;
        UICursorPosn[0] = 3;
        UICursorPosn[1] = 1;
        UIStatus = 'Y';
        printFullLine(" ", 2);
        UIPctEntry();
        break;
      case 1:
        displayHomeScreen();
        break;
    }
  } else {
    UIPosn[2] = scroll(UIPosn[2], noOfOptions);
    UICursorPosn[0] = cursorPosns[UIPosn[2]];
    UICursorPosn[1] = 2;
  }
}
void UIPctEntry() {
  byte cursorPosns[8] = {3, 4, 5, 10, 11, 12, 18, 19};
  if (RE_btnState == 'Y') {
    RE_btnState = ' ';
    switch (UIPosn[2]) {
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
        UIPosn[2] = UIPosn[2] + 1;
        UICursorPosn[0] = cursorPosns[UIPosn[2]];
        break;
      case 7:
        UIPosn[1] = 2;
        UIPosn[2] = 0;
        UICursorPosn[0] = 0;
        UICursorPosn[1] = 2;
        lcd.blink();
        lcd.noCursor();
        printFullLine("Save Exit", 2);
        UISaveOptScroll();
        break;
    }
  } else {
    switch (UIPosn[2]) {
      case 0:
        updateSettings.max_pct = UIPctEntryChg(updateSettings.max_pct, 100, 100);
        break;
      case 1:
        updateSettings.max_pct = UIPctEntryChg(updateSettings.max_pct, 10, 100);
        break;
      case 2:
        updateSettings.max_pct = UIPctEntryChg(updateSettings.max_pct, 1, 100);
        break;
      case 3:
        updateSettings.acclim_pct = UIPctEntryChg(updateSettings.acclim_pct, 100, 100);
        break;
      case 4:
        updateSettings.acclim_pct = UIPctEntryChg(updateSettings.acclim_pct, 10, 100);
        break;
      case 5:
        updateSettings.acclim_pct = UIPctEntryChg(updateSettings.acclim_pct, 1, 100);
        break;
      case 6:
        updateSettings.acclim_days = UIPctEntryChg(updateSettings.acclim_days, 10, 99);
        break;
      case 7:
        updateSettings.acclim_days = UIPctEntryChg(updateSettings.acclim_days, 1, 99);
        break;
    }
    printFullLine(("Mx=" + chk3Digit(updateSettings.max_pct) +
                   " Ac=" + chk3Digit(updateSettings.acclim_pct) +
                   " Dys=" + chk2Digit(updateSettings.acclim_days)),
                  1);
  }
}
byte UIPctEntryChg(byte value, unsigned int change, byte maxVal) {
  if (RE_Dirn == 'C') {
    if ((value + change) > maxVal) {
      value = maxVal;
    } else {
      value = value + change;
    }
  } else if (RE_Dirn == 'A') {
    if (value >= change) {
      value = value - change;
    }
  }
  return (value);
}
//void serialPrintSettings() {
//  Serial.print("ID=");
//  Serial.print(savedSettings.ID);
//  Serial.print(" time_slots=");
//  Serial.print(savedSettings.time_slots);
//  Serial.print(" max_pct=");
//  Serial.print(savedSettings.max_pct);
//  Serial.print(" acclim_pct=");
//  Serial.print(savedSettings.acclim_pct);
//  Serial.print(" acclim_days=");
//  Serial.print(savedSettings.acclim_days);
//  Serial.print(" last_day_change=");
//  Serial.print(savedSettings.last_day_change);
//  Serial.print(" fan_on=");
//  Serial.print(savedSettings.fan_on);
//  Serial.print(" fan_full=");
//  Serial.print(savedSettings.fan_full);
//  Serial.print(" temp_max=");
//  Serial.println(savedSettings.temp_max);
//  for (byte i = 0; i < savedSettings.time_slots; i++) {
//    Serial.print("Start time ");
//    Serial.print(i);
//    Serial.print(":");
//    Serial.println(LT[i].start_time);
//    for (byte j = 0; j < const_LED_channels; j++) {
//      Serial.print("   Channel ");
//      Serial.print(j);
//      Serial.print(":");
//      Serial.println(LT[i].level[j]);
//    }
//  }
//}



// ************* It's all over!! ************* //
