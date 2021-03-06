/**
 * ems.cpp
 *
 * Handles all the processing of the EMS messages
 *
 * Paul Derbyshire - https://github.com/proddy/EMS-ESP
 */

#include "ems.h"
#include "ems_devices.h"
#include "emsuart.h"
#include <Arduino.h>
#include <CircularBuffer.h> // https://github.com/rlogiacco/CircularBuffer
#include <MyESP.h>
#include <list> // std::list

// myESP for logging to telnet and serial
#define myDebug(...) myESP.myDebug(__VA_ARGS__)

_EMS_Sys_Status EMS_Sys_Status; // EMS Status

CircularBuffer<_EMS_TxTelegram, EMS_TX_TELEGRAM_QUEUE_MAX> EMS_TxQueue; // FIFO queue for Tx send buffer

// macros used in the _process* functions
#define _toByte(i) (data[i])
#define _toShort(i) ((data[i] << 8) + data[i + 1])
#define _toLong(i) ((data[i] << 16) + (data[i + 1] << 8) + (data[i + 2]))
#define _bitRead(i, bit) (((data[i]) >> (bit)) & 0x01)

//
// process callbacks per type
//

// generic
void _process_Version(uint8_t src, uint8_t * data, uint8_t length);

// Boiler and Buderus devices
void _process_UBAMonitorFast(uint8_t src, uint8_t * data, uint8_t length);
void _process_UBAMonitorSlow(uint8_t src, uint8_t * data, uint8_t length);
void _process_UBAMonitorWWMessage(uint8_t src, uint8_t * data, uint8_t length);
void _process_UBAParameterWW(uint8_t src, uint8_t * data, uint8_t length);
void _process_UBATotalUptimeMessage(uint8_t src, uint8_t * data, uint8_t length);
void _process_UBAParametersMessage(uint8_t src, uint8_t * data, uint8_t length);
void _process_SetPoints(uint8_t src, uint8_t * data, uint8_t length);
void _process_SM10Monitor(uint8_t src, uint8_t * data, uint8_t length);

// Common for most thermostats
void _process_RCTime(uint8_t src, uint8_t * data, uint8_t length);
void _process_RCOutdoorTempMessage(uint8_t src, uint8_t * data, uint8_t length);

// RC10
void _process_RC10Set(uint8_t src, uint8_t * data, uint8_t length);
void _process_RC10StatusMessage(uint8_t src, uint8_t * data, uint8_t length);

// RC20
void _process_RC20Set(uint8_t src, uint8_t * data, uint8_t length);
void _process_RC20StatusMessage(uint8_t src, uint8_t * data, uint8_t length);

// RC30
void _process_RC30Set(uint8_t src, uint8_t * data, uint8_t length);
void _process_RC30StatusMessage(uint8_t src, uint8_t * data, uint8_t length);

// RC35
void _process_RC35Set(uint8_t src, uint8_t * data, uint8_t length);
void _process_RC35StatusMessage(uint8_t src, uint8_t * data, uint8_t length);
//lobocobra start
void _process_AnlageParamSet(uint8_t src, uint8_t * data, uint8_t length);
void _process_HK2Schaltzeiten(uint8_t src, uint8_t * data, uint8_t length);
//lobocobra end

// Easy
void _process_EasyStatusMessage(uint8_t type, uint8_t * data, uint8_t length);

//RC1010
void _process_RC1010StatusMessage(uint8_t type, uint8_t * data, uint8_t length);
void _process_RC1010SetMessage(uint8_t type, uint8_t * data, uint8_t length);

/*
 * Recognized EMS types and the functions they call to process the telegrams
 * Format: MODEL ID, TYPE ID, Description, function, emsplus
 */
const _EMS_Type EMS_Types[] = {

    // common
    {EMS_MODEL_ALL, EMS_TYPE_Version, "Version", _process_Version, false},

    // Boiler commands
    {EMS_MODEL_UBA, EMS_TYPE_UBAMonitorFast, "UBAMonitorFast", _process_UBAMonitorFast, false},
    {EMS_MODEL_UBA, EMS_TYPE_UBAMonitorSlow, "UBAMonitorSlow", _process_UBAMonitorSlow, false},
    {EMS_MODEL_UBA, EMS_TYPE_UBAMonitorWWMessage, "UBAMonitorWWMessage", _process_UBAMonitorWWMessage, false},
    {EMS_MODEL_UBA, EMS_TYPE_UBAParameterWW, "UBAParameterWW", _process_UBAParameterWW, false},
    {EMS_MODEL_UBA, EMS_TYPE_UBATotalUptimeMessage, "UBATotalUptimeMessage", _process_UBATotalUptimeMessage, false},
    {EMS_MODEL_UBA, EMS_TYPE_UBAMaintenanceSettingsMessage, "UBAMaintenanceSettingsMessage", NULL, false},
    {EMS_MODEL_UBA, EMS_TYPE_UBAParametersMessage, "UBAParametersMessage", _process_UBAParametersMessage, false},
    {EMS_MODEL_UBA, EMS_TYPE_UBASetPoints, "UBASetPoints", _process_SetPoints, false},

    // Other devices
    {EMS_MODEL_OTHER, EMS_TYPE_SM10Monitor, "SM10Monitor", _process_SM10Monitor},

    // RC10
    {EMS_MODEL_RC10, EMS_TYPE_RCTime, "RCTime", _process_RCTime, false},
    {EMS_MODEL_RC10, EMS_TYPE_RC10Set, "RC10Set", _process_RC10Set, false},
    {EMS_MODEL_RC10, EMS_TYPE_RC10StatusMessage, "RC10StatusMessage", _process_RC10StatusMessage, false},

    // RC20 and RC20F
    {EMS_MODEL_RC20, EMS_TYPE_RCOutdoorTempMessage, "RCOutdoorTempMessage", _process_RCOutdoorTempMessage, false},
    {EMS_MODEL_RC20, EMS_TYPE_RCTime, "RCTime", _process_RCTime, false},
    {EMS_MODEL_RC20, EMS_TYPE_RC20Set, "RC20Set", _process_RC20Set, false},
    {EMS_MODEL_RC20, EMS_TYPE_RC20StatusMessage, "RC20StatusMessage", _process_RC20StatusMessage, false},

    {EMS_MODEL_RC20F, EMS_TYPE_RCOutdoorTempMessage, "RCOutdoorTempMessage", _process_RCOutdoorTempMessage, false},
    {EMS_MODEL_RC20F, EMS_TYPE_RCTime, "RCTime", _process_RCTime, false},
    {EMS_MODEL_RC20F, EMS_TYPE_RC20Set, "RC20Set", _process_RC20Set, false},
    {EMS_MODEL_RC20F, EMS_TYPE_RC20StatusMessage, "RC20StatusMessage", _process_RC20StatusMessage, false},

    // RC30
    {EMS_MODEL_RC30, EMS_TYPE_RCOutdoorTempMessage, "RCOutdoorTempMessage", _process_RCOutdoorTempMessage, false},
    {EMS_MODEL_RC30, EMS_TYPE_RCTime, "RCTime", _process_RCTime, false},
    {EMS_MODEL_RC30, EMS_TYPE_RC30Set, "RC30Set", _process_RC30Set, false},
    {EMS_MODEL_RC30, EMS_TYPE_RC30StatusMessage, "RC30StatusMessage", _process_RC30StatusMessage, false},

    // RC35
    {EMS_MODEL_RC35, EMS_TYPE_RCOutdoorTempMessage, "RCOutdoorTempMessage", _process_RCOutdoorTempMessage, false},
    {EMS_MODEL_RC35, EMS_TYPE_RCTime, "RCTime", _process_RCTime, false},
    {EMS_MODEL_RC35, EMS_TYPE_RC35Set_HC1, "RC35Set_HC1", _process_RC35Set, false},
    {EMS_MODEL_RC35, EMS_TYPE_RC35StatusMessage_HC1, "RC35StatusMessage_HC1", _process_RC35StatusMessage, false},
    {EMS_MODEL_RC35, EMS_TYPE_RC35Set_HC2, "RC35Set_HC2", _process_RC35Set, false},
    {EMS_MODEL_RC35, EMS_TYPE_RC35StatusMessage_HC2, "RC35StatusMessage_HC2", _process_RC35StatusMessage, false},
    //lobocobra start
    {EMS_MODEL_RC35, EMS_TYPE_AnlageParamSet, "AnlageParamSet", _process_AnlageParamSet, false},
    {EMS_MODEL_RC35, EMS_TYPE_HK2Schaltzeiten, "HK2Schaltzeiten", _process_HK2Schaltzeiten, false},
    //lobocobra end

    // ES73
    {EMS_MODEL_ES73, EMS_TYPE_RCOutdoorTempMessage, "RCOutdoorTempMessage", _process_RCOutdoorTempMessage, false},
    {EMS_MODEL_ES73, EMS_TYPE_RCTime, "RCTime", _process_RCTime, false},
    {EMS_MODEL_ES73, EMS_TYPE_RC35Set_HC1, "RC35Set", _process_RC35Set, false},
    {EMS_MODEL_ES73, EMS_TYPE_RC35StatusMessage_HC1, "RC35StatusMessage", _process_RC35StatusMessage, false},

    // Easy
    {EMS_MODEL_EASY, EMS_TYPE_EasyStatusMessage, "EasyStatusMessage", _process_EasyStatusMessage, false},
    {EMS_MODEL_BOSCHEASY, EMS_TYPE_EasyStatusMessage, "EasyStatusMessage", _process_EasyStatusMessage, false},

    //Ems plus
    //Nefit 1010
    {EMS_MODEL_RC1010, EMS_TYPE_RC1010StatusMessage, "RC1010StatusMessage", _process_RC1010StatusMessage, true},
    {EMS_MODEL_RC1010, EMS_TYPE_RC1010Set, "RC1010SetMessage", _process_RC1010SetMessage, true}

};

// calculate sizes of arrays at compile
uint8_t _EMS_Types_max        = ArraySize(EMS_Types);        // number of defined types
uint8_t _Boiler_Types_max     = ArraySize(Boiler_Types);     // number of boiler models
uint8_t _Other_Types_max      = ArraySize(Other_Types);      // number of other ems devices
uint8_t _Thermostat_Types_max = ArraySize(Thermostat_Types); // number of defined thermostat types

// these structs contain the data we store from the Boiler and Thermostat
_EMS_Boiler     EMS_Boiler;     // for boiler
_EMS_Thermostat EMS_Thermostat; // for thermostat
_EMS_Other      EMS_Other;      // for other known EMS devices

// CRC lookup table with poly 12 for faster checking
const uint8_t ems_crc_table[] = {0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22,
                                 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x40, 0x42, 0x44, 0x46,
                                 0x48, 0x4A, 0x4C, 0x4E, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5A, 0x5C, 0x5E, 0x60, 0x62, 0x64, 0x66, 0x68, 0x6A,
                                 0x6C, 0x6E, 0x70, 0x72, 0x74, 0x76, 0x78, 0x7A, 0x7C, 0x7E, 0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C, 0x8E,
                                 0x90, 0x92, 0x94, 0x96, 0x98, 0x9A, 0x9C, 0x9E, 0xA0, 0xA2, 0xA4, 0xA6, 0xA8, 0xAA, 0xAC, 0xAE, 0xB0, 0xB2,
                                 0xB4, 0xB6, 0xB8, 0xBA, 0xBC, 0xBE, 0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE, 0xD0, 0xD2, 0xD4, 0xD6,
                                 0xD8, 0xDA, 0xDC, 0xDE, 0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE, 0xF0, 0xF2, 0xF4, 0xF6, 0xF8, 0xFA,
                                 0xFC, 0xFE, 0x19, 0x1B, 0x1D, 0x1F, 0x11, 0x13, 0x15, 0x17, 0x09, 0x0B, 0x0D, 0x0F, 0x01, 0x03, 0x05, 0x07,
                                 0x39, 0x3B, 0x3D, 0x3F, 0x31, 0x33, 0x35, 0x37, 0x29, 0x2B, 0x2D, 0x2F, 0x21, 0x23, 0x25, 0x27, 0x59, 0x5B,
                                 0x5D, 0x5F, 0x51, 0x53, 0x55, 0x57, 0x49, 0x4B, 0x4D, 0x4F, 0x41, 0x43, 0x45, 0x47, 0x79, 0x7B, 0x7D, 0x7F,
                                 0x71, 0x73, 0x75, 0x77, 0x69, 0x6B, 0x6D, 0x6F, 0x61, 0x63, 0x65, 0x67, 0x99, 0x9B, 0x9D, 0x9F, 0x91, 0x93,
                                 0x95, 0x97, 0x89, 0x8B, 0x8D, 0x8F, 0x81, 0x83, 0x85, 0x87, 0xB9, 0xBB, 0xBD, 0xBF, 0xB1, 0xB3, 0xB5, 0xB7,
                                 0xA9, 0xAB, 0xAD, 0xAF, 0xA1, 0xA3, 0xA5, 0xA7, 0xD9, 0xDB, 0xDD, 0xDF, 0xD1, 0xD3, 0xD5, 0xD7, 0xC9, 0xCB,
                                 0xCD, 0xCF, 0xC1, 0xC3, 0xC5, 0xC7, 0xF9, 0xFB, 0xFD, 0xFF, 0xF1, 0xF3, 0xF5, 0xF7, 0xE9, 0xEB, 0xED, 0xEF,
                                 0xE1, 0xE3, 0xE5, 0xE7};

