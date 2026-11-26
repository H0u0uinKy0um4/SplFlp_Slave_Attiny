#pragma once
#include "Motor.h"
#include "MotionPlanner.h"
#include "PersistedLayout.h"
#include <Arduino.h>
#define DRUM_SIZE ((SF_EEPROM_MAP_CHARS_ADDR - SF_EEPROM_MAP_START_ADDR) / 2)
#define HARDCODED_ID 10
#define  NUM_ALL 64
#define NUM_ERROR -1
#define BROADCAST_DELAY 110
const char DEFAULT_FLAP_CHARS[] =" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$&()-+=;q:%'.,/?*roygbpw";

// SplitFlapModule is the Arduino adapter around the deep motion module: it
// supplies persisted calibration and motor access through the two ports, then
// delegates every target decision to MotionPlanner.
class SplitFlapModule : public CalibrationPort, public MotionPort
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
    uint16_t getMapPosition(uint8_t index) const;
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
    uint16_t getTotalStepsPerRev() const { return stepper.totalStepsPerRev; }
    bool isPositionKnown() const { return stepper.positionKnown; }
    // OTA: arm the boot flag so the next reset enters the bootloader.
    bool requestBootloader();

  // CalibrationPort
    uint16_t homeOffset() const override;
    uint16_t totalStepsPerRev() const override;
    uint16_t mapAt(int index) const override;
    char flapAt(int index) const override;
    void storeTotalSteps(uint16_t steps) override;

  // MotionPort
    uint16_t position() const override;
    void setPosition(uint16_t pos) override;
    bool positionKnown() const override;
    void setPositionKnown(bool known) override;
    int currentFlapIndex() const override;
    void setCurrentFlapIndex(int index) override;
    void homeMotor() override;
    void releaseMotor() override;
    void beginHallWatch() override;
    bool hallReferenced() const override;
    void stepOne(bool watchHall) override;
    long countPerRev() override;

private:
  // All addresses and marker values come from the canonical persisted-layout
  // record; see layout/persisted_layout.json and tools/generate_layout.py.
  static constexpr int ADDR_INIT        = SF_EEPROM_INIT_ADDR;
  static constexpr int ADDR_HOME_OFFSET = SF_EEPROM_HOME_OFFSET_ADDR;
  static constexpr int ADDR_TOTAL_STEPS = SF_EEPROM_TOTAL_STEPS_ADDR;
  static constexpr int ADDR_MODULE_ID   = SF_EEPROM_MODULE_ID_ADDR;
  static constexpr int ADDR_AUTO_HOME   = SF_EEPROM_AUTO_HOME_ADDR;
  // Addresses 7..11 previously held a saved position/index; the position is
  // now established by homing on boot, so they stay unused.
  static constexpr int ADDR_MAP_START   = SF_EEPROM_MAP_START_ADDR;
  static constexpr int ADDR_MAP_CHARS   = SF_EEPROM_MAP_CHARS_ADDR;
  static constexpr int ADDR_FORMAT      = SF_EEPROM_MAP_FORMAT_ADDR;
  // Application version string, written by the app when it changes.
  // 205..243 are free (map ends at 204, DebugMark uses 244..254 and the OTA
  // boot flag sits at 255); 205..212 hold the version, the rest stays unused.
  static constexpr int ADDR_FW_VERSION  = SF_EEPROM_FW_VERSION_ADDR;
  static constexpr uint8_t FW_VERSION_SLOT = SF_FW_VERSION_SLOT;
  static constexpr uint8_t INIT_VALUE = SF_EEPROM_INIT_VALUE;
  static constexpr uint8_t FORMAT_VALUE = SF_EEPROM_MAP_FORMAT_VALUE;
  

  bool autoHomeEnabled=true;
  uint8_t id;
  Motor stepper;
  char flapChars[DRUM_SIZE];
  MotionPlanner motion_;
  

};