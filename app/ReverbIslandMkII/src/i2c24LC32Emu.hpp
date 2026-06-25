/*!
 * i2c24LC32Emulation
 * Copyright 2025 marksard
 * This software is released under the MIT license.
 * see https://opensource.org/licenses/MIT
 */

#pragma once

#include <Arduino.h>
#include <hardware/i2c.h>
#include "pico/i2c_slave.h"
#include "EepromHex.hpp"

#define ALLOW_MASTER_WRITES false
static const size_t PRESET_SIZE = 512;
static const size_t PRESET_COUNT = 8;
static const size_t MAX_EEPROM_SLAVE_SIZE = PRESET_SIZE * PRESET_COUNT;
static uint8_t eepromImage[4096] = {0};

// ---------- I2C Slave Event Callback ----------
static void onI2cSlaveEvent(i2c_inst_t *i2c, i2c_slave_event_t event)
{
    static uint8_t addrBuf[2];
    static int addrByteCount = 0;
    static uint16_t currentAddress = 0;

    switch (event)
    {
    case I2C_SLAVE_RECEIVE:
    {
        uint8_t data = i2c_read_byte_raw(i2c);
        if (addrByteCount < 2)
        {
            addrBuf[addrByteCount++] = data;
            if (addrByteCount == 2)
            {
                currentAddress = ((uint16_t)addrBuf[0] << 8) | addrBuf[1];
                if (currentAddress >= MAX_EEPROM_SLAVE_SIZE)
                {
                    currentAddress = 0;
                }
            }
            // No test for writes
        }
        else if (ALLOW_MASTER_WRITES)
        {
            if (currentAddress < MAX_EEPROM_SLAVE_SIZE)
            {
                eepromImage[currentAddress++] = data;
            }
            else
            {
                currentAddress = 0;
            }
        }
        break;
    }
    case I2C_SLAVE_REQUEST:
    {
        // Send PRESET_SIZE bytes for FV-1 EEPROM emulation.
        i2c_write_raw_blocking(i2c, &eepromImage[currentAddress], PRESET_SIZE);
        currentAddress += PRESET_SIZE;
        // i2c_write_byte_raw(i2c, eepromImage[currentAddress]);
        // currentAddress++;
        if (currentAddress >= MAX_EEPROM_SLAVE_SIZE)
        {
            currentAddress = 0;
        }
        addrByteCount = 0;
        break;
    }
    case I2C_SLAVE_FINISH:
        addrByteCount = 0;
        break;
    default:
        break;
    }
}

