#include "../configuration.h"
#include "../main.h"
#include <Wire.h>
#include "mesh/generated/telemetry.pb.h"

// AXP192 and AXP2101 have the same device address, we just need to identify it in Power.cpp
#ifndef XPOWERS_AXP192_AXP2101_ADDRESS
#define XPOWERS_AXP192_AXP2101_ADDRESS      0x34
#endif

#if HAS_WIRE

void printATECCInfo()
{
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
    atecc.readConfigZone(false);

    LOG_DEBUG("ATECC608B Serial Number: ");
    for (int i = 0 ; i < 9 ; i++) {
        LOG_DEBUG("%02x",atecc.serialNumber[i]);
    }

    LOG_DEBUG(", Rev Number: ");
    for (int i = 0 ; i < 4 ; i++) {
        LOG_DEBUG("%02x",atecc.revisionNumber[i]);
    }
    LOG_DEBUG("\n");

    LOG_DEBUG("ATECC608B Config %s",atecc.configLockStatus ? "Locked" : "Unlocked");
    LOG_DEBUG(", Data %s",atecc.dataOTPLockStatus ? "Locked" : "Unlocked");
    LOG_DEBUG(", Slot 0 %s\n",atecc.slot0LockStatus ? "Locked" : "Unlocked");

    if (atecc.configLockStatus && atecc.dataOTPLockStatus && atecc.slot0LockStatus) {
        if (atecc.generatePublicKey() == false) {
            LOG_DEBUG("ATECC608B Error generating public key\n");
        } else {
            LOG_DEBUG("ATECC608B Public Key: ");
            for (int i = 0 ; i < 64 ; i++) {
                LOG_DEBUG("%02x",atecc.publicKey64Bytes[i]);
            }
            LOG_DEBUG("\n");
        }
    }
#endif
}

uint16_t getRegisterValue(uint8_t address, uint8_t reg, uint8_t length) {
    uint16_t value = 0x00;
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    delay(20);
    Wire.requestFrom(address, length);
    LOG_DEBUG("Wire.available() = %d\n", Wire.available());
    if (Wire.available() == 2) {
        // Read MSB, then LSB
        value = (uint16_t)Wire.read() << 8;  
        value |= Wire.read();
    } else if (Wire.available()) {
        value = Wire.read();
    }
    return value;
}

uint8_t oled_probe(byte addr)
{
    uint8_t r = 0;
    uint8_t r_prev = 0;
    uint8_t c = 0;
    uint8_t o_probe = 0;
    do {
        r_prev = r;
        Wire.beginTransmission(addr);
        Wire.write(0x00);
        Wire.endTransmission();
        Wire.requestFrom((int)addr, 1);
        if (Wire.available()) {
            r = Wire.read();
        }
        r &= 0x0f;

        if (r == 0x08 || r == 0x00) {
            o_probe = 2; // SH1106
        } else if ( r == 0x03 || r == 0x04 || r == 0x06 || r == 0x07) {
            o_probe = 1; // SSD1306
        }
        c++;
    } while ((r != r_prev) && (c < 4));
    LOG_DEBUG("0x%x subtype probed in %i tries \n", r, c);
    return o_probe;
}

