/*
 * The Akafugu Nixie Clock
 * (C) 2012-13 Akafugu Corporation
 *
 * This program is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *
 */

#include "global.h"

#ifdef BOARD_MK2

#include "rgbled.h"

extern uint8_t display_on;
extern uint8_t digits;
extern volatile uint8_t data[6];

extern volatile uint8_t g_blank;
extern volatile bool g_blink_on;
extern volatile bool g_antipoison;
extern volatile bool g_randomize_on;

extern state_t g_clock_state;
extern volatile bool g_24h;
extern volatile bool g_is_am;

// anodes (digits)
pin_direct_t digit0_pin;
pin_direct_t digit1_pin;

// HV5812 Data In (PC0 - A0)
#define DATA_HIGH DIRECT_PIN_HIGH(data_pin.reg, data_pin.bitmask)
#define DATA_LOW  DIRECT_PIN_LOW(data_pin.reg, data_pin.bitmask)

// HV5812 Clock (PC1 - A1)
#define CLOCK_HIGH DIRECT_PIN_HIGH(clock_pin.reg, clock_pin.bitmask)
#define CLOCK_LOW  DIRECT_PIN_LOW(clock_pin.reg, clock_pin.bitmask)

// HV5812 Latch / Strobe (PC2 - A2)
#define LATCH_ENABLE  DIRECT_PIN_LOW(latch_pin.reg, latch_pin.bitmask)
#define LATCH_DISABLE DIRECT_PIN_HIGH(latch_pin.reg, latch_pin.bitmask)

pin_direct_t data_pin;
pin_direct_t clock_pin;
pin_direct_t latch_pin;
pin_direct_t blank_pin;

uint8_t multiplex_counter = 0;

void clear_display();
void write_hv5812_8bit(uint8_t data);
void write_nixie(uint8_t digit, uint8_t value1, uint8_t value2);
void set_number(uint8_t value1, uint8_t value2);
void set_indicator(uint8_t intensity, bool override_state = false);
uint32_t rnd(void);

extern pin_direct_t switch_pin;
extern volatile uint8_t g_alarm_switch;

void board_init()
{
    // alarm switch
    pinMode(PinMap::alarm_switch, INPUT); // switch as input
    digitalWrite(PinMap::alarm_switch, HIGH); // enable pullup
  
    switch_pin.pin = PinMap::alarm_switch;
    switch_pin.reg = PIN_TO_INPUT_REG(PinMap::alarm_switch);
    switch_pin.bitmask = PIN_TO_BITMASK(PinMap::alarm_switch);
  
    if ( (*switch_pin.reg & switch_pin.bitmask) == 0)
      g_alarm_switch = true;
    else
      g_alarm_switch = false;

    // digits
    pinMode(PinMap::digit0, OUTPUT);
    pinMode(PinMap::digit1, OUTPUT);

    digit0_pin.pin = PinMap::digit0;
    digit0_pin.reg = PIN_TO_OUTPUT_REG(PinMap::digit0);
    digit0_pin.bitmask = PIN_TO_BITMASK(PinMap::digit0);
    digit1_pin.pin = PinMap::digit1;
    digit1_pin.reg = PIN_TO_OUTPUT_REG(PinMap::digit1);
    digit1_pin.bitmask = PIN_TO_BITMASK(PinMap::digit1);

    pinMode(PinMap::dots, OUTPUT);

    pinMode(PinMap::data, OUTPUT);
    pinMode(PinMap::clock, OUTPUT);
    pinMode(PinMap::latch, OUTPUT);
    pinMode(PinMap::blank, OUTPUT);

    data_pin.pin = PinMap::data;
    data_pin.reg = PIN_TO_OUTPUT_REG(PinMap::data);
    data_pin.bitmask = PIN_TO_BITMASK(PinMap::data);

    clock_pin.pin = PinMap::data;
    clock_pin.reg = PIN_TO_OUTPUT_REG(PinMap::clock);
    clock_pin.bitmask = PIN_TO_BITMASK(PinMap::clock);

    latch_pin.pin = PinMap::latch;
    latch_pin.reg = PIN_TO_OUTPUT_REG(PinMap::latch);
    latch_pin.bitmask = PIN_TO_BITMASK(PinMap::latch);

    blank_pin.pin = PinMap::blank;
    blank_pin.reg = PIN_TO_OUTPUT_REG(PinMap::blank);
    blank_pin.bitmask = PIN_TO_BITMASK(PinMap::blank);

    digitalWrite(PinMap::blank, LOW);
    
    // write 00:00 to the board
    uint32_t val = 0b11111111111111111111101111111110;

    write_hv5812_8bit(val >> 16);
    write_hv5812_8bit(val >> 8);
    write_hv5812_8bit(val);

    LATCH_DISABLE;
    LATCH_ENABLE; 
}