void initI2C24LC32Emu(i2c_inst_t *i2c, uint8_t sda, uint8_t scl)
{
    // 24LC32 typical I2C speed is 400kHz
    i2c_init(i2c, 400 * 1000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    // Use FV-1 internal pull-up
    // gpio_pull_up(sda);
    // gpio_pull_up(scl);
    // 0x50 is the typical 7-bit I2C address for EEPROMs
    i2c_slave_init(i2c, 0x50, &onI2cSlaveEvent);
}

#include <FatFS.h>
#include <FatFSUSB.h>

#include <Adafruit_NeoPixel.h>
static Adafruit_NeoPixel pixels(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

volatile bool updated = false;
volatile bool driveConnected = false;
volatile bool inPrinting = false;
#define MAX_FILE_BYTES (32 * 1024) // 32kB のファイルを想定
static uint8_t fileBuffer[MAX_FILE_BYTES];

// Called by FatFSUSB when the drive is released.  We note this, restart FatFS, and tell the main loop to rescan.
static void unplug(uint32_t i)
{
    (void)i;
    driveConnected = false;
    updated = true;
    FatFS.begin();
}

// Called by FatFSUSB when the drive is mounted by the PC.  Have to stop FatFS, since the drive data can change, note it, and continue.
static void plug(uint32_t i)
{
    (void)i;
    driveConnected = true;
    FatFS.end();
}

// Called by FatFSUSB to determine if it is safe to let the PC mount the USB drive.  If we're accessing the FS in any way, have any Files open, etc. then it's not safe to let the PC mount the drive.
static bool mountable(uint32_t i)
{
    (void)i;
    return !inPrinting;
}

// Intel HEX parser: inBuf は ASCII バイトデータ、len は長さ。
// 成功時には binaryBuffer にバイナリを書き込み、binarySize を設定。最大 64KB を想定。
bool parseIntelHex(const uint8_t *inBuf, size_t len, uint8_t *binaryBuffer, size_t &binarySize)
{
    // 初期化
    memset(binaryBuffer, 0xFF, binarySize);
    size_t maxAddr = 0;
    const char *p = (const char *)inBuf;
    const char *end = p + len;
    while (p < end)
    {
        // 行の先頭を探す
        while (p < end && *p != ':')
            p++;
        if (p >= end)
            break;
        // 読み込む1行
        const char *line = p++;
        // find end of line
        const char *nl = line;
        while (nl < end && *nl != '\n' && *nl != '\r')
            nl++;
        size_t linelen = nl - line;
        // minimal length ":BBAAAATT..." -> at least 11 chars
        if (linelen < 11)
        {
            Serial.printf("01 Unsupported record type: %02X\n", linelen);
            return false;
        }
        // parse hex pairs
        auto hex2 = [&](const char *s) -> int
        {
            auto hx = [](char c) -> int
            {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                return -1;
            };
            int hi = hx(s[0]);
            int lo = hx(s[1]);
            if (hi < 0 || lo < 0)
                return -1;
            return (hi << 4) | lo;
        };
        // バイトカウント
        int byteCount = hex2(line + 1);
        int addrHi = hex2(line + 3);
        int addrLo = hex2(line + 5);
        int rectype = hex2(line + 7);
        if (byteCount < 0 || addrHi < 0 || addrLo < 0 || rectype < 0)
        {
            Serial.printf("02 Unsupported record type: %02X %02X %02X %02X\n", byteCount, addrHi, addrLo, rectype);
            return false;
        }
        uint16_t address = (addrHi << 8) | addrLo;
        // checksum calc
        int sum = 0;
        sum += byteCount;
        sum += addrHi;
        sum += addrLo;
        sum += rectype;
        // data bytes
        const char *dataPtr = line + 9;
        for (int i = 0; i < byteCount; ++i)
        {
            int b = hex2(dataPtr + i * 2);
            if (b < 0)
            {
                Serial.printf("03 Unsupported record type: %02X\n", b);
                return false;
            }
            sum += b;
        }
        // checksum byte (last)
        int checksum = hex2(dataPtr + byteCount * 2);
        if (checksum < 0)
        {
            Serial.printf("04 Unsupported record type: %02X\n", checksum);
            return false;
        }
        sum = (sum + checksum) & 0xFF;
        if (sum != 0)
        {
            Serial.println("Intel HEX checksum mismatch");
            return false;
        }
        // 処理: データレコード (00) をバイナリに書き込む。他は簡易対応（04=extended linear addr）
        static uint32_t extended_linear = 0;
        if (rectype == 0x00)
        {
            uint32_t fullAddr = extended_linear + address;
            for (int i = 0; i < byteCount; ++i)
            {
                int b = hex2(dataPtr + i * 2);
                if (fullAddr + i >= binarySize)
                {
                    return false;
                }

                binaryBuffer[fullAddr + i] = (uint8_t)b;
            }
            if (fullAddr + byteCount > maxAddr)
                maxAddr = fullAddr + byteCount;
        }
        else if (rectype == 0x01)
        {
            // EOF
            break;
        }
        else if (rectype == 0x04)
        {
            // Extended Linear Address (high 16 bits)
            // dataPtr contains two bytes
            int hi = hex2(dataPtr);
            int lo = hex2(dataPtr + 2);
            if (hi < 0 || lo < 0)
            {
                Serial.printf("05 Unsupported record type: %02X, %02X\n", hi, lo);
                return false;
            }
            extended_linear = ((uint32_t)hi << 8 | lo) << 16;
        }
        else
        {
            // 他のレコードタイプはサポート外として失敗扱いにする
            Serial.printf("06 Unsupported record type: %02X\n", rectype);
            return false;
        }
        // advance p to next line
        p = nl;
    }
    binarySize = maxAddr;
    return true;
}

void printDirectory(String dirName, int numTabs)
{
    inPrinting = true;
    Dir dir = FatFS.openDir(dirName);
    Serial.printf("Listing directory: %s\n", dirName.c_str());
    while (true)
    {
        bool isDir = dir.next();
        Serial.printf("isDir=%d\n", isDir ? 1 : 0);
        if (!isDir)
        {
            // no more files
            break;
        }
        for (int i = 0; i < numTabs; i++)
        {
            Serial.print('\t');
        }
        Serial.print(dir.fileName());
        if (dir.isDirectory())
        {
            Serial.println("/");
            printDirectory(dirName + "/" + dir.fileName(), numTabs + 1);
        }
        else
        {
            // files have sizes, directories do not
            Serial.print("\t\t");
            Serial.print(dir.fileSize(), DEC);
            time_t cr = dir.fileCreationTime();
            struct tm tmstruct;
            localtime_r(&cr, &tmstruct);
            Serial.printf("\t%d-%02d-%02d %02d:%02d:%02d\n", (tmstruct.tm_year) + 1900, (tmstruct.tm_mon) + 1, tmstruct.tm_mday, tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);
        }
        tight_loop_contents();
    }
    inPrinting = false;
}

void loadHex()
{
    bool result = false;
    inPrinting = true;
    size_t binSize = MAX_EEPROM_SLAVE_SIZE;
    File f = FatFS.open("eeprom01.hex", "r");
    if (f)
    {
        size_t gotLen = f.read(fileBuffer, MAX_FILE_BYTES);
        Serial.printf("parsing...\n");
        // eepromImage = (uint8_t *)calloc(binSize, 1);
        result = parseIntelHex(fileBuffer, gotLen, eepromImage, binSize);
    }
    else
    {
        Serial.println("No eeprom01.hex file found.");
        // eepromImage = (uint8_t *)calloc(MAX_EEPROM_SLAVE_SIZE, 1);
        result = parseIntelHex((uint8_t *)eeprom01Hex, strlen(eeprom01Hex), eepromImage, binSize);
    }

    if (result)
    {
        Serial.printf("Intel HEX parsed OK, binary size=%lu\n", (unsigned long)binSize);
    }
    else
    {
        Serial.printf("Intel HEX parsed NG, binary size=%lu\n", (unsigned long)binSize);
    }

    inPrinting = false;
}

void initFatFsLoadHex()
{
    if (!FatFS.begin())
    {
        Serial.println("FatFS initialization failed!");
        while (1)
        {
            delay(1);
        }
    }
    Serial.println("FatFS initialization done.");

    FatFSUSB.onUnplug(unplug);
    FatFSUSB.onPlug(plug);
    FatFSUSB.driveReady(mountable);

    inPrinting = true;
    printDirectory("/", 0);
    inPrinting = false;

    loadHex();

    pixels.begin();
    pixels.clear();
    pixels.show();
}

void updateFatFsLoadHex()
{
    if (updated && !driveConnected)
    {
        inPrinting = true;
        pixels.setPixelColor(0, pixels.Color(40, 160, 180));
        pixels.show();
        Serial.println("\n\nDisconnected, new file listing:");
        inPrinting = false;
        FatFS.end();
        FatFS.begin();
        printDirectory("/", 0);
        loadHex();
        updated = false;
    }

    static bool mscEntry = false;
    static bool lastBootsel = false;
    bool bootsel = __Bootsel();
    if (bootsel == true && lastBootsel == false)
    {
        lastBootsel = true;
    }
    else if (bootsel == false && lastBootsel == true)
    {
        lastBootsel = false;
        if (mscEntry == false)
        {
            mscEntry = true;
            FatFSUSB.begin();
            inPrinting = true;
            Serial.println("FatFSUSB started.");
            Serial.println("Connect drive via USB to upload/erase files and re-display");
            inPrinting = false;
            pixels.setPixelColor(0, pixels.Color(40, 160, 180));
            pixels.show();
        }
        else
        {
            mscEntry = false;
            FatFSUSB.end();
            inPrinting = true;
            Serial.println("FatFSUSB ended.");
            inPrinting = false;
            pixels.clear();
            pixels.show();
        }
    }
}
