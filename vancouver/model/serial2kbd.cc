/**
 * Converts ascii chars to keystrokes.
 *
 * Copyright (C) 2007-2009, Bernhard Kauer <bk@vmmon.org>
 *
 * This file is part of Vancouver.
 *
 * Vancouver is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vancouver is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include "vmm/motherboard.h"
#include "models/keyboard.h"

/**
 * A bridge on the host bus that converts serial chars to keystrokes.
 *
 * State: testing
 * Features: alphanumeric chars, ANSI escape codes
 * Missing: some special chars, see keyboard.h for details.
 */
class SerialKbdBridge : public StaticReceiver<SerialKbdBridge>
{
  DBus<MessageKeycode> &_bus_keycode;
  unsigned _serial;
  unsigned _keyboard;
  unsigned char _escape;
  unsigned char _escape_chars[5];
  const char *debug_getname() { return "SerialKbdBridge"; };


  /**
   * Translate ascii code to SCS2 scancodes for an US keyboard layout.
   */
  unsigned translate_ascii(unsigned char key)
  {
    unsigned res = 0;
    if (key < 128)
      res = ascii_map[key];
    return res;
  }


  /**
   * Translate ansi escape sequences
   */ 
  unsigned translate_ansi(unsigned char key)
  {
    _escape_chars[_escape++] = key;
    for (unsigned i=0; i < sizeof(ansi_map) / sizeof(ansi_map[0]); i++)
      {
	unsigned len = strlen(ansi_map[i].escape);
	if (len + 1 == _escape && !memcmp(_escape_chars+1, ansi_map[i].escape, len))
	  {
	    _escape = 0;
	    return ansi_map[i].keycode;
	  }
      }
    // if we have more than 4 escape keys stored -> drop them to avoid overflow
    if (_escape > 4) _escape = 0;
    return 0;
  }


  void send_key(unsigned keycode)
  {
    MessageKeycode msg(_keyboard, keycode);
    _bus_keycode.send(msg);
  }

public:
  bool  receive(MessageSerial &msg)
  {
    if (msg.serial != _serial)   return false;

    //Logging::printf("%s -- %lx -> %x \n", __PRETTY_FUNCTION__, msg.ch, _escape);
    unsigned keycode;
    if (msg.ch == 0x1b || _escape)
      keycode = translate_ansi(msg.ch);
    else
      keycode = translate_ascii(msg.ch);

    //Logging::printf("%s -- %lx -> %x %x \n", __PRETTY_FUNCTION__, msg.ch, _escape, keycode);
    if (keycode)
      {
	if (keycode & KBFLAG_LSHIFT) send_key(0x12);
	send_key(keycode);
	send_key(KBFLAG_RELEASE | keycode);
	if (keycode & KBFLAG_LSHIFT)
	  send_key(KBFLAG_RELEASE | 0x12);
      }
    return true;
  }


  SerialKbdBridge(DBus<MessageKeycode> &bus_keycode, unsigned serial, unsigned keyboard) : _bus_keycode(bus_keycode), _serial(serial), _keyboard(keyboard), _escape(0) {}
};


PARAM(serial2kbd,
      {
	Device *dev = new SerialKbdBridge(mb.bus_keycode, argv[0], argv[1]);
	mb.bus_serial.add(dev, &SerialKbdBridge::receive_static<MessageSerial>);
      },
      "serial2kbd:serial,keyboard - attach a bridge between serial and keyboard.",
      "Example: 'serial2kbd:0x4711,0x2bad'.",
      "The serial input at source serialbus is transformed to keystrokes on the dest keycodebus.");