const uint8_t  TX_WRITE_TIMEOUT_COUNT = 2;     // 3 retries before timeout
const uint32_t EMS_BUS_TIMEOUT        = 15000; // timeout in ms before recognizing the ems bus is offline (15 seconds)
const uint32_t EMS_POLL_TIMEOUT       = 5000;  // timeout in ms before recognizing the ems bus is offline (5 seconds)

// init stats and counters and buffers
// uses -255 or 255 for values that haven't been set yet (EMS_VALUE_INT_NOTSET and EMS_VALUE_FLOAT_NOTSET)
void ems_init() {
    // overall status
    EMS_Sys_Status.emsRxPgks        = 0;
    EMS_Sys_Status.emsTxPkgs        = 0;
    EMS_Sys_Status.emxCrcErr        = 0;
    EMS_Sys_Status.emsRxStatus      = EMS_RX_STATUS_IDLE;
    EMS_Sys_Status.emsTxStatus      = EMS_TX_STATUS_IDLE;
    EMS_Sys_Status.emsRefreshed     = false;
    EMS_Sys_Status.emsPollEnabled   = false; // start up with Poll disabled
    EMS_Sys_Status.emsBusConnected  = false;
    EMS_Sys_Status.emsRxTimestamp   = 0;
    EMS_Sys_Status.emsTxCapable     = false;
    EMS_Sys_Status.emsTxDisabled    = false;
    EMS_Sys_Status.emsPollFrequency = 0;
    EMS_Sys_Status.txRetryCount     = 0;

    // thermostat
    EMS_Thermostat.setpoint_roomTemp = EMS_VALUE_SHORT_NOTSET;
    EMS_Thermostat.curr_roomTemp     = EMS_VALUE_SHORT_NOTSET;
    EMS_Thermostat.hour              = 0;
    EMS_Thermostat.minute            = 0;
    EMS_Thermostat.second            = 0;
    EMS_Thermostat.day               = 0;
    EMS_Thermostat.month             = 0;
    EMS_Thermostat.year              = 0;
    EMS_Thermostat.mode              = 255; // dummy value
    EMS_Thermostat.day_mode          = 255; // dummy value
    EMS_Thermostat.type_id           = EMS_ID_NONE;
    EMS_Thermostat.read_supported    = false;
    EMS_Thermostat.write_supported   = false;
    EMS_Thermostat.hc                = 1; 
    EMS_Thermostat.daytemp           = EMS_VALUE_INT_NOTSET; // 0x47 byte
    EMS_Thermostat.nighttemp         = EMS_VALUE_INT_NOTSET; // 0x47 byte
    EMS_Thermostat.holidaytemp       = EMS_VALUE_INT_NOTSET; // 0x47 byte
    EMS_Thermostat.heatingtype       = EMS_VALUE_INT_NOTSET; // 0x47 byte floor heating = 3
    EMS_Thermostat.circuitcalctemp   = EMS_VALUE_INT_NOTSET; // 0x48 byte 14
    //lobocobra start
    EMS_Thermostat.ausschalthysterese   = EMS_VALUE_INT_NOTSET; // positive value heating off when above x°
    EMS_Thermostat.einschalthysterese   = 196; // negative value heating on when below x° 
    EMS_Thermostat.antipendelzeit       = EMS_VALUE_INT_NOTSET; // time between 2 starts
    EMS_Thermostat.kesselpumennachlauf  = EMS_VALUE_INT_NOTSET; // pump running longer
    EMS_Thermostat.auslegungstemp       = EMS_VALUE_INT_NOTSET; // temp at minimum temp 
    EMS_Thermostat.maxvorlauf           = EMS_VALUE_INT_NOTSET; // max temp in heating circuit
    EMS_Thermostat.heizturbo_till_next  = EMS_VALUE_INT_NOTSET; // new temp till next change
    EMS_Thermostat.roomoffset           = 236; // offset can not be 255 as this would represent -0.5 so I choose 236 as this is -20° a
    EMS_Thermostat.minoutsidetemp       = 196; // minimum temp in region -30 = senseless input
    EMS_Thermostat.housetype            = EMS_VALUE_INT_NOTSET; // light medium heavy
    EMS_Thermostat.tempaveragebool      = false; // activate averaging the temps
    EMS_Thermostat.pausezeit            = EMS_VALUE_INT_NOTSET; // light medium heavy
    EMS_Thermostat.partyzeit            = EMS_VALUE_INT_NOTSET; // light medium heavy
    EMS_Thermostat.max_vorlauf_reached  = 0; 
    EMS_Thermostat.urlaub_modus         = 0;
    EMS_Thermostat.sommer_modus         = 0;

    //lobocobra end



    // UBAParameterWW
    EMS_Boiler.wWActivated   = EMS_VALUE_INT_NOTSET; // Warm Water activated
    EMS_Boiler.wWSelTemp     = EMS_VALUE_INT_NOTSET; // Warm Water selected temperature
    EMS_Boiler.wWCircPump    = EMS_VALUE_INT_NOTSET; // Warm Water circulation pump available
    EMS_Boiler.wWDesiredTemp = EMS_VALUE_INT_NOTSET; // Warm Water desired temperature to prevent infection
    EMS_Boiler.wWComfort     = EMS_VALUE_INT_NOTSET;

    // UBAMonitorFast
    EMS_Boiler.selFlowTemp = EMS_VALUE_INT_NOTSET;   // Selected flow temperature
    EMS_Boiler.curFlowTemp = EMS_VALUE_SHORT_NOTSET; // Current flow temperature
    EMS_Boiler.retTemp     = EMS_VALUE_SHORT_NOTSET; // Return temperature
    EMS_Boiler.burnGas     = EMS_VALUE_INT_NOTSET;   // Gas on/off
    EMS_Boiler.fanWork     = EMS_VALUE_INT_NOTSET;   // Fan on/off
    EMS_Boiler.ignWork     = EMS_VALUE_INT_NOTSET;   // Ignition on/off
    EMS_Boiler.heatPmp     = EMS_VALUE_INT_NOTSET;   // Boiler pump on/off
    EMS_Boiler.wWHeat      = EMS_VALUE_INT_NOTSET;   // 3-way valve on WW
    EMS_Boiler.wWCirc      = EMS_VALUE_INT_NOTSET;   // Circulation on/off
    EMS_Boiler.selBurnPow  = EMS_VALUE_INT_NOTSET;   // Burner max power
    EMS_Boiler.curBurnPow  = EMS_VALUE_INT_NOTSET;   // Burner current power
    EMS_Boiler.flameCurr   = EMS_VALUE_SHORT_NOTSET; // Flame current in micro amps
    EMS_Boiler.sysPress    = EMS_VALUE_INT_NOTSET;   // System pressure
    // lobocobra start initialize value
    // EMS_Boiler.airInflow  = EMS_VALUE_SHORT_NOTSET; // air inflow temp nicht vorhanden = 8300 bei GB125
    // lobocobra end
    strlcpy(EMS_Boiler.serviceCodeChar, "??", sizeof(EMS_Boiler.serviceCodeChar));
    EMS_Boiler.serviceCode = EMS_VALUE_SHORT_NOTSET;

    // UBAMonitorSlow
    EMS_Boiler.extTemp     = EMS_VALUE_SHORT_NOTSET; // Outside temperature
    //lobocobra start
    EMS_Boiler.abgasTemp   = EMS_VALUE_SHORT_NOTSET; // Abgas temperature
    //lobocobra end
    EMS_Boiler.boilTemp    = EMS_VALUE_SHORT_NOTSET; // Boiler temperature
    EMS_Boiler.pumpMod     = EMS_VALUE_INT_NOTSET;   // Pump modulation
    EMS_Boiler.burnStarts  = EMS_VALUE_LONG_NOTSET;  // # burner restarts
    EMS_Boiler.burnWorkMin = EMS_VALUE_LONG_NOTSET;  // Total burner operating time
    EMS_Boiler.heatWorkMin = EMS_VALUE_LONG_NOTSET;  // Total heat operating time

    // UBAMonitorWWMessage
    EMS_Boiler.wWCurTmp  = EMS_VALUE_SHORT_NOTSET; // Warm Water current temperature:
    EMS_Boiler.wWStarts  = EMS_VALUE_LONG_NOTSET;  // Warm Water # starts
    EMS_Boiler.wWWorkM   = EMS_VALUE_LONG_NOTSET;  // Warm Water # minutes
    EMS_Boiler.wWOneTime = EMS_VALUE_INT_NOTSET;   // Warm Water one time function on/off
    EMS_Boiler.wWCurFlow = EMS_VALUE_INT_NOTSET;

    // UBATotalUptimeMessage
    EMS_Boiler.UBAuptime = EMS_VALUE_LONG_NOTSET; // Total UBA working hours

    // UBAParametersMessage
    EMS_Boiler.heating_temp = EMS_VALUE_INT_NOTSET; // Heating temperature setting on the boiler
    EMS_Boiler.pump_mod_max = EMS_VALUE_INT_NOTSET; // Boiler circuit pump modulation max. power
    EMS_Boiler.pump_mod_min = EMS_VALUE_INT_NOTSET; // Boiler circuit pump modulation min. power

    // Other EMS devices values
    EMS_Other.SM10collectorTemp  = EMS_VALUE_SHORT_NOTSET; // collector temp from SM10
    EMS_Other.SM10bottomTemp     = EMS_VALUE_SHORT_NOTSET; // bottom temp from SM10
    EMS_Other.SM10pumpModulation = EMS_VALUE_INT_NOTSET;   // modulation solar pump SM10
    EMS_Other.SM10pump           = EMS_VALUE_INT_NOTSET;   // pump active

    // calculated values
    EMS_Boiler.tapwaterActive = EMS_VALUE_INT_NOTSET; // Hot tap water is on/off
    EMS_Boiler.heatingActive  = EMS_VALUE_INT_NOTSET; // Central heating is on/off

    // set boiler type
    EMS_Boiler.product_id = EMS_ID_NONE;
    strlcpy(EMS_Boiler.version, "?", sizeof(EMS_Boiler.version));

    // set thermostat model
    EMS_Thermostat.model_id   = EMS_MODEL_NONE;
    EMS_Thermostat.product_id = EMS_ID_NONE;
    strlcpy(EMS_Thermostat.version, "?", sizeof(EMS_Thermostat.version));

    // set other types
    EMS_Other.SM10 = false;

    // default logging is none
    ems_setLogging(EMS_SYS_LOGGING_DEFAULT);
}

// Getters and Setters for parameters
void ems_setPoll(bool b) {
    EMS_Sys_Status.emsPollEnabled = b;
    myDebug("EMS Bus Poll is set to %s", EMS_Sys_Status.emsPollEnabled ? "enabled" : "disabled");
}

bool ems_getPoll() {
    return EMS_Sys_Status.emsPollEnabled;
}

bool ems_getEmsRefreshed() {
    return EMS_Sys_Status.emsRefreshed;
}

void ems_setEmsRefreshed(bool b) {
    EMS_Sys_Status.emsRefreshed = b;
}

void ems_setThermostatHC(uint8_t hc) {
    EMS_Thermostat.hc = hc;
}

bool ems_getBoilerEnabled() {
    return (EMS_Boiler.type_id != EMS_ID_NONE);
}

bool ems_getThermostatEnabled() {
    return (EMS_Thermostat.type_id != EMS_ID_NONE);
}

uint8_t ems_getThermostatModel() {
    return (EMS_Thermostat.model_id);
}

void ems_setTxDisabled(bool b) {
    EMS_Sys_Status.emsTxDisabled = b;
}

uint32_t ems_getPollFrequency() {
    return EMS_Sys_Status.emsPollFrequency;
}

bool ems_getTxCapable() {
    if ((EMS_Sys_Status.emsPollFrequency == 0) || (EMS_Sys_Status.emsPollFrequency > EMS_POLL_TIMEOUT)) {
        EMS_Sys_Status.emsTxCapable = false;
    }
    return EMS_Sys_Status.emsTxCapable;
}

bool ems_getBusConnected() {
    if ((millis() - EMS_Sys_Status.emsRxTimestamp) > EMS_BUS_TIMEOUT) {
        EMS_Sys_Status.emsBusConnected = false;
    }
    return EMS_Sys_Status.emsBusConnected;
}

_EMS_SYS_LOGGING ems_getLogging() {
    return EMS_Sys_Status.emsLogging;
}

void ems_setLogging(_EMS_SYS_LOGGING loglevel) {
    if (loglevel <= EMS_SYS_LOGGING_VERBOSE) {
        EMS_Sys_Status.emsLogging = loglevel;
        if (loglevel == EMS_SYS_LOGGING_NONE) {
            myDebug("System Logging set to None");
        } else if (loglevel == EMS_SYS_LOGGING_BASIC) {
            myDebug("System Logging set to Basic");
        } else if (loglevel == EMS_SYS_LOGGING_VERBOSE) {
            myDebug("System Logging set to Verbose");
        } else if (loglevel == EMS_SYS_LOGGING_THERMOSTAT) {
            myDebug("System Logging set to Thermostat only");
        } else if (loglevel == EMS_SYS_LOGGING_RAW) {
            myDebug("System Logging set to Raw mode");
        }
    }
}

