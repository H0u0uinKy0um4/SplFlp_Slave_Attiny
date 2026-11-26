#include "AsciiCommand.h"

#include <stdlib.h>
#include <string.h>

namespace {

class NullReplyWriter : public ReplyWriter {
public:
  void write(const char*) override {}
  void writeChar(char) override {}
  void writeNumber(long) override {}
  void endReply() override {}
};

NullReplyWriter nullReply;

struct ParsedNumber {
  bool valid;
  bool wildcard;
  long value;
};

// A wildcard is a distinct value, never an integer that happens to be 64.
// Only commands that explicitly opt in (map deletion) may consume it.
ParsedNumber parseNumber(const char*& arg, char terminal = '\0') {
  ParsedNumber result = {false, false, 0};

  if (*arg == '*') {
    ++arg;
    if (*arg == terminal || *arg == '\0') {
      result.valid = true;
      result.wildcard = true;
      result.value = 64;
    }
    return result;
  }

  if (*arg < '0' || *arg > '9') return result;

  long sum = 0;
  while (*arg != terminal && *arg != '\0') {
    if (*arg < '0' || *arg > '9') return result;
    sum = sum * 10 + (*arg - '0');
    if (sum > 65535L) return result;
    ++arg;
  }
  if (terminal != '\0' && *arg != terminal) return result;
  if (*arg != '\0') ++arg;

  result.valid = true;
  result.value = sum;
  return result;
}

bool scalar(const char*& arg, long& value) {
  ParsedNumber parsed = parseNumber(arg);
  if (!parsed.valid || parsed.wildcard) return false;
  value = parsed.value;
  return true;
}

void writePrefix(ReplyWriter& out, int id, const char* tail) {
  out.write("m");
  out.writeNumber(id);
  out.write(tail);
}

} // namespace

AsciiCommand::AsciiCommand(ModulePort& module, ReplyWriter& reply,
                           CommandControl& control)
  : module_(module), reply_(reply), control_(control) {}

void AsciiCommand::handleLine(const char* line) {
  if (line == 0 || line[0] != 'm') return;

  const char* p = line + 1;
  bool broadcast = false;
  bool hasDigits = false;
  int id = 0;
  while (*p == '*' || (*p >= '0' && *p <= '9')) {
    if (*p == '*') {
      broadcast = true;
    } else {
      hasDigits = true;
      if (id < 1000) id = id * 10 + (*p - '0');
    }
    ++p;
  }

  if (*p == '\0') return;
  if (!broadcast && !(hasDigits && id == module_.id())) return;

  ReplyWriter& out = broadcast ? nullReply : reply_;
  dispatch(*p, p + 1, broadcast, out);
  out.endReply();
}

void AsciiCommand::dispatch(char cmd, const char* arg, bool broadcast,
                            ReplyWriter& out) {
  long value = 0;

  switch (cmd) {
    case '-':
      if (*arg) module_.moveToFlap(*arg);
      break;
    case '+':
      if (*arg >= '0' && *arg <= '9') module_.moveToIndex(atoi(arg));
      break;
    case 'h':
      module_.home();
      break;
    case 'c':
      module_.calibrate();
      break;
    case 'd':
      module_.writeConfig(out);
      break;
    case 'l':
      module_.writeDrum(out);
      break;
    case 'o':
      if (scalar(arg, value)) module_.setHomeOffset((int)value);
      break;
    case 't':
      if (scalar(arg, value)) module_.setStepsPerRev((int)value);
      break;
    case 'g':
      if (scalar(arg, value)) module_.moveToStep((int)value);
      break;
    case 'i':
      if (scalar(arg, value)) module_.setModuleId((int)value);
      break;
    case 'a':
      if (scalar(arg, value)) module_.setAutoHome((int)value);
      break;
    case 'e': {
      ParsedNumber parsed = parseNumber(arg);
      if (parsed.valid) module_.deleteMapPosition((int)parsed.value);
      break;
    }
    case 'w': {
      const char* p = arg;
      ParsedNumber index = parseNumber(p, ':');
      ParsedNumber position = parseNumber(p);
      if (index.valid && position.valid && index.value >= 0 && position.value >= 0)
        module_.setMapPosition((int)index.value, (int)position.value);
      break;
    }
    case 'f': {
      const char* p = arg;
      ParsedNumber index = parseNumber(p, ':');
      if (index.valid && index.value >= 0 && *p != '\0')
        module_.setFlapChar((int)index.value, *p);
      break;
    }
    case 'r': {
      // Index 0 redefines where flap 0 sits, which also updates homing.
      if (*arg >= '0' && *arg <= '9') {
        long index = atol(arg);
        if (index >= 0 && index < 64 && module_.positionKnown()) {
          int current = module_.currentStepPos();
          module_.setMapPosition((int)index, current);
          if (index == 0) module_.setHomeOffset(current);
          writePrefix(out, module_.id(), "r:");
          out.writeNumber(index);
          out.writeChar(':');
          out.writeNumber(current);
        } else {
          writePrefix(out, module_.id(), "r:");
          out.writeNumber(index);
          out.write(":-1");
        }
      }
      break;
    }
    case 's': {
      writePrefix(out, module_.id(), "s:");
      out.writeNumber(module_.currentStepPos());
      out.write(":0:");
      out.writeNumber(module_.positionKnown() ? 1 : 0);
      out.writeChar(':');
      out.writeNumber(module_.busCompat());
      out.writeChar(':');
      module_.writeFirmwareVersion(out);
      break;
    }
    case 'v': {
      writePrefix(out, module_.id(), "v:");
      out.writeNumber(module_.busCompat());
      out.writeChar(':');
      module_.writeFirmwareVersion(out);
      break;
    }
    case 'b': {
      // Broadcast must never drop every module into update mode.
      if (!broadcast) {
        bool ok = module_.requestBootloader();
        writePrefix(out, module_.id(), "b:");
        out.write(ok ? "1" : "0");
        out.endReply();
        if (ok) {
          control_.delayMs(100);
          control_.reset();
        }
      }
      break;
    }
    default:
      break;
  }
}
