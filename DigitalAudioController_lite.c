/*
* TDA7313 Controller & UI
* James Barnett
*
* TODO:
* Save settings to EEPROM on power down or after period of inactivity
* Overtemp warning/shutdown
*
* I. Kosyachenko
* Lite version for Arduino Uno and LCD 1604
*/
#include <Encoder.h>
#include <LiquidCrystal.h>  // Лобавляем необходимую библиотеку
#include <Wire.h>

// Encoder button
#define BTN 8A //AVR 15

// interface
#define SELECTION_MARKER '>'
#define VOLUME 0
#define TREB 1
#define BASS 2
#define VOLUME_MAX 0
#define VOLUME_MIN -75 // flipped since we are attenuating
#define TREB_MAX 14
#define TREB_MIN -14
#define BASS_MAX 14
#define BASS_MIN -14

LiquidCrystal lcd(7, 6, 5, 4, 3, 2); // (RS, E, DB4, DB5, DB6, DB7)

Encoder enc(10,11);
long oldPosition = -999;
long newPosition = 0;
boolean encUpdated = true;

byte graphBlock[8] = {
  B00011111,
  B00111111,
  B01011111,
  B01111111,
  B10011111,
  B10111111,
  B11011111,
  B11100000
};

// Volume and tone
int cursorSelection;

double volPosition = -40; // value in db
byte volByte = 32; // 00100000 default vol to -40db
boolean volUpdateQueued = false;

int trebPosition = 0;
byte trebByte = 127; // 01111111 default treb to 0db
boolean trebUpdateQueued = false;

int bassPosition = 0;
byte bassByte = 111; // 0110111 default bass to 0db;
boolean bassUpdateQueued = false;

boolean volChanged = false;
boolean trebChanged = false;
boolean bassChanged = false;

// Button
boolean buttonState = false;
boolean prevButtonState = false;
boolean buttonDown = false;
boolean buttonUp = false;
boolean selectionUpdated = false;

void buttonCycled()
{
  //button cycled
  if(cursorSelection == 2)
  {
    cursorSelection = 0;
  }
  else
  {
    cursorSelection++;
  }
  buttonDown = false;
  buttonUp = false;
  selectionUpdated = true;
}

void renderEncoderChange()
{
  lcd.setCursor(13,3);

  if(cursorSelection == VOLUME)
  {
    if(volPosition < -9) // 6 chars
    {
      lcd.setCursor(12,3);
    }
    else if(volPosition == 0) // 4 chars
    {
      lcd.setCursor(12,3);
      lcd.print("  "); // clear sign any old digit
      lcd.setCursor(14,3);
    }
    else // 5 chars
    {
      lcd.setCursor(12,3);
      lcd.print(" "); // clear sign any old digit
      lcd.setCursor(13,3);
    }
    lcd.print(volPosition);
  }

  else if(cursorSelection == TREB)
  {
    if(trebPosition > 9) // two digits
    {
      lcd.setCursor(15,3);
      lcd.print(" "); // clear old sign
      lcd.setCursor(16,3);
    }
    else if(trebPosition >= 0) // one digit
    {
      lcd.setCursor(15,3);
      lcd.print("  ");
      lcd.setCursor(17,3);
    }
    else if(trebPosition < -9)// sign + two digits
    {
      lcd.setCursor(15,3);
    }
    else // sign + one digit
    {
      lcd.setCursor(15,3);
      lcd.print(" ");
      lcd.setCursor(16,3);
    }
    lcd.print(trebPosition, DEC);
  }

  else if(cursorSelection == BASS)
  {
    if(bassPosition > 9) // two digits
    {
      lcd.setCursor(15,3);
      lcd.print(" "); // clear old sign
      lcd.setCursor(16,3);
    }
    else if(bassPosition >= 0) // one digit
    {
      lcd.setCursor(15,3);
      lcd.print("  ");
      lcd.setCursor(17,3);
    }
    else if(bassPosition < -9)// sign + two digits
    {
      lcd.setCursor(15,3);
    }
    else // sign + one digit
    {
      lcd.setCursor(15,3);
      lcd.print(" ");
      lcd.setCursor(16,3);
    }
    lcd.print(bassPosition, DEC);
  }

  if(volChanged)
  {
    lcd.setCursor(5,0);
    renderVolumeGraph(volPosition);
    volChanged = false;
  }

  else if(trebChanged)
  {
    lcd.setCursor(5,1);
    renderToneGraph(trebPosition);
    trebChanged = false;
  }

  else if(bassChanged)
  {
    lcd.setCursor(5,2);
    renderToneGraph(bassPosition);
    bassChanged = false;
  }
}

