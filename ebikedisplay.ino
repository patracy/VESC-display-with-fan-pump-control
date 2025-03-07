#include <VescUart.h>
#include <buffer.h>
#include <crc.h>
#include <datatypes.h>

#include <LiquidCrystal_I2C.h>
#include <VescUart.h>

//GPS Setup
const unsigned char UBX_HEADER[] = { 0xB5, 0x62 };
const char UBLOX_INIT[] PROGMEM = {
  0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x24, // GxGGA off
  0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x01,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x2B, // GxGLL off
  0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x02,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x32, // GxGSA off
  0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x03,0x00,0x00,0x00,0x00,0x00,0x01,0x03,0x39, // GxGSV off
  0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x04,0x00,0x00,0x00,0x00,0x00,0x01,0x04,0x40, // GxRMC off
  0xB5,0x62,0x06,0x01,0x08,0x00,0xF0,0x05,0x00,0x00,0x00,0x00,0x00,0x01,0x05,0x47, // GxVTG off
  0xB5,0x62,0x06,0x01,0x08,0x00,0x01,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x17,0xDC, //NAV-PVT off
  0xB5,0x62,0x06,0x01,0x08,0x00,0x01,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x12,0xB9, //NAV-POSLLH off
  0xB5,0x62,0x06,0x01,0x08,0x00,0x01,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x13,0xC0, //NAV-STATUS off
  0xB5,0x62,0x06,0x01,0x08,0x00,0x01,0x07,0x00,0x01,0x00,0x00,0x00,0x00,0x18,0xE1, //NAV-PVT on
  0xB5,0x62,0x06,0x08,0x06,0x00,0x64,0x00,0x01,0x00,0x01,0x00,0x7A,0x12, //(10Hz)
};
struct NAV_PVT {
  unsigned char cls;
  unsigned char id;
  unsigned short len;
  unsigned long iTOW;          // GPS time of week of the navigation epoch (ms)
  unsigned short year;         // Year (UTC) 
  unsigned char month;         // Month, range 1..12 (UTC)
  unsigned char day;           // Day of month, range 1..31 (UTC)
  unsigned char hour;          // Hour of day, range 0..23 (UTC)
  unsigned char minute;        // Minute of hour, range 0..59 (UTC)
  unsigned char second;        // Seconds of minute, range 0..60 (UTC)
  char valid;                  // Validity Flags (see graphic below)
  unsigned long tAcc;          // Time accuracy estimate (UTC) (ns)
  long nano;                   // Fraction of second, range -1e9 .. 1e9 (UTC) (ns)
  unsigned char fixType;       // GNSSfix Type, range 0..5
  char flags;                  // Fix Status Flags
  unsigned char reserved1;     // reserved
  unsigned char numSV;         // Number of satellites used in Nav Solution
  long lon;                    // Longitude (deg)
  long lat;                    // Latitude (deg)
  long height;                 // Height above Ellipsoid (mm)
  long hMSL;                   // Height above mean sea level (mm)
  unsigned long hAcc;          // Horizontal Accuracy Estimate (mm)
  unsigned long vAcc;          // Vertical Accuracy Estimate (mm)
  long velN;                   // NED north velocity (mm/s)
  long velE;                   // NED east velocity (mm/s)
  long velD;                   // NED down velocity (mm/s)
  long gSpeed;                 // Ground Speed (2-D) (mm/s)
  long heading;                // Heading of motion 2-D (deg)
  unsigned long sAcc;          // Speed Accuracy Estimate
  unsigned long headingAcc;    // Heading Accuracy Estimate
  unsigned short pDOP;         // Position dilution of precision
  short reserved2;             // Reserved
  unsigned long reserved3;     // Reserved
};
NAV_PVT pvt;
void calcChecksum(unsigned char* CK) {
  memset(CK, 0, 2);
  for (int i = 0; i < (int)sizeof(NAV_PVT); i++) {
    CK[0] += ((unsigned char*)(&pvt))[i];
    CK[1] += CK[0];
  }
}
bool processGPS() {
  static int fpos = 0;
  static unsigned char checksum[2];
  const int payloadSize = sizeof(NAV_PVT);
  while ( Serial2.available() ) {
    byte c = Serial2.read();
    if ( fpos < 2 ) {
      if ( c == UBX_HEADER[fpos] )
        fpos++;
      else
        fpos = 0;
    }
    else {      
      if ( (fpos-2) < payloadSize )
        ((unsigned char*)(&pvt))[fpos-2] = c;

      fpos++;

      if ( fpos == (payloadSize+2) ) {
        calcChecksum(checksum);
      }
      else if ( fpos == (payloadSize+3) ) {
        if ( c != checksum[0] )
          fpos = 0;
      }
      else if ( fpos == (payloadSize+4) ) {
        fpos = 0;
        if ( c == checksum[1] ) {
          return true;
        }
      }
      else if ( fpos > (payloadSize+4) ) {
        fpos = 0;
      }
    }
  }
  return false;
}

