/***
 * @author("Raymond Blum" <raymond@insanegiantrobots.com>)
 *
 * Copyright (c) 2011,2017 by Raymond Blum
 * This program code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * A  hardware reference implementation and further information can be found
 * at http://www.insanegiantrobots.com/nixie
 * TODO(raymond) verify the refresh timing params are set from the serial data.
 *
 */


#define DEBUG false
#define TRACE false


// My Bluesmirf silver module has a default baud rate of 9600 and the USB connection on a Teensy
// uses 38400 by default
//
#define SERIAL_SPEED 38400
#define BT_SERIAL_SPEED 9600


/** Our serial protocol message contains 38 bytes, any non-alphanumeric is a reset
# val(4)
# spot1(1)
# spot2(1)
# tone(4)
# tone_dur(7)
# tone_interval(7)
# refresh_cycle_interval(7)
# refresh_cycle_dur(7)
# ##########
*/


char DEFAULT_DISPLAY_VALUE[4] = {'1','2','3','4'};


// Nixie tubes get poisoned if you don't light each digit periodically
#define DEFAULT_REFRESH_CYCLE_INTERVAL 120000  // wait 2m between refresh cycles
#define DEFAULT_REFRESH_CYCLE_STEP_DURATION 600  // Show each digit refresh value for 0.6s
#define REFRESH_CYCLE_START_DIGIT 9
#define REFRESH_CYCLE_END_DIGIT 0
unsigned long int when_refresh_next_event;
unsigned long int when_refresh_cycle_step_end;
int current_refresh_digit_value;
boolean in_refresh_cycle;
unsigned long int refresh_cycle_interval;
unsigned long int refresh_cycle_step_duration;

// Pins 7 and 8 are a hardware UART on a Teensy 2.0
#define PIN_TX 7
#define PIN_RX 8

// Pin 11 is connected to the on-board LED, we resuse it
#define PIN_LED 11
#define PIN_SPEAKER 2
#define PIN_SPOT1 21
#define PIN_SPOT2 20

#define PIN_0_A 15
#define PIN_0_B 13
#define PIN_0_C 12
#define PIN_0_D 14

#define PIN_1_A 10
#define PIN_1_B 1
#define PIN_1_C 0
#define PIN_1_D 9

#define PIN_2_A 16
#define PIN_2_B 18
#define PIN_2_C 19
#define PIN_2_D 17

#define PIN_3_A 6
#define PIN_3_B 4
#define PIN_3_C 3
#define PIN_3_D 5

#define PIN_0_PUNC 11
#define PIN_1_PUNC 22
#define PIN_2_PUNC 23
#define PIN_3_PUNC 24

#define DIGIT_INTERVAL 1000

#define SPOT_INTERVAL 500
#define SPOT_DURATION 500

#define QUIET_TONE_FREQUENCY 0
#define DEFAULT_TONE_FREQUENCY 225
#define DEFAULT_TONE_DURATION 250      //0.25s
#define DEFAULT_TONE_INTERVAL 1800000  //30m


// The Teensy 2.0 hardware Uart is connected to the Bluesmirf modem
HardwareSerial btSerial = HardwareSerial();

unsigned long int when_digit;
unsigned long int when_spot;
unsigned long int when_spot_stop;
unsigned long int when_tone;


boolean led_state;
boolean spot_toggle;


int counter;
int digits[4][4] = {{PIN_0_A,PIN_0_B,PIN_0_C,PIN_0_D},{PIN_1_A,PIN_1_B,PIN_1_C,PIN_1_D},{PIN_2_A,PIN_2_B,PIN_2_C,PIN_2_D},{PIN_3_A,PIN_3_B,PIN_3_C,PIN_3_D}};


struct display_spec {
  char value[4];
  char spot_1;
  char spot_2;
  int tone_frequency;
  unsigned long int tone_duration_ms;
  unsigned long int tone_interval_ms;  
};


struct display_spec *value_display_spec;
struct display_spec *refresh_display_spec;


int bytes_received_count = 0;
byte incoming_bytes[38];
int buffer_pos = 0;


