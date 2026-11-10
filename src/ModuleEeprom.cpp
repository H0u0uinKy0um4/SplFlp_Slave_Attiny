#include <EEPROM.h>
#include "ModuleHandler.h"
#include "OTAProtocol.h"
#include <stdint.h>

// Set the boot flag the bootloader checks (EEPROM 255, alone in its 32-byte
// EEPROM page, above the app's last used byte at 204).  The next reset stays
// in update mode; only a verified FINISH or an explicit RESET clears it.
bool SplitFlapModule::requestBootloader()
{
    EEPROM.update(OTA_EEPROM_BOOT_FLAG_ADDR, OTA_BOOT_FLAG_VALUE);
    return (EEPROM.read(OTA_EEPROM_BOOT_FLAG_ADDR) == OTA_BOOT_FLAG_VALUE);
}

bool SplitFlapModule::getAutoHome()const
{
  return (EEPROM.read(ADDR_AUTO_HOME) == 1);
}

int SplitFlapModule::getId()const
{
  return EEPROM.read(ADDR_MODULE_ID);
}

void SplitFlapModule::getConfig(String &message) const
{
    message += 'm';
    message += String(getId());
    message += ':';
    message += String(stepper.homeOffset);
    message += ':';
    message += String(stepper.totalStepsPerRev);
    message += ':';
    message += String(autoHomeEnabled);
    message += ':';
    bool first = true;

    for (uint8_t i = 0; i < DRUM_SIZE; ++i) {
        uint16_t pos;
        EEPROM.get(ADDR_MAP_START + i * sizeof(pos), pos);
        if (pos == 0xFFFF)continue;
        if (!first)message += ',';

        message += String(i);
        message += '=';
        message += String(pos);
        first = false;
    }
}
void SplitFlapModule::getDrum(String & message)const
{
  for (uint8_t i = 0; i < DRUM_SIZE; i++) {
      message += (char)EEPROM.read(ADDR_MAP_CHARS + i);
  }
}

void SplitFlapModule::loadFlapChars()
{
    for (uint8_t i = 0; i < DRUM_SIZE; i++) {
        flapChars[i] = (char)EEPROM.read(ADDR_MAP_CHARS + i);
    }
}
//STORE//
void SplitFlapModule::setAutoHome(int enable)
{
  if(enable == NUM_ERROR)return;
  autoHomeEnabled=enable;
  EEPROM.put(ADDR_AUTO_HOME, autoHomeEnabled);
}

void SplitFlapModule::setHomeOffset(int homeOffset)
{
  if (homeOffset<0)return;
  EEPROM.put(ADDR_HOME_OFFSET, homeOffset);
  stepper.homeOffset=homeOffset;
}
void SplitFlapModule::setStepsPerRev(int StepsPerRev)
{
  if(StepsPerRev < 0 || StepsPerRev >= UINT16_MAX)return;
  stepper.totalStepsPerRev=StepsPerRev;
  saveTotalSteps();
}

void SplitFlapModule::saveTotalSteps()
{ EEPROM.put(ADDR_TOTAL_STEPS, stepper.totalStepsPerRev); }

void SplitFlapModule::setModuleId(int moduleId)
{
  if(moduleId<=0 || moduleId > UINT8_MAX)return;
  EEPROM.put(ADDR_MODULE_ID, moduleId);
}






//MAP

uint16_t SplitFlapModule::getMapPosition(uint8_t index)
{
    if (index >= DRUM_SIZE) return 0xFFFF;
    uint16_t position;
    EEPROM.get(ADDR_MAP_START + (index * sizeof(uint16_t)), position);
    return position;
}

void SplitFlapModule::setMapPosition(int index, long position)
{
    if (index < 0 || index >= DRUM_SIZE) return;
    if (position < 0 || position > 0xFFFF) return;
    uint16_t pos16 = (uint16_t)position;
    EEPROM.put(ADDR_MAP_START + (index * sizeof(uint16_t)), pos16);
}

void SplitFlapModule::clearMap()
{
  for (uint8_t i = 0; i < DRUM_SIZE; i++) setMapPosition(i, 0xFFFF);
}

void SplitFlapModule::deleteMapPosition(int index)
{
  if (index == NUM_ALL) {
    for (int i = 0; i < DRUM_SIZE; i++) setMapPosition(i, 0xFFFF);
    return;
  }
  if (index < 0 || index >= DRUM_SIZE) return;
  setMapPosition(index, 0xFFFF);
}



//Flaps
void SplitFlapModule::setFlapChar(int index, char c)
{
    if (index <0 || index >= DRUM_SIZE) return;

    EEPROM.update(ADDR_MAP_CHARS + index, (uint8_t)c);
    flapChars[index] = c;
}

// LOAD


bool SplitFlapModule::checkFirstBoot()
{
  return EEPROM.read(ADDR_INIT) == INIT_VALUE;
}
//LOAD//
void SplitFlapModule::loadOnBoot()
{
  EEPROM.get(ADDR_HOME_OFFSET, stepper.homeOffset);
  EEPROM.get(ADDR_MODULE_ID, id);
  EEPROM.get(ADDR_TOTAL_STEPS, stepper.totalStepsPerRev);
  autoHomeEnabled = (EEPROM.read(ADDR_AUTO_HOME) == 1);

  // The saved position is only a hint.  A move homes first unless the boot
  // sequence already homed, so a stale or uninitialised EEPROM value can never
  // make a move silently claim success without turning the motor.
  stepper.currentStepPos   = 0;
  stepper.currentFlapIndex = -1;
  stepper.positionKnown    = false;

  migrateMapFormat();
  loadFlapChars();
}

// Legacy firmware stored map entries relative to flap 0.  The current
// coordinate system counts from the hall entering edge, so add homeOffset once
// and tag the format.  See PROTOCOL.md.
void SplitFlapModule::migrateMapFormat()
{
  if (EEPROM.read(ADDR_FORMAT) == FORMAT_VALUE) return;
  if (stepper.totalStepsPerRev == 0) return;

  for (uint8_t i = 0; i < DRUM_SIZE; i++) {
    uint16_t pos;
    EEPROM.get(ADDR_MAP_START + i * sizeof(pos), pos);
    if (pos == 0xFFFF) continue;
    pos = (uint16_t)(((long)pos + stepper.homeOffset) % stepper.totalStepsPerRev);
    EEPROM.put(ADDR_MAP_START + i * sizeof(pos), pos);
  }
  EEPROM.write(ADDR_FORMAT, FORMAT_VALUE);
}
void SplitFlapModule::firstBoot()
{
      // First boot — write defaults to EEPROM
    EEPROM.write(ADDR_INIT, INIT_VALUE);
    setHomeOffset(stepper.homeOffset);
    saveTotalSteps();
    id = HARDCODED_ID;
    EEPROM.write(ADDR_MODULE_ID, id);
    EEPROM.write(ADDR_AUTO_HOME, 1);
    for (uint8_t i = 0; i < DRUM_SIZE; i++) {
        char c = DEFAULT_FLAP_CHARS[i];

        EEPROM.update(ADDR_MAP_CHARS + i, (uint8_t)c);
        flapChars[i] = c;
    }
    clearMap();
    EEPROM.write(ADDR_FORMAT, FORMAT_VALUE);
}