LiquidCrystal_I2C lcd(0x27, 16, 2);
VescUart UART;
// Fan Control Setup
#define FAN_PIN 8  // Pin connected to fan control (e.g., via relay or MOSFET)
#define TEMP_THRESHOLD 40.0  // Temperature threshold (in Celsius) for turning on fan
//variables
float maxvolt = 0;
float maxspeed = 0;
float maxamp = 0;
float maxw = 0;
float power = 0;
int count = 0;
int intpin = 2;
int screenclear = 0;
unsigned long previousMillis = 0;
float totalDistance = 0;
const unsigned long DEBOUNCE_DELAY = 300; 
volatile unsigned long lastInterruptTime = 0;
// Variables to store motor and MOSFET temperature
float motorTemp = 0.0;
float mosfetTemp = 0.0;

void setup() 
{
  //Hardware Serial Setup
  Serial2.begin(9600, SERIAL_8N1, D10, D9);
  Serial1.begin(115200, SERIAL_8N1, D12, D11);
  UART.setSerialPort(&Serial1);
  delay(500);
  //LCD setup
  lcd.init();
  lcd.backlight();

  //Interrupt setup
  pinMode(intpin, INPUT);
  attachInterrupt(intpin, blink, RISING);

  //Send configuration to GPS module
  for(int i = 0; i < sizeof(UBLOX_INIT); i++) 
  {                        
   Serial2.write( pgm_read_byte(UBLOX_INIT+i) );
   delay(5); // simulating a 38400baud pace (or less), otherwise commands are not accepted by the device.
  }
  // Initialize fan pin as output
  pinMode(FAN_PIN, OUTPUT);
}

void loop() 
{
  if (screenclear == 0)
    {
      lcd.clear();
      screenclear = 1;
    }

  if ( processGPS() ) 
  {
     //access infomation from GPS
  float speed = ((pvt.gSpeed/1000.0f)*2.237);
  unsigned long currentMillis = millis();
  unsigned long elapsedTime = currentMillis - previousMillis;
  float distanceTraveled = speed * (elapsedTime / 3600000.0);
  totalDistance += distanceTraveled;
  previousMillis = currentMillis;

  if (maxspeed < speed)
    {
      maxspeed = speed;
    }
  if (count == 0)
    {
    lcd.setCursor(0,1);
    lcd.print("MPH");
    lcd.print(speed,0);
    lcd.print(" ");
    }
  if (count == 1)
    {
    lcd.setCursor(0,1);
    lcd.print("Max Speed: ");
    lcd.print(maxspeed);
    }
  if (count == 2)
    {
    lcd.setCursor(0,1);
    lcd.print("Distance:");
    lcd.print(totalDistance, 1);
    }
  }

  if (UART.getVescValues()) 
  {
    //access infomation from VESC
    float voltage = UART.data.inpVoltage;
    float current = UART.data.avgInputCurrent;
    float motortemp = UART.data.tempMotor;
    float fettemp = UART.data.tempMosfet;

    // Fan control logic
    controlFan(motortemp);  // Control fan based on motor temperature
    controlFan(fettemp);  // Control fan based on motor temperature

    power = current * voltage;

    if (maxvolt < voltage)
    {
      maxvolt = voltage;
    }
    if (maxamp < current)
    {
      maxamp = voltage;
    }
    if (maxw < power)
    {
      maxw = power;
    }
    if (count == 0)
      {
      lcd.setCursor(0, 0);
      lcd.print("V:");
      lcd.print(voltage, 1);
      lcd.setCursor(8, 0);
      lcd.print("A:");
      lcd.print(" ");
      lcd.print(current, 1);
      lcd.setCursor(8, 1);
      lcd.print("W:");
      lcd.print(power, 0);
      lcd.print("  ");
      if (power < 1000)
      {
        lcd.print(" ");
      }
      }
    if (count == 1)
      {
      lcd.setCursor(0, 0);
      lcd.print("MaxW:");
      lcd.print(maxw, 0);
      }
    if (count == 2)
      {
      lcd.setCursor(0, 0);
      lcd.print("MT");
      //lcd.print(motortemp, 1);
      lcd.print(((motortemp * 9) + 3) / 5 + 32, 1);
      lcd.setCursor(8, 0);
      lcd.print("ESCT");
      //lcd.print(fettemp, 1);
      lcd.print(((fettemp* 9) + 3) / 5 + 32, 1);
      }
  } 
  delay(100);
}
//Interrupt to change screen
void blink() 
{
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > DEBOUNCE_DELAY) 
  {
    screenclear = 0;
    count = count + 1;
    if (count == 3)
    {
      count = 0;
    }
    lastInterruptTime = interruptTime;
  }
}
// Function to control fan based on temperature
void controlFan(float temperature) {
  if (temperature > TEMP_THRESHOLD) {
    digitalWrite(FAN_PIN, HIGH);  // Turn the fan on if temperature exceeds threshold
    Serial.println("Fan ON");
  } else {
    digitalWrite(FAN_PIN, LOW);   // Turn the fan off if temperature is below threshold
    Serial.println("Fan OFF");
  }
}