void setupPins() {
  if (DEBUG) {
  Serial.println("Setting up pins");
  }
  for (int digit = 0; digit < 4; digit++) {
    for (int pin=0; pin<4; pin++) {
      pinMode(digits[digit][pin], OUTPUT);
      digitalWrite(digits[digit][pin], LOW);
      if (DEBUG) {
        Serial.print(digits[digit][pin]);
        Serial.print("\t");
      }
    }
    if (DEBUG) {
      Serial.println();
    }
  }
  pinMode(PIN_SPOT1, OUTPUT);
  pinMode(PIN_SPOT2, OUTPUT);
  digitalWrite(PIN_SPOT1, LOW);
  digitalWrite(PIN_SPOT2, LOW);
}


void setDefaultDisplayValues() {
  value_display_spec = (struct display_spec*)malloc(sizeof(struct display_spec));
  memcpy(value_display_spec->value,DEFAULT_DISPLAY_VALUE,4);
  value_display_spec->spot_1 = 'T';
  value_display_spec->spot_2 = 'T';
  value_display_spec->tone_frequency = DEFAULT_TONE_FREQUENCY;
  value_display_spec->tone_interval_ms = DEFAULT_TONE_INTERVAL;
  value_display_spec->tone_duration_ms = DEFAULT_TONE_DURATION;  
  current_refresh_digit_value = REFRESH_CYCLE_END_DIGIT;
}

void setInitialEventTimes() {
  when_digit = millis() + DIGIT_INTERVAL;
  when_spot = millis() + SPOT_INTERVAL;
  when_spot_stop = millis() + SPOT_DURATION;
  when_tone = millis();
  refresh_cycle_step_duration = DEFAULT_REFRESH_CYCLE_STEP_DURATION;
  refresh_cycle_interval = DEFAULT_REFRESH_CYCLE_INTERVAL;
  when_refresh_next_event  = millis();
  when_refresh_cycle_step_end = millis() - 1;
}

void setup() {
  Serial.begin(SERIAL_SPEED);
  btSerial.begin(BT_SERIAL_SPEED);
  setupPins();
  counter = 0;
  setInitialEventTimes();
  setDefaultDisplayValues();
  in_refresh_cycle = false;
  pinMode(PIN_LED, OUTPUT);
  led_state = false;
}

void showPin(int digit, int pin, boolean state) {
  digitalWrite(digits[digit][pin], state?HIGH:LOW);
  if (DEBUG) {
    if (state)
      Serial.print(digits[digit][pin]);
    else
      Serial.print("-");  
      
    Serial.print(" ");
  }
}

void showDigit(int digit, int value, boolean on_state) {
  int pin0 = value & 1;
  int pin1 = value & 2;
  int pin2 = value & 4;
  int pin3 = value & 8;
  showPin(digit, 0, pin0 * on_state);
  showPin(digit, 1, pin1 * on_state);
  showPin(digit, 2, pin2 * on_state);
  showPin(digit, 3, pin3 * on_state);
  if (DEBUG) {
    Serial.println();
  }
}

void blankDigit(int digit) {
  for (int pin=0;pin < 4;pin++)
    digitalWrite(digits[digit][pin], HIGH);
}

int DigitValue(const char value) {
  if (value >= '0' and value <= '9') {
    return value - '0';
  } else {
    return 10;
  }
}

void displayDigit(int digit, int value) {
  blankAllBut(digit);
  showDigit(digit, value, true);
}

void blankAllBut(int exempt) {
  for (int digit = 0; digit < 4; digit++) {
    if (exempt != digit) {
      blankDigit(digit);
    }
  }
}

int DigitPosition(int value, int digit_position) {
  int pos[4];
  pos[3] = value / 1000;
  pos[2] = (value / 100 * 100 - (pos[3]*1000))/100;
  pos[1] = (value / 10 * 10 - (pos[2]*100 + pos[3]*1000))/10;
  pos[0] = value - (pos[1]*10 + pos[2]*100 + pos[3]*1000);
  return pos[digit_position];
}

/** Are we in a refresh cycle step?
 */
boolean inRefreshCycleStep() {
  return (millis() >= when_refresh_next_event && millis() < when_refresh_cycle_step_end);
}

struct display_spec* makeRefreshDisplay(char values[]) {
  struct display_spec *refresh_display;
  refresh_display = (struct display_spec*)malloc(sizeof(struct display_spec));
  