void set_dots(bool dot1, bool dot2)
{
  if (dot1) digitalWrite(PinMap::dots, LOW);
  else      digitalWrite(PinMap::dots, HIGH);
}

// Display multiplex routine for HV5812 driver
void display_multiplex(void)
{
  if (multiplex_counter == 0)
    clear_display();
  else if (multiplex_counter >= 1 && multiplex_counter <= 10)
    display_on ? write_nixie(0, data[5], data[3]) : clear_display();
  else if (multiplex_counter == 11)
    clear_display();
  else if (multiplex_counter >= 12 && multiplex_counter <= 21)
    display_on ? write_nixie(1, data[4], data[2]) : clear_display();

  multiplex_counter++;

  if (multiplex_counter == 22) multiplex_counter = 0;
}

// Write 8 bits to HV5812 driver
void write_hv5812_8bit(uint8_t data)
{
	// shift out MSB first
	for (uint8_t i = 0; i < 8; i++)  {
		if (!!(data & (1 << (7 - i))))
			DATA_HIGH;
		else
			DATA_LOW;

		CLOCK_HIGH;
		CLOCK_LOW;
  }
}

void write_nixie(uint8_t digit, uint8_t value1, uint8_t value2)
{
  //clear_display();

  if (g_blink_on) {
    if (g_blank == 4) { value1 = value2 = 10; }
    else if (g_blank == 1) { value1 = 10; }
    else if (g_blank == 2) { value2 = 10; }
  }

  set_number(value1, value2);

  switch (digit) {
    case 0:
      DIRECT_PIN_HIGH(digit0_pin.reg, digit0_pin.bitmask);
      break; 
    case 1:
      DIRECT_PIN_HIGH(digit1_pin.reg, digit1_pin.bitmask);
      break; 
  }
  
}

void clear_display(void)
{
  DIRECT_PIN_LOW(digit0_pin.reg, digit0_pin.bitmask);
  DIRECT_PIN_LOW(digit1_pin.reg, digit1_pin.bitmask);
}

void set_number(uint8_t value1, uint8_t value2)
{   
    uint32_t val = value2 == 10 ? 0 : (1<<(uint32_t)value2);
    val <<= 10;
    val |= (value1 == 10 ? 0 : (1<<(uint32_t)value1));
    val = ~val;

    /*
    uint32_t val = (1<<(uint32_t)value2);
    val <<= 10;
    val |= (1<<(uint32_t)value1);
    val = ~val;
    */

    write_hv5812_8bit(val >> 16);
    write_hv5812_8bit(val >> 8);
    write_hv5812_8bit(val);

    LATCH_DISABLE;
    LATCH_ENABLE;
}

void set_indicator(uint8_t intensity, bool override_state)
{
  switch (intensity)
  {
  case INDICATOR_OFF:
      //analogWrite(PinMap::piezo, 0);
  
    if (g_clock_state == STATE_CLOCK && !override_state) {
      if (g_24h)    
        pca9685_set_channel(13, 0);
      else
        pca9685_set_channel(13, g_is_am ? 0 : 100);

      pca9685_set_channel(14, 0);
      pca9685_set_channel(15, g_alarm_switch ? 100 : 0);  
    }
    else {
      pca9685_set_channel(13, 0);
      pca9685_set_channel(14, 0);
      pca9685_set_channel(15, 0);
    }
    break;
  case INDICATOR_HIGH:
      //analogWrite(PinMap::piezo, 150);
    pca9685_set_channel(13, 3000);
  case INDICATOR_MID:
      //analogWrite(PinMap::piezo, 100);
    pca9685_set_channel(14, 3000);
  case INDICATOR_LO:
      //analogWrite(PinMap::piezo, 50);
    pca9685_set_channel(15, 3000);
  }
}

#endif // BOARD_MK2

