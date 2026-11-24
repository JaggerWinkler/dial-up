// File Name:     console.h
// Author:        Arthur Ufongene
// Date:          November 2026

// Description: thread-safe console output shared by the REPL and the receive thread,
// plus helpers for formatting MAC addresses

#ifndef DIAL_UP_CONSOLE_H
#define DIAL_UP_CONSOLE_H

#include "util.h"

#include <string>

// Console output shared by the REPL and the receive thread. Every call takes
// one lock, so lines from the two threads never interleave.
namespace console {

void out(const std::string& line);      // stdout
void err(const std::string& line);      // stderr
void prompt();                          // "> " without a newline

std::string macToString(const MAC_Address& mac);   // aa:bb:cc:dd:ee:ff

}  // namespace console

#endif // DIAL_UP_CONSOLE_H
