/*!
 * Helper
 * Copyright 2025 marksard
 * This software is released under the MIT license.
 * see https://opensource.org/licenses/MIT
 */

#include <Arduino.h>

template <typename vs = int8_t>
vs constrainCyclic(vs value, vs min, vs max)
{
    if (value > max)
        return min;
    if (value < min)
        return max;
    return value;
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

template <typename vs = int16_t>
float conv2ExpScaled(int16_t adcValue, vs maxValue)
{
    const int16_t adcResoM1 = ADC_RESO - 1;
    const float ratio = 5.0;
    const float adcRatio = ratio / adcResoM1;
    const float expRatio = 1.0 / (exp(ratio) - 1.0f);
    return (exp(adcValue * adcRatio) - 1.0) * expRatio * (float)maxValue;
}

template <typename vs = int16_t>
float conv2LinScaled(int16_t adcValue, vs maxValue)
{
    const int16_t adcResoM1 = ADC_RESO - 1;
    return (float)adcValue / (float)adcResoM1 * (float)maxValue;
}

int32_t getSamplingFrequency()
{
    static unsigned long lastTime = 0;
    static bool initialized = false;
    unsigned long currentTime = micros();
    unsigned long deltaTime;

    if (!initialized)
    {
        lastTime = currentTime;
        initialized = true;
        return 1; // 初回は周波数を計算できない
    }

    if (currentTime >= lastTime)
    {
        deltaTime = currentTime - lastTime;
    }
    else
    {
        // オーバーフロー対応
        deltaTime = (4294967295UL - lastTime) + currentTime + 1;
    }

    lastTime = currentTime;

    if (deltaTime == 0)
        return 1; // ゼロ除算防止

    return 1000000 / deltaTime; // Hz
}