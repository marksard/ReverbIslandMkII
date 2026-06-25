/*!
 * Reverb Island MkII
 * Copyright 2026 marksard
 * This software is released under the MIT license.
 * see https://opensource.org/licenses/MIT
 */

/*
# Riverb Island MkII

Riverb Island MkIIは、Spin Semiconductor FV-1 ICを用いたマルチエフェクトモジュールです。
本ファームにはi2C EEPROMスレーブ機能が組み込まれており、FV-1のi2Cと通信し、EEPROMをエミュレーションします。
また、MSCとFATFSの実装によりRP2040とPCをUSBでつなぎ、FV-1プリセットファイルをコピーすることで手軽にFV-1の
プリセット開発を楽しむことが出来ます。

## 機能概要

- 入力
  - `P0~2` プリセットのパラメーター調整用ポット
  - `P0~2 CV` プリセットのパラメーター調整用CV入力
  - `P0~2 CV DEPTH` プリセットのパラメーター調整用CVの効きの調整
  - `IN` (L)入力、調整ポット付き
  - `IN(R)` (R)入力。未接続時はINが接続されています。調整ポットなし。ステレオ対応プリセット開発者向け
- 出力
  - `OUT L/R` 出力

- 操作方法
  - `+` 次のプリセットを選択
  - `-` 前のプリセットを選択
  - `BANK` バンク切替
  - `-長押し`+`+短押し` 現在のプリセット選択を記憶します
*/

#include <Arduino.h>
#include <numeric>
#include <hardware/pwm.h>
#include <hardware/irq.h>
#include <hardware/gpio.h>
#include <EEPROM.h>
#include "lib/Button.hpp"
#include "lib/EepRomConfigIO.hpp"
#include "lib/pwm_wrapper.h"
#include "lib/MiniOsc.hpp"
#include "gpio_mapping.h"
#include "basic_definition.h"
#include "SystemConfig.hpp"
#include "Helper.h"
#include "i2c24LC32Emu.hpp"

enum ButtonCondition
{
    // 各ボタンの押下状態と各ボタンの組み合わせ
    // Button State: 0:None 1:Button down 2:Button up 3:Holding 4:Holded B:Hold edge (Holding | 0x08)
    // U: button up
    // D: button down
    // H: holging
    // L: holded (leaved)
    // E: hold edge
    // 0xBMP (BANK, MINUS, PLUS)
    NONE    = 0x000,
    UB      = 0x200,
    UM      = 0x020,
    UP      = 0x002,
    HM_EP   = 0x03B,
    HM_LP   = 0x034,
};

// 標準インターフェース
static uint interruptSliceNum;
static Button buttons[3];
static EEPROMConfigIO<SystemConfig> systemConfig(0);

static MiniOsc bankLED;

//////////////////////////////////////////

void setBankLED(int8_t bankNum)
{
    // bankLED.setLevel(bankNum == 0 ? 11 : 0);
    bankLED.setFrequency(bankNum == 0 ? 0 : 100);
    // gpio_put(LED_BANK, bankNum == 0 ? HIGH : LOW);
}

void setBank(int8_t bankNum)
{
    systemConfig.Config.bankNum = constrainCyclic(bankNum, (int8_t)0, (int8_t)1);
    setBankLED(systemConfig.Config.bankNum);
    gpio_put(FV1_T0, systemConfig.Config.bankNum == 0 ? LOW : HIGH);
}

void addBank(int8_t delta)
{
    setBank(systemConfig.Config.bankNum + delta);
}

void setPresetLED(int8_t presetNum)
{
    gpio_put(LED_HC138_A0, (presetNum & 0x01) ? HIGH : LOW);
    gpio_put(LED_HC138_A1, (presetNum & 0x02) ? HIGH : LOW);
    gpio_put(LED_HC138_A2, (presetNum & 0x04) ? HIGH : LOW);
}

void setPreset(int8_t presetNum)
{
    systemConfig.Config.PresetNum = constrainCyclic(presetNum, (int8_t)0, (int8_t)7);
    gpio_put(FV1_S0, (systemConfig.Config.PresetNum & 0x01) ? HIGH : LOW);
    gpio_put(FV1_S1, (systemConfig.Config.PresetNum & 0x02) ? HIGH : LOW);
    gpio_put(FV1_S2, (systemConfig.Config.PresetNum & 0x04) ? HIGH : LOW);
    setPresetLED(systemConfig.Config.PresetNum);
}

void addPreset(int8_t delta)
{
    setPreset(systemConfig.Config.PresetNum + delta);
}