/**
 * Calculate CRC checksum using lookup table for speed
 * len is length of data in bytes (including the CRC byte at end)
 * So its the complete telegram with the header
 */
uint8_t _crcCalculator(uint8_t * data, uint8_t len) {
    uint8_t crc = 0;

    // read data and stop before the CRC
    for (uint8_t i = 0; i < len - 1; i++) {
        crc = ems_crc_table[crc];
        crc ^= data[i];
    }

    return crc;
}

// like itoa but for hex, and quicker
char * _hextoa(uint8_t value, char * buffer) {
    char * p    = buffer;
    byte   nib1 = (value >> 4) & 0x0F;
    byte   nib2 = (value >> 0) & 0x0F;
    *p++        = nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
    *p++        = nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
    *p          = '\0'; // null terminate just in case
    return buffer;
}

// for decimals 0 to 99, printed as a string
char * _smallitoa(uint8_t value, char * buffer) {
    buffer[0] = ((value / 10) == 0) ? '0' : (value / 10) + '0';
    buffer[1] = (value % 10) + '0';
    buffer[2] = '\0';
    return buffer;
}

/* for decimals 0 to 999, printed as a string
 * From @nomis
 */
char * _smallitoa3(uint16_t value, char * buffer) {
    buffer[0] = ((value / 100) == 0) ? '0' : (value / 100) + '0';
    buffer[1] = (((value % 100) / 10) == 0) ? '0' : ((value % 100) / 10) + '0';
    buffer[2] = (value % 10) + '0';
    buffer[3] = '\0';
    return buffer;
}

/**
 * Find the pointer to the EMS_Types array for a given type ID
 * or -1 if not found
 */
int _ems_findType(uint8_t type) {
    uint8_t i         = 0;
    bool    typeFound = false;
    // scan through known ID types
    while (i < _EMS_Types_max) {
        if (EMS_Types[i].type == type) {
            typeFound = true; // we have a match
            break;
        }
        i++;
    }

    return (typeFound ? i : -1);
}

/**
 * debug print a telegram to telnet/serial including the CRC
 * len is length in bytes including the CRC
 */
void _debugPrintTelegram(const char * prefix, _EMS_RxTelegram * EMS_RxTelegram, const char * color) {
    if (EMS_Sys_Status.emsLogging <= EMS_SYS_LOGGING_BASIC)
        return;

    char      output_str[200] = {0};
    char      buffer[16]      = {0};
    uint8_t   len             = EMS_RxTelegram->length;
    uint8_t * data            = EMS_RxTelegram->telegram;

    strlcpy(output_str, "(", sizeof(output_str));
    strlcat(output_str, COLOR_CYAN, sizeof(output_str));
    strlcat(output_str, _smallitoa((uint8_t)((EMS_RxTelegram->timestamp / 3600000) % 24), buffer), sizeof(output_str));
    strlcat(output_str, ":", sizeof(output_str));
    strlcat(output_str, _smallitoa((uint8_t)((EMS_RxTelegram->timestamp / 60000) % 60), buffer), sizeof(output_str));
    strlcat(output_str, ":", sizeof(output_str));
    strlcat(output_str, _smallitoa((uint8_t)((EMS_RxTelegram->timestamp / 1000) % 60), buffer), sizeof(output_str));
    strlcat(output_str, ".", sizeof(output_str));
    strlcat(output_str, _smallitoa3(EMS_RxTelegram->timestamp % 1000, buffer), sizeof(output_str));
    strlcat(output_str, COLOR_RESET, sizeof(output_str));
    strlcat(output_str, ") ", sizeof(output_str));

    strlcat(output_str, color, sizeof(output_str));
    strlcat(output_str, prefix, sizeof(output_str));
    strlcat(output_str, " telegram: ", sizeof(output_str));

    for (int i = 0; i < len - 1; i++) {
        strlcat(output_str, _hextoa(data[i], buffer), sizeof(output_str));
        strlcat(output_str, " ", sizeof(output_str)); // add space
    }

    strlcat(output_str, "(CRC=", sizeof(output_str));
    strlcat(output_str, _hextoa(data[len - 1], buffer), sizeof(output_str));
    strlcat(output_str, ")", sizeof(output_str));

    // print number of data bytes only if its a valid telegram
    if (len > 5) {
        strlcat(output_str, ", #data=", sizeof(output_str));
        strlcat(output_str, itoa(len - 5, buffer, 10), sizeof(output_str));
    }

    strlcat(output_str, COLOR_RESET, sizeof(output_str));

    myDebug(output_str);
}

/**
 * send the contents of the Tx buffer to the UART
 * we take telegram from the queue and send it, but don't remove it until later when its confirmed successful
 */
void _ems_sendTelegram() {
    // check if we have something in the queue to send
    if (EMS_TxQueue.isEmpty()) {
        return;
    }

    // get the first in the queue, which is at the head
    // we don't remove from the queue yet
    _EMS_TxTelegram EMS_TxTelegram = EMS_TxQueue.first();

    // if there is no destination, also delete it from the queue
    if (EMS_TxTelegram.dest == EMS_ID_NONE) {
        EMS_TxQueue.shift(); // remove from queue
        return;
    }


    // if we're in raw mode just fire and forget
    if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_RAW) {
        EMS_TxTelegram.data[EMS_TxTelegram.length - 1] = _crcCalculator(EMS_TxTelegram.data, EMS_TxTelegram.length); // add the CRC
        _EMS_RxTelegram EMS_RxTelegram;
        EMS_RxTelegram.length    = EMS_TxTelegram.length;
        EMS_RxTelegram.telegram  = EMS_TxTelegram.data;
        EMS_RxTelegram.timestamp = millis();                             // now
        _debugPrintTelegram("Sending raw", &EMS_RxTelegram, COLOR_CYAN); // always show
        emsuart_tx_buffer(EMS_TxTelegram.data, EMS_TxTelegram.length);   // send the telegram to the UART Tx
        EMS_TxQueue.shift();                                             // remove from queue
        return;
    }

    // create header
    EMS_TxTelegram.data[0] = EMS_ID_ME; // src
    // dest
    if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_WRITE) {
        EMS_TxTelegram.data[1] = EMS_TxTelegram.dest;
    } else {
        // for a READ or VALIDATE
        EMS_TxTelegram.data[1] = EMS_TxTelegram.dest | 0x80; // read has 8th bit set
    }
    EMS_TxTelegram.data[2] = EMS_TxTelegram.type;   // type
    EMS_TxTelegram.data[3] = EMS_TxTelegram.offset; // offset

    // see if it has data, add the single data value byte
    // otherwise leave it alone and assume the data has been pre-populated
    if (EMS_TxTelegram.length == EMS_MIN_TELEGRAM_LENGTH) {
        // for reading this is #bytes we want to read (the size)
        // for writing its the value we want to write
        EMS_TxTelegram.data[4] = EMS_TxTelegram.dataValue;
    }
    // finally calculate CRC and add it to the end
    uint8_t crc                                    = _crcCalculator(EMS_TxTelegram.data, EMS_TxTelegram.length);
    EMS_TxTelegram.data[EMS_TxTelegram.length - 1] = crc;

    // print debug info
    if (EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_VERBOSE) {
        char s[64] = {0};
        if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_WRITE) {
            snprintf(s, sizeof(s), "Sending write of type 0x%02X to 0x%02X:", EMS_TxTelegram.type, EMS_TxTelegram.dest & 0x7F);
        } else if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_READ) {
            snprintf(s, sizeof(s), "Sending read of type 0x%02X to 0x%02X:", EMS_TxTelegram.type, EMS_TxTelegram.dest & 0x7F);
        } else if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_VALIDATE) {
            snprintf(s, sizeof(s), "Sending validate of type 0x%02X to 0x%02X:", EMS_TxTelegram.type, EMS_TxTelegram.dest & 0x7F);
        }

        _EMS_RxTelegram EMS_RxTelegram;
        EMS_RxTelegram.length    = EMS_TxTelegram.length;
        EMS_RxTelegram.telegram  = EMS_TxTelegram.data;
        EMS_RxTelegram.timestamp = millis(); // now
        _debugPrintTelegram(s, &EMS_RxTelegram, COLOR_CYAN);
    }

    // send the telegram to the UART Tx
    emsuart_tx_buffer(EMS_TxTelegram.data, EMS_TxTelegram.length);

    EMS_Sys_Status.emsTxStatus = EMS_TX_STATUS_WAIT;
}


/**
 * Takes the last write command and turns into a validate request
 * placing it on the queue
 */
void _createValidate() {
    if (EMS_TxQueue.isEmpty()) {
        return;
    }

    // release the Tx lock
    EMS_Sys_Status.emsTxStatus = EMS_TX_STATUS_IDLE;

    // get the first in the queue, which is at the head
    _EMS_TxTelegram EMS_TxTelegram = EMS_TxQueue.first();

    // safety check: only do a validate after a write and when we have a type to validate
    if ((EMS_TxTelegram.action != EMS_TX_TELEGRAM_WRITE) || (EMS_TxTelegram.type_validate == EMS_ID_NONE)) {
        EMS_TxQueue.shift(); // remove from queue
        return;
    }

    // create a new Telegram copying from the last write
    _EMS_TxTelegram new_EMS_TxTelegram;
    new_EMS_TxTelegram.action = EMS_TX_TELEGRAM_VALIDATE;

    // copy old Write record
    new_EMS_TxTelegram.type_validate      = EMS_TxTelegram.type_validate;
    new_EMS_TxTelegram.dest               = EMS_TxTelegram.dest;
    new_EMS_TxTelegram.type               = EMS_TxTelegram.type;
    new_EMS_TxTelegram.comparisonValue    = EMS_TxTelegram.comparisonValue;
    new_EMS_TxTelegram.comparisonPostRead = EMS_TxTelegram.comparisonPostRead;
    new_EMS_TxTelegram.comparisonOffset   = EMS_TxTelegram.comparisonOffset;

    // this is what is different
    new_EMS_TxTelegram.offset    = EMS_TxTelegram.comparisonOffset; // location of byte to fetch
    new_EMS_TxTelegram.dataValue = 1;                               // fetch single byte
    new_EMS_TxTelegram.length    = EMS_MIN_TELEGRAM_LENGTH;         // is always 6 bytes long (including CRC at end)

    // remove old telegram from queue and add this new read one
    EMS_TxQueue.shift();                     // remove from queue
    EMS_TxQueue.unshift(new_EMS_TxTelegram); // add back to queue making it first to be picked up next (FIFO)
}

/*
 * Entry point triggered by an interrupt in emsuart.cpp
 * length is only data bytes, excluding the BRK
 * Read commands are asynchronous as they're handled by the interrupt
 * When a telegram is processed we forcefully erase it from the stack to prevent overflow
 */
void ems_parseTelegram(uint8_t * telegram, uint8_t length) {
    if ((length != 0) && (telegram[0] != 0x00)) {
        _ems_readTelegram(telegram, length);
    }

    // clear the Rx buffer just be safe and prevent duplicates
    memset(telegram, 0, EMS_MAXBUFFERSIZE);
}

/**
 * the main logic that parses the telegram message
 * When we receive a Poll Request we need to send any Tx packages quickly within a 200ms window
 */
