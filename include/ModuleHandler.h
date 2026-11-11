#pragma once
#include "Motor.h"
#include <Arduino.h>
#define DRUM_SIZE 64
#define HARDCODED_ID 10
#define  NUM_ALL 64
#define NUM_ERROR -1
#define BROADCAST_DELAY 110
const char DEFAULT_FLAP_CHARS[] =" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$&()-+=;q:%'.,/?*roygbpw";

class SplitFlapModule
{
  public:
  SplitFlapModule();

  //EEprom
    void waitOnBroadCast();
    //Get
    bool getAutoHome()const;
    int getId()const;
    void getConfig(String & message)const;
    void getDrum(String & message)const;
    // Application version from the EEPROM slot (see FirmwareVersion.h).
    void getFirmwareVersion(String & message)const;
    void saveFirmwareVersion();
    //STORE
    void saveTotalSteps();
    void setHomeOffset(int homeOffset);
    void setModuleId(int moduleId);
    void setStepsPerRev(int StepsPerRev);
    void setAutoHome(int enable);
    //Map
    void clearMap();
    void setMapPosition(int index, long position);
    void deleteMapPosition(int index);
    //Flaps
    void setFlapChar(int index, char c);
    uint16_t getMapPosition(uint8_t index);
    void loadFlapChars();
    //LOAD
    void loadOnBoot();
    bool checkFirstBoot();
    void firstBoot();
    void migrateMapFormat();
  //Commands
    void Calibrate();
    void moveToFlap(char Flap);
    void moveToIndex(int index);
    void moveToStep(int steps);
    void home();
    uint16_t getCurrentStepPos() const { return stepper.currentStepPos; }
    int getHomeOffset() const { return stepper.homeOffset; }
    bool isPositionKnown() const { return stepper.positionKnown; }
    // OTA: arm the boot flag so the next reset enters the bootloader.
    bool requestBootloader();

private:
  static constexpr int ADDR_INIT        = 0;
  static constexpr int ADDR_HOME_OFFSET = 1;
  static constexpr int ADDR_TOTAL_STEPS = 3;
  static constexpr int ADDR_MODULE_ID   = 5;
  static constexpr int ADDR_AUTO_HOME   = 6;
  // Addresses 7..11 previously held a saved position/index; the position is
  // now established by homing on boot, so they stay unused.
  static constexpr int ADDR_MAP_START   = 12;
  static constexpr int ADDR_MAP_CHARS = 140;
  static constexpr int ADDR_FORMAT      = 204;
  // Application version string, written by the app when it changes.
  // 205..243 are free (map ends at 204, DebugMark uses 244..254 and the OTA
  // boot flag sits at 255); 205..212 hold the version, the rest stays unused.
  static constexpr int ADDR_FW_VERSION  = 205;
  static constexpr uint8_t FW_VERSION_SLOT = 8;
  static constexpr uint8_t INIT_VALUE = 0x5D;
  static constexpr uint8_t FORMAT_VALUE = 0x02;
  

  bool autoHomeEnabled=true;
  uint8_t id;
  Motor stepper;
  char flapChars[DRUM_SIZE];
  

};