void renderSelectionChange()
{
  // clear existing cursors
  lcd.setCursor(3,0);
  lcd.print(' ');
  lcd.setCursor(3,1);
  lcd.print(' ');
  lcd.setCursor(3,2);
  lcd.print(' ');
  // print updated location
  lcd.setCursor(3,cursorSelection);
  lcd.print(SELECTION_MARKER);
  // clear old encoder value
  lcd.setCursor(10,3);
  lcd.print("        ");
  // encoder value will also have changed to its relevant vol/treb/bass value
  renderEncoderChange();
}


void sendByte(byte data)
{
  Wire.beginTransmission(0x44); // TDA7313 7bit addr 01000100
  Wire.write(data);
  Wire.endTransmission();
}

void tdaInit()
{
  Wire.beginTransmission(0x44); // 01000100
  Wire.write(0x45); // input 2, 11.25db gain, loud mode off
  Wire.write(0x6F); // bass flat
  Wire.write(0x7F); // treb flat
  Wire.write(0x9F); // mute lf
  Wire.write(0xBF); // mute rf
  Wire.write(0xC0); // 0db attn RL
  Wire.write(0xE0); // 0db attn RR
  Wire.write(0x16); // vol atten to -40db
  Wire.endTransmission();
}

void encoderInc()
{
  if(cursorSelection == VOLUME)
  {
    if(volPosition < VOLUME_MAX)
    {
      volPosition += 1.25; // 1.25db resolution
      volByte -= 1; // reduce vol atten by 1.25db
      volChanged = true;
      volUpdateQueued = true;
    }
  }
  // increment
  else if(cursorSelection == TREB)
  {
    if(trebPosition < TREB_MAX)
    {
      if(trebPosition > 0) // we are already boosting, so increace boost
      {
        trebByte -= 1; // confusingly we decrement to increace boost
      }
      else if(trebPosition < 0) // reduce attenuation
      {
        trebByte += 1; // increment to reduce
      }
      else // flat and incrementing, so want atten bit c3(5) = 1
      {
        trebByte = 126; // 01111110
      }
      trebPosition += 2; // 2db resolution
      trebChanged = true;
      trebUpdateQueued = true;
    }
  }
  // increment
  else if(cursorSelection == BASS)
  {
    if(bassPosition < BASS_MAX)
    {
      if(bassPosition > 0) // we are already boosting, so increce boost
      {
        bassByte -= 1; // confusingly we decrement to increace boost
      }
      else if(bassPosition < 0) // reduce attenuation
      {
        bassByte += 1; // increment to reduce
      }
      else
      {
        bassByte = 110; // 01111110
      }
      bassPosition += 2; // 2db resolution
      bassChanged = true;
      bassUpdateQueued = true;
    }
  }
}

void encoderDec()
{
  if(cursorSelection == VOLUME)
  {
    if(volPosition > VOLUME_MIN)
    {
      volPosition -= 1.25;
      volByte += 1; //increace atten by 1.25db;
      volChanged = true;
      volUpdateQueued = true;
    }
  }
  // decrement
  else if(cursorSelection == TREB)
  {
    if(trebPosition > TREB_MIN)
    {
      if(trebPosition > 0) // reduce the boost
      {
        trebByte += 1; // confusingly we still increment. eg 14db to 12db is 01111000 -> 01111001
      }
      else if(trebPosition < 0) // we are already attenuating, just decrement
      {
        trebByte -= 1;
      }
      else // flat and decrementing, so want atten bit c3(5) = 0
      {
        trebByte = 118; // 01110110 = -2db;
      }
      trebPosition -= 2; // 2db resolution
      trebChanged = true;
      trebUpdateQueued = true;
    }
  }
  else if(cursorSelection == BASS)
  {
    if(bassPosition > BASS_MIN)
    {
      if(bassPosition > 0) // reduce the boost
      {
        bassByte += 1; // confusingly we still increment. eg 14db to 12db is 01101000 -> 01101001
      }
      else if(bassPosition < 0) // we are already attenuating, just decrement
      {
        bassByte -= 1;
      }
      else // flat and decrementing, so want atten bit c3(5) = 0
      {
        bassByte = 102; // 01100110 = -2db;
      }
      bassPosition -= 2; // 2db resolution
      bassChanged = true;
      bassUpdateQueued = true;
    }
  }
}

