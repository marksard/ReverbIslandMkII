/*!
 * SystemConfig
 * Copyright 2026 marksard
 * This software is released under the MIT license.
 * see https://opensource.org/licenses/MIT
 */

#pragma once

#include <Arduino.h>

// 基本設定
struct SystemConfig
{
    char ver[15] = "RI02_conf_000\0";
    int8_t bankNum;
    int8_t PresetNum;

    SystemConfig()
    {
        bankNum = 0;
        PresetNum = 0;
    }
};