void _ems_readTelegram(uint8_t * telegram, uint8_t length) {
    // create the Rx package
    static _EMS_RxTelegram EMS_RxTelegram;
    static uint32_t        _last_emsPollFrequency = 0;
    EMS_RxTelegram.length                         = length;
    EMS_RxTelegram.telegram                       = telegram;
    EMS_RxTelegram.timestamp                      = millis();

    // check if we just received a single byte
    // it could well be a Poll request from the boiler for us, which will have a value of 0x8B (0x0B | 0x80)
    // or either a return code like 0x01 or 0x04 from the last Write command
    if (length == 1) {
        uint8_t value = telegram[0]; // 1st byte of data package

        EMS_Sys_Status.emsPollFrequency = (EMS_RxTelegram.timestamp - _last_emsPollFrequency);
        _last_emsPollFrequency          = EMS_RxTelegram.timestamp;

        // check first for a Poll for us
        if (value == (EMS_ID_ME | 0x80)) {
            EMS_Sys_Status.emsTxCapable = true;

            // do we have something to send thats waiting in the Tx queue?
            // if so send it if the Queue is not in a wait state
            if ((!EMS_TxQueue.isEmpty()) && (EMS_Sys_Status.emsTxStatus == EMS_TX_STATUS_IDLE)) {
                _ems_sendTelegram(); // perform the read/write command immediately
            } else {
                // nothing to send so just send a poll acknowledgement back
                if (EMS_Sys_Status.emsPollEnabled) {
                    emsaurt_tx_poll();
                }
            }
        } else if (EMS_Sys_Status.emsTxStatus == EMS_TX_STATUS_WAIT) {
            // this may be a single byte 01 (success) or 04 (error) from a recent write command?
            if (value == EMS_TX_SUCCESS) {
                EMS_Sys_Status.emsTxPkgs++;
                // got a success 01. Send a validate to check the value of the last write
                emsaurt_tx_poll(); // send a poll to free the EMS bus
                _createValidate(); // create a validate Tx request (if needed)
            } else if (value == EMS_TX_ERROR) {
                // last write failed (04), delete it from queue and dont bother to retry
                if (EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_VERBOSE) {
                    myDebug("** Write command failed from host");
                }
                emsaurt_tx_poll(); // send a poll to free the EMS bus
                _removeTxQueue();  // remove from queue
            }
        }

        return; // all done here
    }

    // ignore anything that doesn't resemble a proper telegram package
    // minimal is 5 bytes, excluding CRC at the end
    if (length <= 4) {
        //_debugPrintTelegram("Noisy data:", &EMS_RxTelegram COLOR_RED);
        return;
    }

    // Assume at this point we have something that vaguely resembles a telegram in the format [src] [dest] [type] [offset] [data] [crc]
    // validate the CRC, if its bad ignore it
    uint8_t crc = _crcCalculator(telegram, length);
    if (telegram[length - 1] != crc) {
        EMS_Sys_Status.emxCrcErr++;
        if (EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_VERBOSE) {
            _debugPrintTelegram("Corrupt telegram:", &EMS_RxTelegram, COLOR_RED);
        }
        return;
    }

    // if we are in raw logging mode then just print out the telegram as it is
    // but still continue to process it
    if (EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_RAW) {
        char raw[300]   = {0};
        char buffer[16] = {0};
        for (int i = 0; i < length; i++) {
            strlcat(raw, _hextoa(telegram[i], buffer), sizeof(raw));
            strlcat(raw, " ", sizeof(raw)); // add space
        }
        myDebug(raw);
    }

    // here we know its a valid incoming telegram of at least 6 bytes
    // we use this to see if we always have a connection to the boiler, in case of drop outs
    EMS_Sys_Status.emsRxTimestamp  = EMS_RxTelegram.timestamp; // timestamp of last read
    EMS_Sys_Status.emsBusConnected = true;
//lobocobra info check incoming telegram
    // now lets process it and see what to do next

    //lobocobra info here we have the lenght of the telegram.... if we need it longer... hack here if it is 20hex or 32
    //    myDebug("<<<<<<<<<<<<<<<<<<<length: %d data %s",EMS_RxTelegram.length,EMS_RxTelegram.telegram);
    //lobocobra end
    _processType(&EMS_RxTelegram);
}

/*
 * print the telegram
 */
void _printMessage(_EMS_RxTelegram * EMS_RxTelegram) {
    uint8_t * telegram = EMS_RxTelegram->telegram;

    // header info
    uint8_t   src  = telegram[0] & 0x7F;
    uint8_t   dest = telegram[1] & 0x7F; // remove 8th bit to handle both reads and writes
    uint8_t   type;
    bool      emsp;

    // check if EMS or EMS+ by checking 3rd byte of telegram
    if (telegram[2] >= 0xF0) {
        // EMS+
        type   = telegram[3];
        emsp   = true;
    } else {
        type   = telegram[2];
        emsp   = false;
    }

    char output_str[200] = {0};
    char buffer[16]      = {0};
    char color_s[20]     = {0};

    // source
    if (src == EMS_Boiler.type_id) {
        strlcpy(output_str, "Boiler", sizeof(output_str));
    } else if (src == EMS_Thermostat.type_id) {
        if (emsp)
            strlcpy(output_str, "Thermostat+", sizeof(output_str));
        else
            strlcpy(output_str, "Thermostat", sizeof(output_str));
    } else {
        strlcpy(output_str, "0x", sizeof(output_str));
        strlcat(output_str, _hextoa(src, buffer), sizeof(output_str));
    }

    strlcat(output_str, " -> ", sizeof(output_str));

    // destination
    if (dest == EMS_ID_ME) {
        if (emsp) {
            strlcat(output_str, "me", sizeof(output_str));
            strlcpy(color_s, COLOR_BRIGHT_YELLOW, sizeof(color_s));
        } else {
            strlcat(output_str, "me", sizeof(output_str));
            strlcpy(color_s, COLOR_YELLOW, sizeof(color_s));
        }
    } else if (dest == EMS_ID_NONE) {
        if (emsp) {
            strlcat(output_str, "all", sizeof(output_str));
            strlcpy(color_s, COLOR_BRIGHT_GREEN, sizeof(color_s));
        } else {
            strlcat(output_str, "all", sizeof(output_str));
            strlcpy(color_s, COLOR_GREEN, sizeof(color_s));
        }
    } else if (dest == EMS_Boiler.type_id) {
        if (emsp) {
            strlcat(output_str, "Boiler+", sizeof(output_str));
            strlcpy(color_s, COLOR_BRIGHT_MAGENTA, sizeof(color_s));
        } else {
            strlcat(output_str, "Boiler", sizeof(output_str));
            strlcpy(color_s, COLOR_MAGENTA, sizeof(color_s));
        }
    } else if (dest == EMS_ID_SM10) {
        strlcat(output_str, "SM10", sizeof(output_str));
        strlcpy(color_s, COLOR_MAGENTA, sizeof(color_s));
    } else if (dest == EMS_Thermostat.type_id) {
        if (emsp) {
            strlcat(output_str, "Thermostat+", sizeof(output_str));
            strlcpy(color_s, COLOR_BRIGHT_MAGENTA, sizeof(color_s));
        } else {
            strlcat(output_str, "Thermostat", sizeof(output_str));
            strlcpy(color_s, COLOR_MAGENTA, sizeof(color_s));
        }
    } else {
        if (emsp) {
            strlcat(output_str, "0x", sizeof(output_str));
            strlcat(output_str, _hextoa(dest, buffer), sizeof(output_str));
            strlcpy(color_s, COLOR_BRIGHT_MAGENTA, sizeof(color_s));
        } else {
            strlcat(output_str, "0x", sizeof(output_str));
            strlcat(output_str, _hextoa(dest, buffer), sizeof(output_str));
            strlcpy(color_s, COLOR_MAGENTA, sizeof(color_s));
        }
    }

    // type
    strlcat(output_str, ", type 0x", sizeof(output_str));
    strlcat(output_str, _hextoa(type, buffer), sizeof(output_str));

    if (EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_THERMOSTAT) {
        // only print ones to/from thermostat if logging is set to thermostat only
        if ((src == EMS_Thermostat.type_id) || (dest == EMS_Thermostat.type_id)) {
            _debugPrintTelegram(output_str, EMS_RxTelegram, color_s);
        }
    } else {
        // always print
        _debugPrintTelegram(output_str, EMS_RxTelegram, color_s);
    }
}

/**
 * print detailed telegram
 * and then call its callback if there is one defined
 */
void _ems_processTelegram(_EMS_RxTelegram * EMS_RxTelegram) {
    // header
    uint8_t * telegram = EMS_RxTelegram->telegram;
    uint8_t   length   = EMS_RxTelegram->length;
    uint8_t   src      = telegram[0] & 0x7F;
    uint8_t   type     = telegram[2];
    uint8_t   offset   = telegram[3];
    uint8_t * data     = &telegram[4]; // data block starts at position 4

    // EMS Plus support
    uint8_t   ptype   = telegram[3];
    uint8_t   poffset = telegram[4];
    uint8_t * pdata   = &telegram[5 + poffset]; // data block starts at position 5 plus the offset

    // print out the telegram
    if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_THERMOSTAT) {
        _printMessage(EMS_RxTelegram);
    }

    // see if we recognize the type first by scanning our known EMS types list
    bool    typeFound = false;
    uint8_t i         = 0;

    while (i < _EMS_Types_max) {
        if (EMS_Types[i].type == type) {
            // is it common type for everyone?
            // is it for us? So the src must match with either the boiler, thermostat or other devices
            if ((EMS_Types[i].model_id == EMS_MODEL_ALL)
                || ((src == EMS_Boiler.type_id) || (src == EMS_Thermostat.type_id) || (src == EMS_ID_SM10))) { //lobocobra
                typeFound = true;
                break;
            }
        }
        i++;
    }
    //myDebug("TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT OFFSET %d type: %d src:%d", offset,type,src); //lobocobra info
    if ( src == 16 && type == 73 && offset == 85) { // lobocobra, ok we get the 0x49... handle it
    //lobocobra start
    EMS_Thermostat.pausezeit  = _toByte(0); //send 0b 90 49 55 01 (pos 0 as we read from 55)
    EMS_Thermostat.partyzeit  = _toByte(1); //send 0b 90 49 56 01 (pos 1 as we read from 55)
    //lobocobra end
    EMS_Sys_Status.emsTxStatus = EMS_TX_STATUS_IDLE;  
    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
    return;
    }

    if ( src == 16 && type == 71 && offset == 22) { // lobocobra, ok we get the 2nd part of 0x47... handle it
    EMS_Thermostat.maxvorlauf         = _toByte(13); // read max temp temp send 0b 90 47 23 01 !!RC35 other offset
    EMS_Thermostat.auslegungstemp     = _toByte(14); // read max temp at min outside temp send 0b 90 47 24 01 !!RC36 other offset
    EMS_Thermostat.heizturbo_till_next =_toByte(15); // read max temp temp send 0b 90 47 25 01 !!RC35 other offset x2
    EMS_Sys_Status.emsTxStatus = EMS_TX_STATUS_IDLE;  
    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
    return; // we exit as we are done
    }
    // if it's a common type (across ems devices) or something specifically for us process it.
    // dest will be EMS_ID_NONE and offset 0x00 for a broadcast message
    if (typeFound) {
        if ((EMS_Types[i].processType_cb) != (void *)NULL) {
            // print non-verbose message
            if ((EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_BASIC) || (EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_VERBOSE)) {
                if (EMS_Types[i].emsplus)
                    myDebug("<--- %s(0x%02X) received", EMS_Types[i].typeString, ptype);
                else
                    myDebug("<--- %s(0x%02X) received", EMS_Types[i].typeString, type);
            }
            // call callback function to process it
            if (EMS_Types[i].emsplus && poffset == EMS_PLUS_ID_NONE)
                (void)EMS_Types[i].processType_cb(ptype, pdata, length - 6 - poffset);
            // as we only handle complete telegrams (not partial) check that the offset is 0
            else if (offset == EMS_ID_NONE && !EMS_Types[i].emsplus) { 
                (void)EMS_Types[i].processType_cb(type, data, length - 5);
            }
        }
    }

    EMS_Sys_Status.emsTxStatus = EMS_TX_STATUS_IDLE;
}

/**
 * Remove current Tx telegram from queue and release lock on Tx
 */
void _removeTxQueue() {
    if (!EMS_TxQueue.isEmpty()) {
        EMS_TxQueue.shift(); // remove item from top of the queue
    }
    EMS_Sys_Status.emsTxStatus = EMS_TX_STATUS_IDLE;
}

/**
 * deciphers the telegram packet, which has already been checked for valid CRC and has a complete header (min of 5 bytes)
 * length is only data bytes, excluding the BRK
 * We only remove from the Tx queue if the read or write was successful
 */