void operation(uint16_t buttonStates)
{
    if (buttonStates == ButtonCondition::UB)
    {
        addBank(1);
    }
    else if (buttonStates == ButtonCondition::UM)
    {
        addPreset(-1);
    }
    else if (buttonStates == ButtonCondition::UP)
    {
        addPreset(1);
    }
    else if (buttonStates == ButtonCondition::HM_EP)
    {
        bankLED.setFrequency(5);
    }
    else if (buttonStates == ButtonCondition::HM_LP)
    {
        setBankLED(systemConfig.Config.bankNum);
        systemConfig.saveUserConfig();
    }
}

void setup()
{
    // Serial.begin(9600);
    // while (!Serial)
    // {
    // }
    // sleep_ms(500);

    set_sys_clock_hz(CPU_CLOCK, true);
    pinMode(23, OUTPUT);
    gpio_put(23, HIGH);

    // EEPROM Slave Emulator Initialization
    initFatFsLoadHex();
    initI2C24LC32Emu(i2c0, SDA, SCL);

    // AP2112K Power OFF
    gpio_init(FV1_PWR_EN);
    gpio_set_dir(FV1_PWR_EN, GPIO_OUT);
    gpio_put(FV1_PWR_EN, LOW);

    // LED control initialization
    bankLED.init(1000, PWM_BIT);
    bankLED.setWave(MiniOsc::Wave::SQU);
    bankLED.setFrequency(1);
    bankLED.setLevel(11);
    initPWM(LED_BANK, PWM_RESO);
    // gpio_init(LED_BANK);
    // gpio_set_dir(LED_BANK, GPIO_OUT);
    gpio_init(LED_HC138_A0);
    gpio_set_dir(LED_HC138_A0, GPIO_OUT);
    gpio_init(LED_HC138_A1);
    gpio_set_dir(LED_HC138_A1, GPIO_OUT);
    gpio_init(LED_HC138_A2);
    gpio_set_dir(LED_HC138_A2, GPIO_OUT);
    gpio_init(LED_HC138_E2);
    gpio_set_dir(LED_HC138_E2, GPIO_OUT);

    // FV-1 Control Initialization
    gpio_init(FV1_S0);
    gpio_set_dir(FV1_S0, GPIO_OUT);
    gpio_init(FV1_S1);
    gpio_set_dir(FV1_S1, GPIO_OUT);
    gpio_init(FV1_S2);
    gpio_set_dir(FV1_S2, GPIO_OUT);
    gpio_init(FV1_T0);
    gpio_set_dir(FV1_T0, GPIO_OUT);

    // 初期値
    // gpio_put(LED_BANK, HIGH);
    gpio_put(LED_HC138_A0, LOW);
    gpio_put(LED_HC138_A1, LOW);
    gpio_put(LED_HC138_A2, LOW);
    gpio_put(LED_HC138_E2, LOW);
    gpio_put(FV1_S0, LOW);
    gpio_put(FV1_S1, LOW);
    gpio_put(FV1_S2, LOW);
    gpio_put(FV1_T0, LOW);

    buttons[0].init(BTN_BANK, INPUT_PULLDOWN, true, true);
    buttons[0].setHoldTime(350);
    buttons[1].init(BTN_MINUS, INPUT_PULLDOWN, true, true);
    buttons[1].setHoldTime(350);
    buttons[2].init(BTN_PLUS, INPUT_PULLDOWN, true, true);
    buttons[2].setHoldTime(350);

    systemConfig.initEEPROM();
    systemConfig.loadUserConfig();

    // FV-1起動直前に設定されたプリセットに強制的に反映させるため
    // ひとつ前にずらして起動後に正しい設定を反映する
    addBank(-1);
    addPreset(-1);
    
    // AP2112K Power ON
    gpio_put(FV1_PWR_EN, HIGH);
}

ulong startupWaitTime = 1000;
bool startupDone = false;

void loop()
{
    // 起動LED演出と最初のプリセット選択
    ulong mills = millis();
    if (startupDone == false)
    {
        if (mills > startupWaitTime)
        {
            startupDone = true;
            addBank(1);
            sleep_ms(10); // バンク設定でi2cアクセスするので少しウェイトを入れる
            addPreset(1);
        }
        else
        {
            int8_t led = mills / 100;
            if (led != 0)
            {
                gpio_put(LED_HC138_E2, HIGH);
                setPresetLED(led - 1);
            }
            else if (led == 0)
            {
                setBankLED(1);
            }
        }
    }

    uint8_t btnB = buttons[0].getState();
    uint8_t btnM = buttons[1].getState();
    uint8_t btnP = buttons[2].getState();
    uint16_t buttonStates = (btnB << 8) + (btnM << 4) + btnP;

    operation(buttonStates);
    
    pwm_set_gpio_level(LED_BANK, bankLED.getFrequencey() == 0.0 ? PWM_RESO : bankLED.getWaveValue());

    updateFatFsLoadHex();

    tight_loop_contents();
    sleep_ms(1);
}
