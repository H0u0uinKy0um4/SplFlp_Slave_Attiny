#pragma once

#include "CommandPorts.h"

// One deep module: parsing, validation, addressing, dispatch and reply
// emission for the bus language. The interface is one line in; the byte source,
// output stream, module and reset controls sit behind adapters.
class AsciiCommand {
public:
  AsciiCommand(ModulePort& module, ReplyWriter& reply, CommandControl& control);

  void handleLine(const char* line);

private:
  ModulePort& module_;
  ReplyWriter& reply_;
  CommandControl& control_;

  void dispatch(char cmd, const char* arg, bool broadcast, ReplyWriter& out);
};