void _processType(_EMS_RxTelegram * EMS_RxTelegram) {
    uint8_t * telegram = EMS_RxTelegram->telegram;

    // header
    uint8_t src = telegram[0] & 0x7F; // removing 8th bit as we deal with both reads and writes here

    // if its an echo of ourselves from the master UBA, ignore
    if (src == EMS_ID_ME) {
        // _debugPrintTelegram("echo:", EMS_RxTelegram, COLOR_WHITE);
        return;
    }

    // if its a broadcast and we didn't just send anything, process it and exit
    if (EMS_Sys_Status.emsTxStatus == EMS_TX_STATUS_IDLE) {
        _ems_processTelegram(EMS_RxTelegram);
        return;
    }

    // release the lock on the TxQueue
    EMS_Sys_Status.emsTxStatus = EMS_TX_STATUS_IDLE;

    // at this point we can assume Txstatus is EMS_TX_STATUS_WAIT so we just sent a read/write/validate
    // for READ, WRITE or VALIDATE the dest (telegram[1]) is always us, so check for this
    // and if not we probably didn't get any response so remove the last Tx from the queue and process the telegram anyway
    if ((telegram[1] & 0x7F) != EMS_ID_ME) {
        _removeTxQueue();
        _ems_processTelegram(EMS_RxTelegram);
        return;
    }

    // first double check we actually have something in the queue
    if (EMS_TxQueue.isEmpty()) {
        _ems_processTelegram(EMS_RxTelegram);
        return;
    }

    // get the Tx telegram we just sent
    _EMS_TxTelegram EMS_TxTelegram = EMS_TxQueue.first();

    // check action
    // if READ, match the current inbound telegram to what we sent
    // if WRITE, should not happen
    // if VALIDATE, check the contents
    if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_READ) {
        uint8_t type = telegram[2];
        if ((src == EMS_TxTelegram.dest) && (type == EMS_TxTelegram.type)) {
            // all checks out, read was successful, remove tx from queue and continue to process telegram
            _removeTxQueue();
            EMS_Sys_Status.emsRxPgks++; // increment counter
            // myDebug("** Read from 0x%02X ok", type);
            ems_setEmsRefreshed(EMS_TxTelegram.forceRefresh); // does mqtt need refreshing?
        } else {
            // read not OK, we didn't get back a telegram we expected
            // leave on queue and try again, but continue to process what we received as it may be important
            EMS_Sys_Status.txRetryCount++;
            // if retried too many times, give up and remove it
            if (EMS_Sys_Status.txRetryCount >= TX_WRITE_TIMEOUT_COUNT) {
                if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_BASIC) {
                    myDebug("Read failed. Giving up, removing from queue");
                }
                _removeTxQueue();
            } else {
                if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_BASIC) {
                    myDebug("...Retrying read. Attempt %d/%d...", EMS_Sys_Status.txRetryCount, TX_WRITE_TIMEOUT_COUNT);
                }
            }
        }
        _ems_processTelegram(EMS_RxTelegram); // process it always
    }

    if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_WRITE) {
        // should not get here, since this is handled earlier receiving a 01 or 04
        myDebug("** Error ! Write - should not be here");
    }

    if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_VALIDATE) {
        // this is a read telegram which we use to validate the last write
        uint8_t * data         = telegram + 4; // data block starts at position 5
        uint8_t   dataReceived = data[0];      // only a single byte is returned after a read
        if (EMS_TxTelegram.comparisonValue == dataReceived) {
            // validate was successful, the write changed the value
            _removeTxQueue(); // now we can remove the Tx validate command the queue
            if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_BASIC) {
                myDebug("Write to 0x%02X was successful", EMS_TxTelegram.dest);
            }
            // follow up with the post read command
            ems_doReadCommand(EMS_TxTelegram.comparisonPostRead, EMS_TxTelegram.dest, true);
        } else {
            // write failed
            if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_BASIC) {
                myDebug("Last write failed. Compared set value 0x%02X with received value 0x%02X",
                        EMS_TxTelegram.comparisonValue,
                        dataReceived);
            }
            if (++EMS_Sys_Status.txRetryCount > TX_WRITE_TIMEOUT_COUNT) {
                if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_BASIC) {
                    myDebug("Write failed. Giving up, removing from queue");
                }
                _removeTxQueue();
            } else {
                // retry, turn the validate back into a write and try again
                if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_BASIC) {
                    myDebug("...Retrying write. Attempt %d/%d...", EMS_Sys_Status.txRetryCount, TX_WRITE_TIMEOUT_COUNT);
                }
                EMS_TxTelegram.action    = EMS_TX_TELEGRAM_WRITE;
                EMS_TxTelegram.dataValue = EMS_TxTelegram.comparisonValue;  // restore old value
                EMS_TxTelegram.offset    = EMS_TxTelegram.comparisonOffset; // restore old value
                EMS_TxQueue.shift();                                        // remove validate from queue
                EMS_TxQueue.unshift(EMS_TxTelegram);                        // add back to queue making it next in line
            }
        }
    }

    emsaurt_tx_poll(); // send Acknowledgement back to free the EMS bus since we have the telegram
}


/**
 * Check if hot tap water or heating is active
 * using a quick hack for checking the heating. Selected Flow Temp >= 70
 */
void _checkActive() {
    // hot tap water, using flow to check instead of the burner power
    if (EMS_Boiler.wWCurFlow != EMS_VALUE_INT_NOTSET && EMS_Boiler.burnGas != EMS_VALUE_INT_NOTSET) {
        EMS_Boiler.tapwaterActive = ((EMS_Boiler.wWCurFlow != 0) && (EMS_Boiler.burnGas == EMS_VALUE_INT_ON));
    }

    // heating
    if (EMS_Boiler.selFlowTemp != EMS_VALUE_INT_NOTSET && EMS_Boiler.burnGas != EMS_VALUE_INT_NOTSET) {
        EMS_Boiler.heatingActive = ((EMS_Boiler.selFlowTemp >= EMS_BOILER_SELFLOWTEMP_HEATING) && (EMS_Boiler.burnGas == EMS_VALUE_INT_ON));
    }
}

/**
 * UBAParameterWW - type 0x33 - warm water parameters
 * received only after requested (not broadcasted)
 */
void _process_UBAParameterWW(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Boiler.wWActivated   = (_toByte(1) == 0xFF); // 0xFF means on
    EMS_Boiler.wWSelTemp     = _toByte(2);
    EMS_Boiler.wWCircPump    = (_toByte(6) == 0xFF); // 0xFF means on
    EMS_Boiler.wWDesiredTemp = _toByte(8);
    EMS_Boiler.wWComfort     = _toByte(EMS_OFFSET_UBAParameterWW_wwComfort);

    EMS_Sys_Status.emsRefreshed = true; // when we receieve this, lets force an MQTT publish
}

/**
 * UBATotalUptimeMessage - type 0x14 - total uptime
 * received only after requested (not broadcasted)
 */
void _process_UBATotalUptimeMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Boiler.UBAuptime        = _toLong(0);
    EMS_Sys_Status.emsRefreshed = true; // when we receieve this, lets force an MQTT publish
}

/*
 * UBAParametersMessage - type 0x16
 */
void _process_UBAParametersMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Boiler.heating_temp = _toByte(1);
    EMS_Boiler.pump_mod_max = _toByte(9);
    EMS_Boiler.pump_mod_min = _toByte(10);
    // lobocobra start read values MC10
    EMS_Thermostat.ausschalthysterese   = _toByte(4);
    EMS_Thermostat.einschalthysterese   = _toByte(5);
    EMS_Thermostat.antipendelzeit       = _toByte(6);
    EMS_Thermostat.kesselpumennachlauf  = _toByte(8);
    // lobocobra end
}

/**
 * UBAMonitorWWMessage - type 0x34 - warm water monitor. 19 bytes long
 * received every 10 seconds
 */
void _process_UBAMonitorWWMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Boiler.wWCurTmp  = _toShort(1);
    EMS_Boiler.wWStarts  = _toLong(13);
    EMS_Boiler.wWWorkM   = _toLong(10);
    EMS_Boiler.wWOneTime = _bitRead(5, 1);
    EMS_Boiler.wWCurFlow = _toByte(9);
}

/**
 * UBAMonitorFast - type 0x18 - central heating monitor part 1 (25 bytes long)
 * received every 10 seconds
 */
void _process_UBAMonitorFast(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Boiler.selFlowTemp = _toByte(0);
    EMS_Boiler.curFlowTemp = _toShort(1);
    EMS_Boiler.retTemp     = _toShort(13);

    EMS_Boiler.burnGas = _bitRead(7, 0);
    EMS_Boiler.fanWork = _bitRead(7, 2);
    EMS_Boiler.ignWork = _bitRead(7, 3);
    EMS_Boiler.heatPmp = _bitRead(7, 5);
    EMS_Boiler.wWHeat  = _bitRead(7, 6);
    EMS_Boiler.wWCirc  = _bitRead(7, 7);

    EMS_Boiler.curBurnPow = _toByte(4);
    EMS_Boiler.selBurnPow = _toByte(3); // burn power max setting

    EMS_Boiler.flameCurr = _toShort(15);

    // read the service code / installation status as appears on the display
    EMS_Boiler.serviceCodeChar[0] = char(_toByte(18)); // ascii character 1
    EMS_Boiler.serviceCodeChar[1] = char(_toByte(19)); // ascii character 2
    EMS_Boiler.serviceCodeChar[2] = '\0';              // null terminate string

    // read error code
    EMS_Boiler.serviceCode = _toShort(20);

    // system pressure. FF means missing
    EMS_Boiler.sysPress = _toByte(17); // this is *10
    // lobocobra start read value
    //EMS_Boiler.airInflow = _toByte(25);  nicht vorhanden = 8300 bei GB125
    // lobocobra end
    // at this point do a quick check to see if the hot water or heating is active
    _checkActive();
}

/**
 * UBAMonitorSlow - type 0x19 - central heating monitor part 2 (27 bytes long)
 * received every 60 seconds
 */
void _process_UBAMonitorSlow(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Boiler.extTemp     = _toShort(0); // 0x8000 if not available
    EMS_Boiler.abgasTemp   = _toShort(4); // 0x8000 if not available
    EMS_Boiler.boilTemp    = _toShort(2); // 0x8000 if not available
    EMS_Boiler.pumpMod     = _toByte(9);
    EMS_Boiler.burnStarts  = _toLong(10);
    EMS_Boiler.burnWorkMin = _toLong(13);
    EMS_Boiler.heatWorkMin = _toLong(19);
}

/**
 * type 0xB1 - data from the RC10 thermostat (0x17)
 * For reading the temp values only
 * received every 60 seconds
 * e.g. 17 0B 91 00 80 1E 00 CB 27 00 00 00 00 05 01 00 CB 00 (CRC=47), #data=14
 */
void _process_RC10StatusMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.setpoint_roomTemp = _toByte(EMS_OFFSET_RC10StatusMessage_setpoint); // is * 2
    EMS_Thermostat.curr_roomTemp     = _toByte(EMS_OFFSET_RC10StatusMessage_curr);     // is * 10

    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
}

/**
 * type 0x91 - data from the RC20 thermostat (0x17) - 15 bytes long
 * For reading the temp values only
 * received every 60 seconds
 */
void _process_RC20StatusMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.setpoint_roomTemp = _toByte(EMS_OFFSET_RC20StatusMessage_setpoint); // is * 2
    EMS_Thermostat.curr_roomTemp     = _toShort(EMS_OFFSET_RC20StatusMessage_curr);    // is * 10

    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
}

/**
 * type 0x41 - data from the RC30 thermostat(0x10) - 14 bytes long
 * For reading the temp values only * received every 60 seconds 
*/
void _process_RC30StatusMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.setpoint_roomTemp = _toByte(EMS_OFFSET_RC30StatusMessage_setpoint); // is * 2
    EMS_Thermostat.curr_roomTemp     = _toShort(EMS_OFFSET_RC30StatusMessage_curr);    // note, its 2 bytes here

    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
}

/**
 * type 0x3E and 0x48 - data from the RC35 thermostat (0x10) - 16 bytes
 * For reading the temp values only
 * received every 60 seconds
 */
void _process_RC35StatusMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.setpoint_roomTemp = _toByte(EMS_OFFSET_RC35StatusMessage_setpoint); // is * 2

    // check if temp sensor is unavailable
    if (data[3] == 0x7D) {
        EMS_Thermostat.curr_roomTemp = EMS_VALUE_SHORT_NOTSET;
    } else {
        EMS_Thermostat.curr_roomTemp = _toShort(EMS_OFFSET_RC35StatusMessage_curr);
    }
    EMS_Thermostat.urlaub_modus = bitRead(data[0], 5); // get urlaub mode flag
    EMS_Thermostat.sommer_modus = bitRead(data[EMS_OFFSET_RC35Get_mode_day], 0); // get sommer mode flag
    EMS_Thermostat.day_mode = bitRead(data[EMS_OFFSET_RC35Get_mode_day], 1); // get day mode flag
    EMS_Thermostat.max_vorlauf_reached = bitRead(data[EMS_OFFSET_RC35Get_mode_day], 5); // get max vorlauf flag

    EMS_Thermostat.circuitcalctemp = data[EMS_OFFSET_RC35Set_circuitcalctemp]; // 0x48 calculated temperature Vorlauf bit 14

    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
}

/**
 * type 0x0A - data from the Nefit Easy/TC100 thermostat (0x18) - 31 bytes long
 * The Easy has a digital precision of its floats to 2 decimal places, so values must be divided by 100
 */
void _process_EasyStatusMessage(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.curr_roomTemp     = _toShort(EMS_OFFSET_EasyStatusMessage_curr);     // is *100
    EMS_Thermostat.setpoint_roomTemp = _toShort(EMS_OFFSET_EasyStatusMessage_setpoint); // is *100

    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
}

/**
 * type 0x00 - data from the Nefit RC1010 thermostat (0x18) - 24 bytes long
 * The 1010 has a digital precision of its floats to 1 decimal places for the current temperature, so values is divided by 10
 * The 1010 has a digital precision of its floats to 1 decimal places for the set temperature, so values is divided by 2
 */
void _process_RC1010StatusMessage(uint8_t type, uint8_t * data, uint8_t length) {
    EMS_Thermostat.curr_roomTemp     = _toShort(EMS_OFFSET_RC1010StatusMessage_curr);
    EMS_Thermostat.setpoint_roomTemp = _toByte(EMS_OFFSET_RC1010StatusMessage_setpoint); // is * 2
}

void _process_RC1010SetMessage(uint8_t type, uint8_t * data, uint8_t length) {
    // to complete
}

/**
 * type 0xB0 - for reading the mode from the RC10 thermostat (0x17)
 * received only after requested
 */
void _process_RC10Set(uint8_t src, uint8_t * data, uint8_t length) {
    // mode not implemented yet
}

/**
 * type 0xA8 - for reading the mode from the RC20 thermostat (0x17)
 * received only after requested
 */
void _process_RC20Set(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.mode = _toByte(EMS_OFFSET_RC20Set_mode);
}

/**
 * type 0xA7 - for reading the mode from the RC30 thermostat (0x10)
 * received only after requested
 */
void _process_RC30Set(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.mode = _toByte(EMS_OFFSET_RC30Set_mode);
}

/** lobocobra start
 * type 0xA5 - for reading the mode from the RC35 thermostat (0x10)
 * received only after requested
 */
