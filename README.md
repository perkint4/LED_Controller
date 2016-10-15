# LED_Controller
Simple arduino based LED controller to automatically fade LEDs on and off at set times

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
  Libraries:
    LEDBloke's PCA9685_Lite
    FM NewLiquidCrystal v1.3.4 (https://bitbucket.org/fmalpartida/new-liquidcrystal/wiki/Home)
    Dallas OneWire v2.3.2 (http://playground.arduino.cc/Learning/OneWire)
    Wire (part of Arduino IDE)
    EEPROM (part of Arduino IDE)
  IDE:
    Arduino IDE v1.6.8
*/

Current state - loaded to my lights 15/10/16. Using Optiboot 32pin ATMega328P (ie Arduino Nano v3.0 with Optiboot loader)
Sketch uses 24,208 bytes (75%) of program storage space. Maximum is 32,256 bytes.
Global variables use 1,058 bytes (51%) of dynamic memory, leaving 990 bytes for local variables. Maximum is 2,048 bytes.
