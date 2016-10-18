#include <EEPROM.h>
#include <Wire.h>

const int  const_version_ID                  = 01;
const int  const_LED_channels                = 10;
const byte const_time_slots                  = 8;
struct Saved_Settings {
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
  int           level[const_LED_channels];   // Array of target PWMs for each string
};
Saved_Settings         savedSettings;
LED_Timings            LT[const_time_slots];
int                    EEPROM_address        = 0;

void setup() {
  Serial.begin(38400);
  Wire.begin();

  savedSettings.ID               = const_LED_channels * 100 + const_version_ID;
  savedSettings.time_slots       = const_time_slots;
  savedSettings.max_pct          = 50;
  savedSettings.acclim_pct       = 50;
  savedSettings.acclim_days      = 0;
  savedSettings.last_day_change  = 0;
  savedSettings.fan_on           = 30;
  savedSettings.fan_full         = 50;
  savedSettings.temp_max         = 75;
  LT[0].start_time = timeOfDay(12, 00);
  LT[0].fade_mins  = 10;
  LT[0].level[0] = 1000;
  LT[0].level[1] = 10;
  LT[0].level[2] = 10;
  LT[0].level[3] = 10;
  LT[0].level[4] = 400;
  LT[0].level[5] = 1000;
  LT[0].level[6] = 10;
  LT[0].level[7] = 10;
  LT[0].level[8] = 10;
  LT[0].level[9] = 400;
  LT[1].start_time = timeOfDay(13, 00);
  LT[1].fade_mins  = 30;
  LT[1].level[0] = 2000;
  LT[1].level[1] = 4000;
  LT[1].level[2] = 4000;
  LT[1].level[3] = 4000;
  LT[1].level[4] = 1000;
  LT[1].level[5] = 2000;
  LT[1].level[6] = 4000;
  LT[1].level[7] = 4000;
  LT[1].level[8] = 4000;
  LT[1].level[9] = 1000;
  LT[2].start_time = timeOfDay(16, 00);
  LT[2].fade_mins  = 30;
  LT[2].level[0] = 2000;
  LT[2].level[1] = 3000;
  LT[2].level[2] = 4000;
  LT[2].level[3] = 4000;
  LT[2].level[4] = 1000;
  LT[2].level[5] = 2000;
  LT[2].level[6] = 3000;
  LT[2].level[7] = 4000;
  LT[2].level[8] = 4000;
  LT[2].level[9] = 1000;
  LT[3].start_time = timeOfDay(19, 00);
  LT[3].fade_mins  = 30;
  LT[3].level[0] = 2000;
  LT[3].level[1] = 4095;
  LT[3].level[2] = 4095;
  LT[3].level[3] = 4095;
  LT[3].level[4] = 1000;
  LT[3].level[5] = 2000;
  LT[3].level[6] = 4095;
  LT[3].level[7] = 4095;
  LT[3].level[8] = 4095;
  LT[3].level[9] = 1000;
  LT[4].start_time = timeOfDay(21, 00);
  LT[4].fade_mins  = 30;
  LT[4].level[0] = 0;
  LT[4].level[1] = 1000;
  LT[4].level[2] = 1000;
  LT[4].level[3] = 1000;
  LT[4].level[4] = 0;
  LT[4].level[5] = 0;
  LT[4].level[6] = 1000;
  LT[4].level[7] = 1000;
  LT[4].level[8] = 1000;
  LT[4].level[9] = 0;
  LT[5].start_time = timeOfDay(22, 00);
  LT[5].fade_mins  = 10;
  LT[5].level[0] = 0;
  LT[5].level[1] = 0;
  LT[5].level[2] = 0;
  LT[5].level[3] = 0;
  LT[5].level[4] = 0;
  LT[5].level[5] = 0;
  LT[5].level[6] = 0;
  LT[5].level[7] = 0;
  LT[5].level[8] = 0;
  LT[5].level[9] = 0;
  LT[6].start_time = timeOfDay(22, 30);
  LT[6].fade_mins  = 5;
  LT[6].level[0] = 0;
  LT[6].level[1] = 0;
  LT[6].level[2] = 0;
  LT[6].level[3] = 0;
  LT[6].level[4] = 1000;
  LT[6].level[5] = 0;
  LT[6].level[6] = 0;
  LT[6].level[7] = 0;
  LT[6].level[8] = 0;
  LT[6].level[9] = 1000;
  LT[7].start_time = timeOfDay(23, 00);
  LT[7].fade_mins  = 5;
  LT[7].level[0] = 0;
  LT[7].level[1] = 0;
  LT[7].level[2] = 0;
  LT[7].level[3] = 0;
  LT[7].level[4] = 0;
  LT[7].level[5] = 0;
  LT[7].level[6] = 0;
  LT[7].level[7] = 0;
  LT[7].level[8] = 0;
  LT[7].level[9] = 0;

  saveSettings();

  savedSettings.ID = 0;
  savedSettings.time_slots = 0;
  savedSettings.max_pct = 0;
  savedSettings.acclim_pct = 0;
  savedSettings.acclim_days = 0;
  savedSettings.last_day_change = 1;
  savedSettings.fan_on = 0;
  savedSettings.fan_full = 0;
  savedSettings.temp_max = 0;

  for (byte i = 0; i < const_time_slots; i++) {
    LT[i].start_time = 0;
    LT[i].fade_mins = 0;
    for (byte j = 0; j < const_LED_channels; j++) {
      LT[i].level[j] = 0;
    }
  }

  readSettings();
}

