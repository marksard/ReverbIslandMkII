/*!
 * GPIO Mapping
 * Copyright 2026 marksard
 * This software is released under the MIT license.
 * see https://opensource.org/licenses/MIT
 */

#pragma once
#include <Arduino.h>

#define PWM_INTR_PIN D25 // PMW4 B

#define BTN_BANK D29
#define BTN_MINUS D28
#define BTN_PLUS D27

// Direct control pin for BANK LED
#define LED_BANK D26

// Preset LED control pin for 74HC138
#define LED_HC138_A0 D7
#define LED_HC138_A1 D8
#define LED_HC138_A2 D14
#define LED_HC138_E2 D15

// FV-1 control pins
#define FV1_S0 D0
#define FV1_S1 D1
#define FV1_S2 D2
#define FV1_T0 D3

// AP2112K Enable pin (power control for FV-1)
#define FV1_PWR_EN D6