void scanI2Cdevice()
{
    byte err, addr;
    uint16_t registerValue = 0x00;
    int nDevices = 0;
    for (addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        err = Wire.endTransmission();
        if (err == 0) {
            LOG_DEBUG("I2C device found at address 0x%x\n", addr);

            nDevices++;

            if (addr == SSD1306_ADDRESS) {
                screen_found = addr;
                screen_model = oled_probe(addr);
                if (screen_model == 1) {
                    LOG_INFO("ssd1306 display found\n");
                } else if (screen_model == 2) {
                    LOG_INFO("sh1106 display found\n");
                } else {
                    LOG_INFO("unknown display found\n");
                }
            }
#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
            if (addr == ATECC608B_ADDR) {
                keystore_found = addr;
                if (atecc.begin(keystore_found) == true) {
                    LOG_INFO("ATECC608B initialized\n");
                } else {
                    LOG_WARN("ATECC608B initialization failed\n");
                }
                printATECCInfo();
            }
#endif
#ifdef RV3028_RTC
            if (addr == RV3028_RTC){
                rtc_found = addr;
                LOG_INFO("RV3028 RTC found\n");
                Melopero_RV3028 rtc;
                rtc.initI2C();
                rtc.writeToRegister(0x35,0x07); // no Clkout
                rtc.writeToRegister(0x37,0xB4);
            }
#endif
#ifdef PCF8563_RTC
            if (addr == PCF8563_RTC){
                rtc_found = addr;
                LOG_INFO("PCF8563 RTC found\n");
            }
#endif
            if (addr == CARDKB_ADDR) {
                cardkb_found = addr;
                // Do we have the RAK14006 instead?
                registerValue = getRegisterValue(addr, 0x04, 1);
                if (registerValue == 0x02) { // KEYPAD_VERSION
                    LOG_INFO("RAK14004 found\n");
                    kb_model = 0x02;
                } else {
                    LOG_INFO("m5 cardKB found\n");
                    kb_model = 0x00;
                }
            }
            if (addr == ST7567_ADDRESS) {
                screen_found = addr;
                LOG_INFO("st7567 display found\n");
            }
#ifdef HAS_PMU
            if (addr == XPOWERS_AXP192_AXP2101_ADDRESS) {
                pmu_found = true;
                LOG_INFO("axp192/axp2101 PMU found\n");
            }
#endif
            if (addr == BME_ADDR || addr == BME_ADDR_ALTERNATE) {
                registerValue = getRegisterValue(addr, 0xD0, 1); // GET_ID
                if (registerValue == 0x61) {
                    LOG_INFO("BME-680 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_BME680] = addr;
                } else if (registerValue == 0x60) {
                    LOG_INFO("BME-280 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_BME280] = addr;
                } else {
                    LOG_INFO("BMP-280 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_BMP280] = addr;
                }
            }
            if (addr == INA_ADDR || addr == INA_ADDR_ALTERNATE) {
                registerValue = getRegisterValue(addr, 0xFE, 2);
                LOG_DEBUG("Register MFG_UID: 0x%x\n", registerValue);
                if (registerValue == 0x5449) {
                    LOG_INFO("INA260 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_INA260] = addr;
                } else { // Assume INA219 if INA260 ID is not found
                    LOG_INFO("INA219 sensor found at address 0x%x\n", (uint8_t)addr);
                    nodeTelemetrySensorsMap[TelemetrySensorType_INA219] = addr;
                }
            }
            if (addr == MCP9808_ADDR) {
                nodeTelemetrySensorsMap[TelemetrySensorType_MCP9808] = addr;
                LOG_INFO("MCP9808 sensor found\n");
            }
            if (addr == SHT31_ADDR) {
                LOG_INFO("SHT31 sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_SHT31] = addr;
            }
            if (addr == SHTC3_ADDR) {
                LOG_INFO("SHTC3 sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_SHTC3] = addr;
            }
            if (addr == LPS22HB_ADDR || addr == LPS22HB_ADDR_ALT) {
                LOG_INFO("LPS22HB sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_LPS22] = addr;
            }

            // High rate sensors, will be processed internally
            if (addr == QMC6310_ADDR) {
                LOG_INFO("QMC6310 Highrate 3-Axis magnetic sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_QMC6310] = addr;
            }
            if (addr == QMI8658_ADDR) {
                LOG_INFO("QMI8658 Highrate 6-Axis inertial measurement sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_QMI8658] = addr;
            }
            if (addr == QMC5883L_ADDR) {
                LOG_INFO("QMC5883L Highrate 3-Axis magnetic sensor found\n");
                nodeTelemetrySensorsMap[TelemetrySensorType_QMC5883L] = addr;
            }
        } else if (err == 4) {
            LOG_ERROR("Unknow error at address 0x%x\n", addr);
        }
    }

    if (nDevices == 0)
        LOG_INFO("No I2C devices found\n");
    else
        LOG_INFO("%i I2C devices found\n",nDevices);
}
#else
void scanI2Cdevice() {}
#endif