void loop() {
  delay(5000);
  getTheTime();                        // can also set time & day of week
  Serial.print("Settings:");
  Serial.print(" Slots:");
  Serial.print(savedSettings.time_slots);
  Serial.print(" Max pct:");
  Serial.print(savedSettings.max_pct);
  Serial.print(" Acclim pct:");
  Serial.print(savedSettings.acclim_pct);
  Serial.print(" Acclim days:");
  Serial.print(savedSettings.acclim_days);
  Serial.print(" Fan on:");
  Serial.print(savedSettings.fan_on);
  Serial.print(" Fan full:");
  Serial.print(savedSettings.fan_full);
  Serial.print(" Max temp:");
  Serial.println(savedSettings.temp_max);
  Serial.println(" Slots:");

  for (byte i = 0; i < const_time_slots; i++) {
    Serial.print(" Start time ");
    Serial.print(i);
    Serial.print("=");
    Serial.print(hhmm(LT[i].start_time));
    Serial.print(" Fade:");
    Serial.print(LT[i].fade_mins);
    Serial.print(" PWMs");
    for (byte j = 0; j < const_LED_channels; j++) {
      Serial.print(" ");
      Serial.print(j);
      Serial.print(":");
      Serial.print(LT[i].level[j]);
    }
    Serial.println(" ");
  }
  Serial.println(" ");

}

void readSettings() {
  EEPROM_address = 0;
  cli();
  EEPROM.get(EEPROM_address, savedSettings);
  sei();
  if (savedSettings.ID != (const_LED_channels * 100 + const_version_ID)) {
    savedSettings.ID               = const_LED_channels * 100 + const_version_ID;
    savedSettings.time_slots       = 0;
    savedSettings.max_pct          = 100;
    savedSettings.acclim_pct       = 100;
    savedSettings.acclim_days      = 0;
    savedSettings.last_day_change  = 0;
    savedSettings.fan_on           = 30;
    savedSettings.temp_max         = 60;
  } else {
    EEPROM_address = sizeof(Saved_Settings);
    for (byte i = 0; i < savedSettings.time_slots; i++) {
      cli();
      EEPROM.get(EEPROM_address, LT[i]);
      sei();
      EEPROM_address = EEPROM_address + sizeof(LED_Timings);
    }
  }
}
void saveSettings() {
  EEPROM_address = 0;
  cli();
  EEPROM.put(EEPROM_address, savedSettings);
  sei();
  EEPROM_address = sizeof(Saved_Settings);
  for (byte i = 0; i < savedSettings.time_slots; i++) {
    cli();
    EEPROM.put(EEPROM_address, LT[i]);
    sei();
    EEPROM_address = EEPROM_address + sizeof(LED_Timings);
  }
}
void setDS3231time(byte second,
                   byte minute,
                   byte hour,
                   byte dayOfWeek,
                   byte dayOfMonth,
                   byte month,
                   byte year)
{
  // sets time and date data to DS3231
  Wire.beginTransmission(0x68);
  Wire.write(0); // set next input to start at the seconds register
  Wire.write(decToBcd(second)); // set seconds
  Wire.write(decToBcd(minute)); // set minutes
  Wire.write(decToBcd(hour)); // set hours
  Wire.write(decToBcd(dayOfWeek)); // set day of week (1=Sunday, 7=Saturday)
  Wire.write(decToBcd(dayOfMonth)); // set date (1 to 31)
  Wire.write(decToBcd(month)); // set month
  Wire.write(decToBcd(year)); // set year (0 to 99)
  Wire.endTransmission();
}
void readDS3231time(byte *second,
                    byte *minute,
                    byte *hour,
                    byte *dayOfWeek,
                    byte *dayOfMonth,
                    byte *month,
                    byte *year)
{
  Wire.beginTransmission(0x68);
  Wire.write(0); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(0x68, 7);
  // request seven bytes of data from DS3231 starting from register 00h
  *second = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
  *dayOfWeek = bcdToDec(Wire.read());
  *dayOfMonth = bcdToDec(Wire.read());
  *month = bcdToDec(Wire.read());
  *year = bcdToDec(Wire.read());
}
void getTheTime() {
  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
  readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month,
                 &year);
  dayOfWeek = 4;
  //  If you want to set the time, replace the below with what you want to set eg the below line would set it to 23:59:59 on 31/12/16
  //  setDS3231time(59, 59, 23, 7, 31, 21, 16)

  //  setDS3231time(second, minute, hour, dayOfWeek, dayOfMonth, month,
  //                year);
  Serial.print(hour, DEC);
  Serial.print(":");
  if (minute < 10)
  {
    Serial.print("0");
  }
  Serial.print(minute, DEC);
  Serial.print(":");
  if (second < 10)
  {
    Serial.print("0");
  }
  Serial.print(second, DEC);
  Serial.print(" ");
  Serial.print(dayOfMonth, DEC);
  Serial.print("/");
  Serial.print(month, DEC);
  Serial.print("/");
  Serial.print(year, DEC);
  Serial.print(" Day of week: ");
  Serial.print(" dayOfWeek = ");
  Serial.println(dayOfWeek);
}
byte bcdToDec(byte val) {
  return ( (val / 16 * 10) + (val % 16) );
}
byte decToBcd(byte val) {
  return ( (val / 10 * 16) + (val % 10) );
}
unsigned int timeOfDay(byte hh, byte mm) {
  return ((hh * 60) + mm);
}
String hhmm(unsigned int TOD) {
  byte hh;
  byte mm;
  String displayTime;
  hh = TOD / 60;
  mm = TOD - (hh * 60);
  if (mm < 10) {
    displayTime = String(hh) + ":0" + String(mm);
  }
  else {
    displayTime = String(hh) + ":" + String(mm);
  }
  return (displayTime);
}