void _process_AnlageParamSet(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.minoutsidetemp   = _toByte(5);
    EMS_Thermostat.housetype        = _toByte(6);
    EMS_Thermostat.tempaveragebool  = _toByte(21); //send 0b 90 a5 15 01 (position 21= hex 15)
    //myDebug("************************************* Anlageparamset %d",EMS_Thermostat.housetype);    
}
 /* type 0x49 - for reading the mode from the RC35 thermostat (0x10)
 * received only after requested
 */
void _process_HK2Schaltzeiten(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.pausezeit  = _toByte(1); //send 0b 90 49 55 01 (pos 1 as we read from 55)
    EMS_Thermostat.partyzeit  = _toByte(2); //send 0b 90 49 56 01 (pos 2 as we read from 55)
    //myDebug("*********************************** Pause h %d Party h %d",EMS_Thermostat.pausezeit,EMS_Thermostat.partyzeit);
}
// lobocobra end

/**
 * type 0x3D and 0x47 - for reading the mode from the RC35 thermostat (0x10)
 * Working Mode Heating Circuit 1 & 2 (HC1, HC2)
 * received only after requested
 */
void _process_RC35Set(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Thermostat.mode = _toByte(EMS_OFFSET_RC35Set_mode);
if (EMS_Thermostat.hc =2 && src != 71) {return; }; // lobocobra desperate attempt to avoid that status messages from 3d overwrite values if you are on HC2
    EMS_Thermostat.daytemp              = _toByte(EMS_OFFSET_RC35Set_temp_day);     // is * 2
    EMS_Thermostat.nighttemp            = _toByte(EMS_OFFSET_RC35Set_temp_night);   // is * 2
    EMS_Thermostat.holidaytemp          = _toByte(EMS_OFFSET_RC35Set_temp_holiday); // is * 2
    //lobocobra start only read if we have 0x47, if not offset goes back 0 (only mqtt not in reality)
        EMS_Thermostat.roomoffset           = _toByte(06); 
        EMS_Thermostat.heatingtype          = _toByte(EMS_OFFSET_RC35Set_heatingtype);  // byte 0 bit floor heating = 3 0x47
        EMS_Thermostat.sommerschwelletemp   = _toByte(22);  // 
        EMS_Thermostat.minvorlauf           = _toByte(16);  // read max temp temp send 0b 90 47 10 01 !!Max Vorlauf is other region
    // read offset temp at min outside temp send 0b 90 47 06 01  

    //lobocobra end
    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
}

/**
 * type 0xA3 - for external temp settings from the the RC* thermostats
 */
void _process_RCOutdoorTempMessage(uint8_t src, uint8_t * data, uint8_t length) {
    // add support here if you're reading external sensors
}

/*
 * SM10Monitor - type 0x97
 */
void _process_SM10Monitor(uint8_t src, uint8_t * data, uint8_t length) {
    EMS_Other.SM10collectorTemp  = _toShort(2);    // collector temp from SM10, is *10
    EMS_Other.SM10bottomTemp     = _toShort(5);    // bottom temp from SM10, is *10
    EMS_Other.SM10pumpModulation = _toByte(4);     // modulation solar pump
    EMS_Other.SM10pump           = _bitRead(7, 1); // active if bit 1 is set

    EMS_Sys_Status.emsRefreshed = true; // triggers a send the values back via MQTT
}

/**
 * UBASetPoint 0x1A
 */
void _process_SetPoints(uint8_t src, uint8_t * data, uint8_t length) {
    
    if (EMS_Sys_Status.emsLogging == EMS_SYS_LOGGING_VERBOSE) {
        if (length != 0) {
            uint8_t setpoint = data[0]; // flow temp
            //uint8_t ww_power = data[2]; // power in %

            /* this logic if the value is *2
            char s[5];
            char s2[5];
            strlcpy(s, itoa(setpoint >> 1, s2, 10), 5);
            strlcat(s, ".", sizeof(s));
            strlcat(s, ((setpoint & 0x01) ? "5" : "0"), 5);
            myDebug(" Boiler flow temp %s C, Warm Water power %d %", s, ww_power);
            */

            myDebug(" Boiler flow temperature is %d C", setpoint);
        }
    }
    
}

/**
 * process_RCTime - type 0x06 - date and time from a thermostat - 14 bytes long
 * common for all thermostats
 */
void _process_RCTime(uint8_t src, uint8_t * data, uint8_t length) {
    if ((EMS_Thermostat.model_id == EMS_MODEL_EASY) || (EMS_Thermostat.model_id == EMS_MODEL_BOSCHEASY)) {
        return; // not supported
    }

    EMS_Thermostat.hour   = _toByte(2);
    EMS_Thermostat.minute = _toByte(4);
    EMS_Thermostat.second = _toByte(5);
    EMS_Thermostat.day    = _toByte(3);
    EMS_Thermostat.month  = _toByte(1);
    EMS_Thermostat.year   = _toByte(0);
}

/**
 * type 0x02 - get the firmware version and type of an EMS device
 * look up known devices via the product id and setup if not already set
 */
void _process_Version(uint8_t src, uint8_t * data, uint8_t length) {
    // ignore short messages that we can't interpret
    if (length < 3) {
        return;
    }

    uint8_t product_id  = _toByte(0);
    char    version[10] = {0};
    snprintf(version, sizeof(version), "%02d.%02d", _toByte(1), _toByte(2));

    // see if its a known boiler
    int  i         = 0;
    bool typeFound = false;
    while (i < _Boiler_Types_max) {
        if (Boiler_Types[i].product_id == product_id) {
            typeFound = true; // we have a matching product id. i is the index.
            break;
        }
        i++;
    }

    if (typeFound) {
        // its a boiler
        myDebug("Boiler found. Model %s (TypeID:0x%02X ProductID:%d Version:%s)",
                Boiler_Types[i].model_string,
                Boiler_Types[i].type_id,
                product_id,
                version);

        // if its a boiler set it, unless it already has been set by checking for a productID
        // it will take the first one found in the list
        if (((EMS_Boiler.type_id == EMS_ID_NONE) || (EMS_Boiler.type_id == Boiler_Types[i].type_id)) && EMS_Boiler.product_id == EMS_ID_NONE) {
            myDebug("* Setting Boiler to model %s (TypeID:0x%02X ProductID:%d Version:%s)",
                    Boiler_Types[i].model_string,
                    Boiler_Types[i].type_id,
                    product_id,
                    version);

            EMS_Boiler.type_id    = Boiler_Types[i].type_id;
            EMS_Boiler.product_id = Boiler_Types[i].product_id;
            strlcpy(EMS_Boiler.version, version, sizeof(EMS_Boiler.version));

            myESP.fs_saveConfig(); // save config to SPIFFS

            ems_getBoilerValues(); // get Boiler values that we would usually have to wait for
        }
        return;
    }

    // its not a boiler, maybe its a known thermostat?
    i = 0;
    while (i < _Thermostat_Types_max) {
        if (Thermostat_Types[i].product_id == product_id) {
            typeFound = true; // we have a matching product id. i is the index.
            break;
        }
        i++;
    }

    if (typeFound) {
        // its a known thermostat
        if (EMS_Sys_Status.emsLogging >= EMS_SYS_LOGGING_BASIC) {
            myDebug("Thermostat found. Model %s (TypeID:0x%02X ProductID:%d Version:%s)",
                    Thermostat_Types[i].model_string,
                    Thermostat_Types[i].type_id,
                    product_id,
                    version);
        }

        // if we don't have a thermostat set, use this one
        if (((EMS_Thermostat.type_id == EMS_ID_NONE) || (EMS_Thermostat.model_id == EMS_MODEL_NONE)
             || (EMS_Thermostat.type_id == Thermostat_Types[i].type_id))
            && EMS_Thermostat.product_id == EMS_ID_NONE) {
            myDebug("* Setting Thermostat model to %s (TypeID:0x%02X ProductID:%d Version:%s)",
                    Thermostat_Types[i].model_string,
                    Thermostat_Types[i].type_id,
                    product_id,
                    version);

            EMS_Thermostat.model_id        = Thermostat_Types[i].model_id;
            EMS_Thermostat.type_id         = Thermostat_Types[i].type_id;
            EMS_Thermostat.read_supported  = Thermostat_Types[i].read_supported;
            EMS_Thermostat.write_supported = Thermostat_Types[i].write_supported;
            EMS_Thermostat.product_id      = product_id;
            strlcpy(EMS_Thermostat.version, version, sizeof(EMS_Thermostat.version));

            myESP.fs_saveConfig(); // save config to SPIFFS

            // get Thermostat values (if supported)
            ems_getThermostatValues();
        }
        return;
    }

    // finally look for the other EMS devices
    i = 0;
    while (i < _Other_Types_max) {
        if (Other_Types[i].product_id == product_id) {
            typeFound = true; // we have a matching product id. i is the index.
            break;
        }
        i++;
    }

    if (typeFound) {
        myDebug("Device found. Model %s with TypeID 0x%02X, ProductID %d, Version %s",
                Other_Types[i].model_string,
                Other_Types[i].type_id,
                product_id,
                version);

        // see if this is a Solar Module SM10
        if (Other_Types[i].type_id == EMS_ID_SM10) {
            EMS_Other.SM10 = true; // we have detected a SM10
            myDebug("SM10 Solar Module support enabled.");
        }

        // fetch other values
        ems_getOtherValues();
        return;

    } else {
        myDebug("Unrecognized device found. TypeID 0x%02X, ProductID %d, Version %s", src, product_id, version);
    }
}

/*
 * Figure out the boiler and thermostat types
 */
void ems_discoverModels() {
    myDebug("Starting auto discover of EMS devices...");

    // boiler
    ems_doReadCommand(EMS_TYPE_Version, EMS_Boiler.type_id); // get version details of boiler

    // solar module
    ems_doReadCommand(EMS_TYPE_Version, EMS_ID_SM10); // check if there is Solar Module available

    // thermostat
    // if it hasn't been set, auto discover it
    if (EMS_Thermostat.type_id == EMS_ID_NONE) {
        ems_scanDevices(); // auto-discover it
    } else {
        // set the model as hardcoded (see my_devices.h) and fetch the version and product id
        ems_doReadCommand(EMS_TYPE_Version, EMS_Thermostat.type_id);
    }
}

/**
 * Print the Tx queue - for debugging
 */
void ems_printTxQueue() {
    _EMS_TxTelegram EMS_TxTelegram;
    char            sType[20] = {0};

    if (EMS_TxQueue.size() == 0) {
        myDebug("Tx queue is empty");
        return;
    }

    myDebug("Tx queue (%d/%d)", EMS_TxQueue.size(), EMS_TxQueue.capacity);

    for (byte i = 0; i < EMS_TxQueue.size(); i++) {
        EMS_TxTelegram = EMS_TxQueue[i]; // retrieves the i-th element from the buffer without removing it

        // get action
        if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_WRITE) {
            strlcpy(sType, "write", sizeof(sType));
        } else if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_READ) {
            strlcpy(sType, "read", sizeof(sType));
        } else if (EMS_TxTelegram.action == EMS_TX_TELEGRAM_VALIDATE) {
            strlcpy(sType, "validate", sizeof(sType));
        } else {
            strlcpy(sType, "?", sizeof(sType));
        }

        char     addedTime[15] = {0};
        uint32_t upt           = EMS_TxTelegram.timestamp;
        snprintf(addedTime,
                 sizeof(addedTime),
                 "(%02d:%02d:%02d)",
                 (uint8_t)((upt / (1000 * 60 * 60)) % 24),
                 (uint8_t)((upt / (1000 * 60)) % 60),
                 (uint8_t)((upt / 1000) % 60));

        myDebug(" [%d] action=%s dest=0x%02x type=0x%02x offset=%d length=%d dataValue=%d "
                "comparisonValue=%d type_validate=0x%02x comparisonPostRead=0x%02x @ %s",
                i + 1,
                sType,
                EMS_TxTelegram.dest & 0x7F,
                EMS_TxTelegram.type,
                EMS_TxTelegram.offset,
                EMS_TxTelegram.length,
                EMS_TxTelegram.dataValue,
                EMS_TxTelegram.comparisonValue,
                EMS_TxTelegram.type_validate,
                EMS_TxTelegram.comparisonPostRead,
                addedTime);
    }
}

/**
 * Generic function to return various settings from the thermostat
 */
