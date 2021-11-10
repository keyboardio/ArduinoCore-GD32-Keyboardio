#include "KeyboardioHID.h"

void setup()
{
				//Serial.begin(115200);
				Serial.println("-- Blink demo --");

				pinMode(LED2, OUTPUT);

				Keyboard.begin();
}

int f = 0;

void loop()
{
				Serial.println("On");
				digitalWrite(LED2, HIGH);
				delay(500);

				Keyboard.press(HID_KEYBOARD_A_AND_A);
				f++;
				Keyboard.sendReport();
				f++;
				Keyboard.releaseAll();
				f++;
				Keyboard.sendReport();
				f++;

				Serial.println("Off");
				digitalWrite(LED2, LOW);
				delay(500);
}