  refresh_display->spot_1 = 'T';
  refresh_display->spot_2 = 'T';
  refresh_display->tone_frequency = QUIET_TONE_FREQUENCY;
  refresh_display->tone_interval_ms = DEFAULT_TONE_INTERVAL;
  refresh_display->tone_duration_ms = DEFAULT_TONE_DURATION;
  memcpy(refresh_display->value,values,4);
  return refresh_display;
}

/**
Return a new refresh display spec if we have reached the next refresh display event else return null
maintain the next refresh event and end of current step
 */
struct display_spec* getNextRefreshDisplay() {
  if (millis() > when_refresh_next_event && millis() > when_refresh_cycle_step_end) {
    if (TRACE || DEBUG) { Serial.println("Next refresh event reached at: "); Serial.println(millis());}

    if (current_refresh_digit_value == REFRESH_CYCLE_END_DIGIT) {  // We're at the end of a cycle
      current_refresh_digit_value = REFRESH_CYCLE_START_DIGIT;
      when_refresh_next_event = millis() + refresh_cycle_interval;
    } else {
      current_refresh_digit_value += (REFRESH_CYCLE_START_DIGIT > REFRESH_CYCLE_END_DIGIT)?-1:+1;  // Counting up or down?
      when_refresh_next_event = millis();
    }
    if (TRACE || DEBUG) { Serial.print("next refresh digit: "); Serial.println(current_refresh_digit_value); }
    if (TRACE || DEBUG) { Serial.println("Next refresh event scheduled for: "); Serial.println(when_refresh_next_event);}
    
    char refresh_values[4];
    for (int i=0;i<=3;i++) {
      refresh_values[i]='0' + current_refresh_digit_value;   // HERE
    }
    when_refresh_cycle_step_end = when_refresh_next_event + refresh_cycle_step_duration;
    
    return makeRefreshDisplay(refresh_values);
  } else {
    return 0l;
  } 
}

struct display_spec* getNewValueDisplay() {  
 struct display_spec *new_display = 0l;
 char numeric_value[8];
 
   if (btSerial.available()) {
    incoming_bytes[bytes_received_count++] = btSerial.read();
    if (DEBUG || TRACE) {
      Serial.print("()");
      Serial.print(incoming_bytes[bytes_received_count-1]);
      Serial.print(")");
    }
    if (! ((incoming_bytes[bytes_received_count-1] >= '0' && incoming_bytes[bytes_received_count-1] <= '9') ||
           (incoming_bytes[bytes_received_count-1] >= 'A' && incoming_bytes[bytes_received_count-1] <= 'Z'))) {
             bytes_received_count=0;
           }
   }
   if (bytes_received_count < 38) {
     return 0l;
   }
   bytes_received_count = 0;
   buffer_pos = 0;
   
   new_display = (struct display_spec*)malloc(sizeof(struct display_spec));
   // read 4 bytes as our new numeric display value
   for (int i=0;i<4;i++) {
     numeric_value[i] =incoming_bytes[buffer_pos++];
   }
   numeric_value[4]='\0';
   memcpy(new_display->value,numeric_value,4);
    
   // take 2 bytes as our spotlight indicator settings
   new_display->spot_1=incoming_bytes[buffer_pos++];
   new_display->spot_2=incoming_bytes[buffer_pos++];
    
   // read 4 bytes as our new tone frequency value
   for (int i=0;i<4;i++) {
     numeric_value[i] = incoming_bytes[buffer_pos++];
   }
   numeric_value[4]='\0';
   new_display->tone_frequency = atoi(numeric_value);

   // read 7 bytes as our new tone duration value
   for (int i=0;i<7;i++) {
     numeric_value[i] = incoming_bytes[buffer_pos++];
   }
   numeric_value[7]='\0';
   new_display->tone_duration_ms = atol(numeric_value);

   // read 7 bytes as our new tone interval value
   for (int i=0;i<7;i++) {
     numeric_value[i] = incoming_bytes[buffer_pos++];
   }
   numeric_value[7]='\0';
   new_display->tone_interval_ms = atol(numeric_value);

   // read 7 bytes as our new refresh interval value
   for (int i=0;i<7;i++) {
     numeric_value[i] = incoming_bytes[buffer_pos++];
   }
   numeric_value[7]='\0';
   refresh_cycle_interval = atol(numeric_value);