void ems_getThermostatValues() {
    if (!ems_getThermostatEnabled()) {
        return;
    }

    if (!EMS_Thermostat.read_supported) {
        myDebug("Read operations not yet supported for this model thermostat");
        return;
    }
    uint8_t model_id = EMS_Thermostat.model_id;
    uint8_t type     = EMS_Thermostat.type_id;
    EMS_Thermostat.hc =2 ; // lobocobra IT WAS 0 after a while, so I force it to 2 here... ugly hack, but who cares
    uint8_t hc       = EMS_Thermostat.hc;
    
    if (model_id == EMS_MODEL_RC20) {
        ems_doReadCommand(EMS_TYPE_RC20StatusMessage, type); // to get the setpoint temp
        ems_doReadCommand(EMS_TYPE_RC20Set, type);           // to get the mode
    } else if (model_id == EMS_MODEL_RC30) {
        ems_doReadCommand(EMS_TYPE_RC30StatusMessage, type); // to get the setpoint temp
        ems_doReadCommand(EMS_TYPE_RC30Set, type);           // to get the mode
    } else if ((model_id == EMS_MODEL_RC35) || (model_id == EMS_MODEL_ES73)) {
        if (hc == 1) {
            ems_doReadCommand(EMS_TYPE_RC35StatusMessage_HC1, type); // to get the setpoint temp
            ems_doReadCommand(EMS_TYPE_RC35Set_HC1, type);           // to get the mode
        } else if (hc == 2) {
            ems_doReadCommand(EMS_TYPE_RC35StatusMessage_HC2, type); // to get the setpoint temp
            ems_doReadCommand(EMS_TYPE_RC35Set_HC2, type);           // to get the mode
            //lobocobra start here we read regularily the data
            ems_doReadCommand(EMS_TYPE_AnlageParamSet, type);        // get PARAM settings
            //ems_doReadCommand(EMS_TYPE_HK2Schaltzeiten, type);     // would read from 0 I need 56
            char Str2[] = "0b 90 49 55 20";                          // read 2nd part of 0x49 starting from DEC 85 
                ems_sendRawTelegram((char *)&Str2);                  // read 2nd part of 0x49 starting from DEC 85
            //ems_doReadCommand(EMS_TYPE_RC35Set_HC2, type, 21);     // => did not work, so I write it directly
            char Str[] = "0b 90 47 16 20";                           // read 2nd part of 0x47 starting from DEC 22 
                ems_sendRawTelegram((char *)&Str);                   // read 2nd part of 0x47 starting from DEC 22 
            //lobocobra end
        }
    } else if ((model_id == EMS_MODEL_EASY) || (model_id == EMS_MODEL_BOSCHEASY)) {
        ems_doReadCommand(EMS_TYPE_EasyStatusMessage, type);
    }

    ems_doReadCommand(EMS_TYPE_RCTime, type); // get Thermostat time
}

/**
 * Generic function to return various settings from the thermostat
 */
void ems_getBoilerValues() {
    ems_doReadCommand(EMS_TYPE_UBAMonitorFast, EMS_Boiler.type_id); // get boiler stats, instead of waiting 10secs for the broadcast
    ems_doReadCommand(EMS_TYPE_UBAMonitorSlow, EMS_Boiler.type_id); // get more boiler stats, instead of waiting 60secs for the broadcast
    ems_doReadCommand(EMS_TYPE_UBAParameterWW, EMS_Boiler.type_id); // get Warm Water values
    ems_doReadCommand(EMS_TYPE_UBAParametersMessage, EMS_Boiler.type_id);  // get MC10 boiler values
    ems_doReadCommand(EMS_TYPE_UBATotalUptimeMessage, EMS_Boiler.type_id); // get uptime from boiler
}

/*
 * Get other values from EMS devices
 */
void ems_getOtherValues() {
    if (EMS_Other.SM10) {
        ems_doReadCommand(EMS_TYPE_SM10Monitor, EMS_ID_SM10); // fetch all from SM10Monitor, e.g. 0B B0 97 00 16
    }
}

/**
 *  returns current thermostat type as a string
 */
char * ems_getThermostatDescription(char * buffer) {
    uint8_t size = 128;
    if (!ems_getThermostatEnabled()) {
        strlcpy(buffer, "<not enabled>", size);
    } else {
        int  i      = 0;
        bool found  = false;
        char tmp[6] = {0};

        // scan through known ID types
        while (i < _Thermostat_Types_max) {
            if (Thermostat_Types[i].product_id == EMS_Thermostat.product_id) {
                found = true; // we have a match
                break;
            }
            i++;
        }

        if (found) {
            strlcpy(buffer, Thermostat_Types[i].model_string, size);
        } else {
            strlcpy(buffer, "TypeID: 0x", size);
            strlcat(buffer, _hextoa(EMS_Thermostat.type_id, tmp), size);
        }

        strlcat(buffer, " (ProductID:", size);
        strlcat(buffer, itoa(EMS_Thermostat.product_id, tmp, 10), size);
        strlcat(buffer, " Version:", size);
        strlcat(buffer, EMS_Thermostat.version, size);
        strlcat(buffer, ")", size);
    }

    return buffer;
}

/**
 *  returns current boiler type as a string
 */
char * ems_getBoilerDescription(char * buffer) {
    uint8_t size = 128;
    if (!ems_getBoilerEnabled()) {
        strlcpy(buffer, "<not enabled>", size);
    } else {
        int  i      = 0;
        bool found  = false;
        char tmp[6] = {0};

        // scan through known ID types
        while (i < _Boiler_Types_max) {
            if (Boiler_Types[i].product_id == EMS_Boiler.product_id) {
                found = true; // we have a match
                break;
            }
            i++;
        }
        if (found) {
            strlcpy(buffer, Boiler_Types[i].model_string, size);
        } else {
            strlcpy(buffer, "TypeID: 0x", size);
            strlcat(buffer, _hextoa(EMS_Boiler.type_id, tmp), size);
        }

        strlcat(buffer, " (ProductID:", size);
        strlcat(buffer, itoa(EMS_Boiler.product_id, tmp, 10), size);
        strlcat(buffer, " Version:", size);
        strlcat(buffer, EMS_Boiler.version, size);
        strlcat(buffer, ")", size);
    }

    return buffer;
}

/*
 * Find the versions of our connected devices
 */
void ems_scanDevices() {
    myDebug("Started scan on EMS bus for known devices");

    std::list<uint8_t> Device_Ids; // create a new list

    // copy over boilers
    for (_Boiler_Type bt : Boiler_Types) {
        Device_Ids.push_back(bt.type_id);
    }

    // copy over thermostats
    for (_Thermostat_Type tt : Thermostat_Types) {
        Device_Ids.push_back(tt.type_id);
    }

    // copy over others
    for (_Other_Type ot : Other_Types) {
        Device_Ids.push_back(ot.type_id);
    }

    // remove duplicates and reserved IDs (like our own device)
    Device_Ids.sort();
    Device_Ids.unique();
    Device_Ids.remove(EMS_MODEL_NONE);

    // send the read command with Version command
    for (uint8_t type_id : Device_Ids) {
        ems_doReadCommand(EMS_TYPE_Version, type_id);
    }
}

/**
 * Print out all handled types
 */
void ems_printAllTypes() {
    uint8_t i;

    myDebug("\nThese %d devices are defined as boiler units:", _Boiler_Types_max);
    for (i = 0; i < _Boiler_Types_max; i++) {
        myDebug(" %s%s%s (TypeID:0x%02X ProductID:%d)",
                COLOR_BOLD_ON,
                Boiler_Types[i].model_string,
                COLOR_BOLD_OFF,
                Boiler_Types[i].type_id,
                Boiler_Types[i].product_id);
    }

    myDebug("\nThese %d devices are defined as other EMS devices:", _Other_Types_max);
    for (i = 0; i < _Other_Types_max; i++) {
        myDebug(" %s%s%s (TypeID:0x%02X ProductID:%d)",
                COLOR_BOLD_ON,
                Other_Types[i].model_string,
                COLOR_BOLD_OFF,
                Other_Types[i].type_id,
                Other_Types[i].product_id);
    }

    myDebug("\nThe following telegram type IDs are recognized:");
    for (i = 0; i < _EMS_Types_max; i++) {
        if ((EMS_Types[i].model_id == EMS_MODEL_ALL) || (EMS_Types[i].model_id == EMS_MODEL_UBA)) {
            myDebug(" type %02X (%s)", EMS_Types[i].type, EMS_Types[i].typeString);
        }
    }

    myDebug("\nThese %d thermostats models are supported:", _Thermostat_Types_max);
    for (i = 0; i < _Thermostat_Types_max; i++) {
        myDebug(" %s%s%s (TypeID:0x%02X ProductID:%d) Read:%c Write:%c",
                COLOR_BOLD_ON,
                Thermostat_Types[i].model_string,
                COLOR_BOLD_OFF,
                Thermostat_Types[i].type_id,
                Thermostat_Types[i].product_id,
                (Thermostat_Types[i].read_supported) ? 'y' : 'n',
                (Thermostat_Types[i].write_supported) ? 'y' : 'n');
    }
}

/**
 * Send a command to UART Tx to Read from another device
 * Read commands when sent must respond by the destination (target) immediately (or within 10ms)
 */
void ems_doReadCommand(uint8_t type, uint8_t dest, bool forceRefresh) {
    // if not a valid type of boiler is not accessible then quits
    if ((type == EMS_ID_NONE) || (dest == EMS_ID_NONE)) {
        return;
    }
    // if we're preventing all outbound traffic, quit
    if (EMS_Sys_Status.emsTxDisabled) {
        myDebug("in Silent Mode. All Tx is disabled.");
        return;
    }

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    // see if its a known type
    int i = _ems_findType(type);

    if ((ems_getLogging() == EMS_SYS_LOGGING_BASIC) || (ems_getLogging() == EMS_SYS_LOGGING_VERBOSE)) {
        if (i == -1) {
            myDebug("Requesting type (0x%02X) from dest 0x%02X", type, dest);
        } else {
            myDebug("Requesting type %s(0x%02X) from dest 0x%02X", EMS_Types[i].typeString, type, dest);
        }
    }
    EMS_TxTelegram.action             = EMS_TX_TELEGRAM_READ;    // read command
    EMS_TxTelegram.dest               = dest;                    // set 8th bit to indicate a read
    EMS_TxTelegram.offset             = 0;                       // 0 for all data
    EMS_TxTelegram.length             = EMS_MIN_TELEGRAM_LENGTH; // is always 6 bytes long (including CRC at end)
    EMS_TxTelegram.type               = type;
    EMS_TxTelegram.dataValue          = EMS_MAX_TELEGRAM_LENGTH; // for a read this is the # bytes we want back
    EMS_TxTelegram.type_validate      = EMS_ID_NONE;
    EMS_TxTelegram.comparisonValue    = 0;
    EMS_TxTelegram.comparisonOffset   = 0;
    EMS_TxTelegram.comparisonPostRead = EMS_ID_NONE;
    EMS_TxTelegram.forceRefresh       = forceRefresh; // should we send to MQTT after a successful read?

    EMS_TxQueue.push(EMS_TxTelegram);
}

/**
 * Send a raw telegram to the bus
 * telegram is a string of hex values
 */
void ems_sendRawTelegram(char * telegram) {
    uint8_t count = 0;
    char *  p;
    char    value[10] = {0};

    if (EMS_Sys_Status.emsTxDisabled) {
        return; // user has disabled all Tx
    }

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    // get first value, which should be the src
    if ((p = strtok(telegram, " ,"))) { // delimiter
        strlcpy(value, p, sizeof(value));
        EMS_TxTelegram.data[0] = (uint8_t)strtol(value, 0, 16);
    }
    // and interate until end
    while (p != 0) {
        if ((p = strtok(NULL, " ,"))) {
            strlcpy(value, p, sizeof(value));
            uint8_t val                  = (uint8_t)strtol(value, 0, 16);
            EMS_TxTelegram.data[++count] = val;
            if (count == 1) {
                EMS_TxTelegram.dest = val;
            } else if (count == 2) {
                EMS_TxTelegram.type = val;
            } else if (count == 3) {
                EMS_TxTelegram.offset = val;
            }
        }
    }

    if (count == 0) {
        return; // nothing to send
    }

    // calculate length including header and CRC
    EMS_TxTelegram.length        = count + 2;
    EMS_TxTelegram.type_validate = EMS_ID_NONE;
    EMS_TxTelegram.action        = EMS_TX_TELEGRAM_RAW;

    // add to Tx queue. Assume it's not full.
    EMS_TxQueue.push(EMS_TxTelegram);
}

/**
 * Set the temperature of the thermostat
 * temptype 0 = normal, 1=night temp, 2=day temp, 3=holiday temp
 */