void setup()
{
  // I2C for TDA7313
  Wire.begin();
  tdaInit();

  lcd.begin(16,2);
  //lcd.print("Loading...");
  lcd.createChar(0, graphBlock);
  lcd.setCursor(0,0);
  //delay(2000);

  lcd.print("VOL>|");
  lcd.write((byte) 0);
  lcd.write((byte) 0);
  lcd.print("            |"); // print default volume graph for -40db
  lcd.setCursor(0,1);
  lcd.print("TRB |     FLAT     |");
  lcd.setCursor(0,2);
  lcd.print("BAS |     FLAT     |");
  lcd.setCursor(18,3);
  lcd.print("dB");

  volChanged = true;
  renderEncoderChange(); // show initial volume

}

void loop()
{
  // encoder
  newPosition = enc.read()/4; // 4 pulses per notch
  if(newPosition > oldPosition)
  {
    encoderInc();
    oldPosition = newPosition;
    encUpdated = true;
  }
  else if (newPosition < oldPosition)
  {
    encoderDec();
    oldPosition = newPosition;
    encUpdated = true;
  }
  else
  {
    encUpdated = false;
  }


  // button
  buttonState = digitalRead(BTN);
  if(buttonState != prevButtonState)
  {
    if(buttonState == HIGH)
    {
      buttonDown = true;
    }
    else
    {
      buttonUp = true;
    }
  }
  // if we have completed a full button cycle
  if(buttonDown == true && buttonUp == true)
  {
    buttonCycled();
  }
  else
  {
    selectionUpdated = false;
  }
  prevButtonState = buttonState;

  // update TDA7313
  // only one update should ever be sent per loop iteration
  if(volUpdateQueued)
  {
    sendByte(volByte);
    volUpdateQueued = false;
  }
  else if(trebUpdateQueued)
  {
    sendByte(trebByte);
    trebUpdateQueued = false;
  }
  else if(bassUpdateQueued)
  {
    sendByte(bassByte);
    bassUpdateQueued = false;
  }

  if(encUpdated)
  {
    renderEncoderChange();
  }
  if(selectionUpdated)
  {
    renderSelectionChange();
  }

}

// using a mapping function (like the volume graph) is much nicer, but this is probably faster
void renderToneGraph(int position)
{
  switch (position)
  {
    case -14:
      lcd.write((byte) 0);
      lcd.print("             ");
      break;
    case -12:
      lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("            -");
      break;
    case -10:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("           ");
      break;
    case -8:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("          ");
      break;
    case -6:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("         ");
      break;
    case -4:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("        ");
      break;
    case -2:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("       ");
      break;
    case 0:
      lcd.print("     FLAT     ");
      break;
    case 2:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("      ");
      break;
    case 4:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("     ");
      break;
    case 6:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("    ");
      break;
    case 8:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("   ");
      break;
    case 10:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print("  ");
      break;
    case 12:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      lcd.print(" ");
      break;
    case 14:
      lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);lcd.write((byte) 0);
      break;
  }
}

int mapVolumeValues(int val)
{
  return ((abs(val)-50)/(50.0))*(-14.0);
}

void renderVolumeGraph(int position)
{
  if(position > -50)
  {
    int segments = mapVolumeValues(position);
    lcd.setCursor(5,0);
    for(int i=0; i< segments; i++)
    {
      lcd.write((byte) 0);
    }
    int blankSpace = 14-segments;
    for(int j=0; j< blankSpace; j++)
    {
      lcd.print(" ");
    }
  }
}