   // read 7 bytes as our new refresh cycle duration
   for (int i=0;i<7;i++) {
     numeric_value[i] = incoming_bytes[buffer_pos++];
   }
   numeric_value[7]='\0';
   refresh_cycle_step_duration = atol(numeric_value);

  if (TRACE || DEBUG) {
    Serial.print("New display parms read: {");
    Serial.print("["); Serial.print(new_display->value[0]);Serial.print(new_display->value[1]);Serial.print(new_display->value[2]);Serial.print(new_display->value[3]);Serial.print("],");
    Serial.println("...}");
    Serial.print("New refresh parms read: {");
    Serial.print(refresh_cycle_interval);
    Serial.println(",");
    Serial.print(refresh_cycle_step_duration);
    Serial.println("}");  }
  return new_display;
}

void pulseLed() {
  led_state = !led_state;
  digitalWrite(PIN_LED, led_state?HIGH:LOW);
  if (DEBUG) {
    Serial.print("LED: ");
    Serial.println(led_state?"ON":"OFF");
  }
}

void writeDisplayValues(struct display_spec *display_values) {
  if (DEBUG || TRACE) {Serial.print("Display: "); Serial.print(display_values->value[0]);Serial.print(display_values->value[1]);Serial.print(display_values->value[2]);Serial.println(display_values->value[3]);}
  if (when_spot_stop >= millis()) {
    if (display_values->spot_1 == 'Y' || (display_values->spot_1 == 'T' && spot_toggle)) {
      digitalWrite(PIN_SPOT1, HIGH);
      delayMicroseconds(500);
    }
    digitalWrite(PIN_SPOT1, LOW);
   
    if (display_values->spot_2 == 'Y'|| (display_values->spot_1 == 'T' && !spot_toggle)) {
      digitalWrite(PIN_SPOT2, HIGH);
      delayMicroseconds(500);  
    }
    digitalWrite(PIN_SPOT2, LOW);
  }

  for (int digit=0; digit<4; digit++) {
    displayDigit(digit,DigitValue(display_values->value[abs(3-digit)]));
    delay(5);
  }
  if (when_spot <= millis()) {
    spot_toggle = !spot_toggle;
    when_spot_stop = millis() + SPOT_DURATION;
    when_spot = millis() + SPOT_INTERVAL;
  }
}

void maintainRefreshDisplay() {
  struct display_spec *t;
  
  t = getNextRefreshDisplay(); 
  if (t) {
    if (DEBUG || TRACE) {
      Serial.print("New refresh display: {");
      Serial.print("["); Serial.print(t->value[0]);Serial.print(t->value[1]);Serial.print(t->value[2]);Serial.print(t->value[3]);Serial.print("],");
      Serial.println("...}");
    }
    if (refresh_display_spec) {
      free(refresh_display_spec);
    }
    refresh_display_spec = t;
  }
}

void maintainValueDisplay() {
  struct display_spec *t;
  
  t = getNewValueDisplay(); 
  if (t) {
    if (DEBUG || TRACE) {
      Serial.print("New value display: {");
      Serial.print("["); Serial.print(t->value[0]);Serial.print(t->value[1]);Serial.print(t->value[2]);Serial.print(t->value[3]);Serial.print("],");
      Serial.println("...}");
    }
    if (value_display_spec) {
      free(value_display_spec);
    }
    value_display_spec = t;
  }
}

void loop() {
  maintainRefreshDisplay();
  maintainValueDisplay(); 
  if (inRefreshCycleStep()) {
    if (TRACE || DEBUG) { Serial.print("Refresh display "); }
    writeDisplayValues(refresh_display_spec);
  } else {
    if (TRACE || DEBUG) { Serial.print("Value display "); }
    writeDisplayValues(value_display_spec);
  } 
  if (when_tone <= millis()) {
    if (DEBUG || TRACE) { Serial.print("tone: "); Serial.print(value_display_spec->tone_frequency); Serial.print(" for: "); Serial.println(value_display_spec->tone_duration_ms);}
    tone(PIN_SPEAKER, value_display_spec->tone_frequency, value_display_spec->tone_duration_ms);
    when_tone = millis() + value_display_spec->tone_interval_ms;
  }  
}