void ems_setThermostatTemp(float temperature, uint8_t temptype) {
    if (!ems_getThermostatEnabled()) {
        return;
    }

    if (!EMS_Thermostat.write_supported) {
        myDebug("Write not supported for this model Thermostat");
        return;
    }

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    uint8_t model_id = EMS_Thermostat.model_id;
    uint8_t type     = EMS_Thermostat.type_id;
    uint8_t hc       = EMS_Thermostat.hc; // heating circuit

    EMS_TxTelegram.action = EMS_TX_TELEGRAM_WRITE;
    EMS_TxTelegram.dest   = type;

    myDebug("Setting new thermostat temperature");

    // when doing a comparison  to validate the new temperature we call a different type

    if (model_id == EMS_MODEL_RC20) {
        EMS_TxTelegram.type               = EMS_TYPE_RC20Set;
        EMS_TxTelegram.offset             = EMS_OFFSET_RC20Set_temp;
        EMS_TxTelegram.comparisonPostRead = EMS_TYPE_RC20StatusMessage;
    } else if (model_id == EMS_MODEL_RC10) {
        EMS_TxTelegram.type               = EMS_TYPE_RC10Set;
        EMS_TxTelegram.offset             = EMS_OFFSET_RC10Set_temp;
        EMS_TxTelegram.comparisonPostRead = EMS_TYPE_RC10StatusMessage;
    } else if (model_id == EMS_MODEL_RC30) {
        EMS_TxTelegram.type               = EMS_TYPE_RC30Set;
        EMS_TxTelegram.offset             = EMS_OFFSET_RC30Set_temp;
        EMS_TxTelegram.comparisonPostRead = EMS_TYPE_RC30StatusMessage;
    } else if ((model_id == EMS_MODEL_RC35) || (model_id == EMS_MODEL_ES73)) {
        switch (temptype) {
        case 1: // change the night temp
            EMS_TxTelegram.offset = EMS_OFFSET_RC35Set_temp_night;
            break;
        case 2: //  change the day temp
            EMS_TxTelegram.offset = EMS_OFFSET_RC35Set_temp_day;
            break;
        case 3:                        // change the holiday temp
            EMS_TxTelegram.offset = 3; //holiday on RC35
            break;
        default:
        case 0: // automatic selection, if no type is defined, we use the standard code
            if (EMS_Thermostat.day_mode == 0) {
                EMS_TxTelegram.offset = EMS_OFFSET_RC35Set_temp_night;
            } else if (EMS_Thermostat.day_mode == 1) {
                EMS_TxTelegram.offset = EMS_OFFSET_RC35Set_temp_day;
            }
            break;
        }

        if (hc == 1) {
            // heating circuit 1
            EMS_TxTelegram.type               = EMS_TYPE_RC35Set_HC1;
            EMS_TxTelegram.comparisonPostRead = EMS_TYPE_RC35StatusMessage_HC1;
        } else {
            // heating circuit 2
            EMS_TxTelegram.type               = EMS_TYPE_RC35Set_HC2;
            EMS_TxTelegram.comparisonPostRead = EMS_TYPE_RC35StatusMessage_HC2;
        }
    }

    EMS_TxTelegram.length           = EMS_MIN_TELEGRAM_LENGTH;
    EMS_TxTelegram.dataValue        = (uint8_t)((float)temperature * (float)2); // value
    EMS_TxTelegram.type_validate    = EMS_TxTelegram.type;
    EMS_TxTelegram.comparisonOffset = EMS_TxTelegram.offset;
    EMS_TxTelegram.comparisonValue  = EMS_TxTelegram.dataValue;

    EMS_TxTelegram.forceRefresh = false; // send to MQTT is done automatically in EMS_TYPE_RC*StatusMessage
    EMS_TxQueue.push(EMS_TxTelegram);
}

/**
 * Set the thermostat working mode (0=low/night, 1=manual/day, 2=auto/clock)
 * 0xA8 on a RC20 and 0xA7 on RC30
 */
void ems_setThermostatMode(uint8_t mode) {
    if (!ems_getThermostatEnabled()) {
        return;
    }

    if (!EMS_Thermostat.write_supported) {
        myDebug("Write not supported for this model Thermostat");
        return;
    }

    uint8_t model_id = EMS_Thermostat.model_id;
    uint8_t type     = EMS_Thermostat.type_id;
    uint8_t hc       = EMS_Thermostat.hc;

    myDebug("Setting thermostat mode to %d", mode);

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    EMS_TxTelegram.action    = EMS_TX_TELEGRAM_WRITE;
    EMS_TxTelegram.dest      = type;
    EMS_TxTelegram.length    = EMS_MIN_TELEGRAM_LENGTH;
    EMS_TxTelegram.dataValue = mode;

    // handle different thermostat types
    if (model_id == EMS_MODEL_RC20) {
        EMS_TxTelegram.type   = EMS_TYPE_RC20Set;
        EMS_TxTelegram.offset = EMS_OFFSET_RC20Set_mode;
    } else if (model_id == EMS_MODEL_RC30) {
        EMS_TxTelegram.type   = EMS_TYPE_RC30Set;
        EMS_TxTelegram.offset = EMS_OFFSET_RC30Set_mode;
    } else if ((model_id == EMS_MODEL_RC35) || (model_id == EMS_MODEL_ES73)) {
        EMS_TxTelegram.type   = (hc == 2) ? EMS_TYPE_RC35Set_HC2 : EMS_TYPE_RC35Set_HC1;
        EMS_TxTelegram.offset = EMS_OFFSET_RC35Set_mode;
    }

    EMS_TxTelegram.type_validate      = EMS_TxTelegram.type; // callback to EMS_TYPE_RC30Temperature to fetch temps
    EMS_TxTelegram.comparisonOffset   = EMS_TxTelegram.offset;
    EMS_TxTelegram.comparisonValue    = EMS_TxTelegram.dataValue;
    EMS_TxTelegram.comparisonPostRead = EMS_TxTelegram.type;
    EMS_TxTelegram.forceRefresh       = false; // send to MQTT is done automatically in 0xA8 process

    EMS_TxQueue.push(EMS_TxTelegram);
}

/**
 * Set the warm water temperature 0x33
 */
void ems_setWarmWaterTemp(uint8_t temperature) {
    // check for invalid temp values
    if ((temperature < 30) || (temperature > EMS_BOILER_TAPWATER_TEMPERATURE_MAX)) {
        return;
    }

    myDebug("Setting boiler warm water temperature to %d C", temperature);

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    EMS_TxTelegram.action    = EMS_TX_TELEGRAM_WRITE;
    EMS_TxTelegram.dest      = EMS_Boiler.type_id;
    EMS_TxTelegram.type      = EMS_TYPE_UBAParameterWW;
    EMS_TxTelegram.offset    = EMS_OFFSET_UBAParameterWW_wwtemp;
    EMS_TxTelegram.length    = EMS_MIN_TELEGRAM_LENGTH;
    EMS_TxTelegram.dataValue = temperature; // value to compare against. must be a single int

    EMS_TxTelegram.type_validate      = EMS_TYPE_UBAParameterWW; // validate
    EMS_TxTelegram.comparisonOffset   = EMS_OFFSET_UBAParameterWW_wwtemp;
    EMS_TxTelegram.comparisonValue    = temperature;
    EMS_TxTelegram.comparisonPostRead = EMS_TYPE_UBAParameterWW;
    EMS_TxTelegram.forceRefresh       = false; // no need to send since this is done by 0x33 process

    EMS_TxQueue.push(EMS_TxTelegram);
}

/**
 * Set the boiler flow temp
 */
void ems_setFlowTemp(uint8_t temperature) {

    myDebug("Setting boiler flow temperature to %d C", temperature);

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    EMS_TxTelegram.action    = EMS_TX_TELEGRAM_WRITE;
    EMS_TxTelegram.dest      = EMS_Boiler.type_id;
    EMS_TxTelegram.type      = EMS_TYPE_UBASetPoints;
    EMS_TxTelegram.offset    = EMS_OFFSET_UBASetPoints_flowtemp;
    EMS_TxTelegram.length    = EMS_MIN_TELEGRAM_LENGTH;
    EMS_TxTelegram.dataValue = temperature; // value to compare against. must be a single int

    EMS_TxTelegram.type_validate      = EMS_TYPE_UBASetPoints; // validate
    EMS_TxTelegram.comparisonOffset   = EMS_OFFSET_UBASetPoints_flowtemp;
    EMS_TxTelegram.comparisonValue    = temperature;
    EMS_TxTelegram.comparisonPostRead = EMS_TYPE_UBASetPoints;
    EMS_TxTelegram.forceRefresh       = false;

    EMS_TxQueue.push(EMS_TxTelegram);
   
}

/**
 * Set the warm water mode to comfort to Eco/Comfort
 * 1 = Hot, 2 = Eco, 3 = Intelligent
 */
void ems_setWarmWaterModeComfort(uint8_t comfort) {
    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    if (comfort == 1) {
        myDebug("Setting boiler warm water comfort mode to Hot");
        EMS_TxTelegram.dataValue = EMS_VALUE_UBAParameterWW_wwComfort_Hot;
    } else if (comfort == 2) {
        myDebug("Setting boiler warm water comfort mode to Eco");
        EMS_TxTelegram.dataValue = EMS_VALUE_UBAParameterWW_wwComfort_Eco;
    } else if (comfort == 3) {
        myDebug("Setting boiler warm water comfort mode to Intelligent");
        EMS_TxTelegram.dataValue = EMS_VALUE_UBAParameterWW_wwComfort_Intelligent;
    } else {
        return; // invalid comfort value
    }

    EMS_TxTelegram.action        = EMS_TX_TELEGRAM_WRITE;
    EMS_TxTelegram.dest          = EMS_Boiler.type_id;
    EMS_TxTelegram.type          = EMS_TYPE_UBAParameterWW;
    EMS_TxTelegram.offset        = EMS_OFFSET_UBAParameterWW_wwComfort;
    EMS_TxTelegram.length        = EMS_MIN_TELEGRAM_LENGTH;
    EMS_TxTelegram.type_validate = EMS_ID_NONE; // don't validate

    EMS_TxQueue.push(EMS_TxTelegram);
}

/**
 * Activate / De-activate the Warm Water 0x33
 * true = on, false = off
 */
void ems_setWarmWaterActivated(bool activated) {
    myDebug("Setting boiler warm water %s", activated ? "on" : "off");

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    EMS_TxTelegram.action        = EMS_TX_TELEGRAM_WRITE;
    EMS_TxTelegram.dest          = EMS_Boiler.type_id;
    EMS_TxTelegram.type          = EMS_TYPE_UBAParameterWW;
    EMS_TxTelegram.offset        = EMS_OFFSET_UBAParameterWW_wwactivated;
    EMS_TxTelegram.length        = EMS_MIN_TELEGRAM_LENGTH;
    EMS_TxTelegram.type_validate = EMS_ID_NONE;               // don't validate
    EMS_TxTelegram.dataValue     = (activated ? 0xFF : 0x00); // 0xFF is on, 0x00 is off

    EMS_TxQueue.push(EMS_TxTelegram);
}

/**
 * Activate / De-activate the Warm Tap Water
 * true = on, false = off
 * Using the type 0x1D to put the boiler into Test mode. This may be shown on the boiler with a flashing 'T'
 */
void ems_setWarmTapWaterActivated(bool activated) {
    myDebug("Setting boiler warm tap water %s", activated ? "on" : "off");

    _EMS_TxTelegram EMS_TxTelegram = EMS_TX_TELEGRAM_NEW; // create new Tx
    EMS_TxTelegram.timestamp       = millis();            // set timestamp
    EMS_Sys_Status.txRetryCount    = 0;                   // reset retry counter

    // clear Tx to make sure all data is set to 0x00
    for (int i = 0; (i < EMS_MAX_TELEGRAM_LENGTH); i++) {
        EMS_TxTelegram.data[i] = 0x00;
    }

    EMS_TxTelegram.action = EMS_TX_TELEGRAM_WRITE;
    EMS_TxTelegram.dest   = EMS_Boiler.type_id;
    EMS_TxTelegram.type   = EMS_TYPE_UBAFunctionTest;
    EMS_TxTelegram.offset = 0;
    EMS_TxTelegram.length = 22; // 17 bytes of data including header and CRC

    EMS_TxTelegram.type_validate      = EMS_TxTelegram.type;
    EMS_TxTelegram.comparisonOffset   = 0;                   // 1st byte
    EMS_TxTelegram.comparisonValue    = (activated ? 0 : 1); // value is 1 if in Test mode (not activated)
    EMS_TxTelegram.comparisonPostRead = EMS_TxTelegram.type;
    EMS_TxTelegram.forceRefresh       = true; // send new value to MQTT after successful write

    // create header
    EMS_TxTelegram.data[0] = EMS_ID_ME;             // src
    EMS_TxTelegram.data[1] = EMS_TxTelegram.dest;   // dest
    EMS_TxTelegram.data[2] = EMS_TxTelegram.type;   // type
    EMS_TxTelegram.data[3] = EMS_TxTelegram.offset; // offset

    // we use the special test mode 0x1D for this. Setting the first data to 5A puts the system into test mode and
    // a setting of 0x00 puts it back into normal operarting mode
    // when in test mode we're able to mess around with the core 3-way valve settings
    if (!activated) {
        // on
        EMS_TxTelegram.data[4] = 0x5A; // test mode on
        EMS_TxTelegram.data[5] = 0x00; // burner output 0%
        EMS_TxTelegram.data[7] = 0x64; // boiler pump capacity 100%
        EMS_TxTelegram.data[8] = 0xFF; // 3-way valve hot water only
    }

    EMS_TxQueue.push(EMS_TxTelegram); // add to queue
}

/*
 * Start up sequence for UBA Master, hopefully to initialize a handshake
 * Still experimental
 */
void ems_startupTelegrams() {
    if ((EMS_Sys_Status.emsTxDisabled) || (!EMS_Sys_Status.emsBusConnected)) {
        myDebug("Unable to send startup sequence when in silent mode or bus is disabled");
    }

    myDebug("Sending startup sequence...");
    char s[20] = {0};

    // (00:07:27.512) Telegram echo: telegram: 0B 08 1D 00 00 (CRC=84), #data=1
    // Write type 0x1D to get out of function test mode
    snprintf(s, sizeof(s), "%02X %02X 1D 00 00", EMS_ID_ME, EMS_Boiler.type_id);
    ems_sendRawTelegram(s);

    // (00:07:35.555) Telegram echo: telegram: 0B 88 01 00 1B (CRC=8B), #data=1
    // Read type 0x01
    snprintf(s, sizeof(s), "%02X %02X 01 00 1B", EMS_ID_ME, EMS_Boiler.type_id | 0x80);
    ems_sendRawTelegram(s);
}
