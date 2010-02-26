/*
 * Generic NOVA producer/consumer code based on shared memory and
 * semaphores.
 *
 * Copyright (C) 2008-2009, Bernhard Kauer <bk@vmmon.org>
 *
 * This file is part of Vancouver.
 *
 * Vancouver.nova is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * Vancouver.nova is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */
#pragma once
#include "sys/semaphore.h"
#include "service/profile.h"


/**
 * Consumer with fixed items.
 */
template <typename T, unsigned SIZE>
class Consumer
{
public:
  KernelSemaphore _sem;
  volatile unsigned  _rpos;
  volatile unsigned  _wpos;
  T         _buffer[SIZE];
  unsigned sm() { return _sem.sm(); }

  T * get_buffer() {
    _sem.down();
    return _buffer + _rpos;
  }

  void free_buffer()  { _rpos = (_rpos + 1) % SIZE; }


  Consumer(unsigned cap_sm) : _sem(KernelSemaphore(cap_sm)),  _rpos(0), _wpos(0)
  {
    unsigned res;
    if ((res = create_sm(cap_sm)))
      Logging::panic("create Consumer failed with %x\n", res);
    Logging::printf("create Consumer ok with %x nq %x\n", res, cap_sm);
  }
};



/**
 * Producer with fixed items.
 */
template <typename T, unsigned SIZE>
class Producer
{
protected:
  Consumer<T, SIZE> *_consumer;
  KernelSemaphore    _sem;
  bool               _dropping;

public:
  /**
   * Put something in the buffer. Please note that this function is
   * not locked, thus only a single producer should do the access at
   * the very same time.
   */
  bool produce(T &value)
  {
    if (!_consumer || ((_consumer->_wpos + 1) % SIZE == _consumer->_rpos))
      {
	_dropping = true;
	return false;
      }
    _dropping = false;
    _consumer->_buffer[_consumer->_wpos] = value;
    _consumer->_wpos = (_consumer->_wpos + 1) % SIZE;
    _sem.up();
    return true;
  }

  Producer(Consumer<T, SIZE> *consumer = 0, unsigned nq = 0) : _consumer(consumer), _sem(nq) {};
};


/**
 * Packet consumer that supports variable sized packets.
 */
template <unsigned SIZE>
class PacketConsumer : public Consumer<unsigned, SIZE>
{
  typedef Consumer<unsigned, SIZE> Parent;
public:
  /**
   * Get a pointer to a the buffer and return the length of the buffer.
   */
  unsigned get_buffer(unsigned char *&buffer)
  {
    unsigned *len = Parent::get_buffer();
    if (*len == ~0u)
      {
	Parent::_rpos = 0;
	len = Parent::_buffer + Parent::_rpos;
      }
    assert(*len < sizeof(unsigned) * SIZE);
    buffer =  reinterpret_cast<unsigned char *>(Parent::_buffer + Parent::_rpos + 1);
    return *len;
  }

  void free_buffer()
  {
    Parent::_rpos = (Parent::_rpos + (Parent::_buffer[Parent::_rpos] + 2*sizeof(unsigned) - 1)/sizeof(unsigned)) % SIZE;
  }

  PacketConsumer(unsigned cap_sm) : Parent(cap_sm) {}
};


/**
 * Packet producer that supports variable sized packets.
 */
template <unsigned SIZE>
class PacketProducer : public Producer<unsigned, SIZE>
{
  typedef Producer<unsigned, SIZE> Parent;
public:
  PacketProducer(PacketConsumer<SIZE> *consumer=0, unsigned cap_nq=0) : Producer<unsigned, SIZE>(consumer, cap_nq) {}

  /**
   * Put something in the buffer. Please note that this function is
   * not locked, thus only a single producer should do the access at
   * the very same time.
   */
 bool produce(const unsigned char *buf, unsigned len)
  {
    if (!Parent::_consumer || !len) return false;
    unsigned right = SIZE - Parent::_consumer->_wpos;
    unsigned left = Parent::_consumer->_rpos;
    unsigned needed = (len + 2*sizeof(unsigned) - 1) / sizeof(unsigned);
    if (left > Parent::_consumer->_wpos)
      {
	right = left - Parent::_consumer->_wpos;
	left = 0;
      }

    if ((needed > right) && (needed > left))
      {
	COUNTER_INC("NET drop");
	COUNTER_SET("NET rpos", Parent::_consumer->_rpos);
	COUNTER_SET("NET wpos", Parent::_consumer->_wpos);
	COUNTER_SET("NET size", len);
	Parent::_dropping = true;
	return false;
      }
    Parent::_dropping = false;

    unsigned ofs = Parent::_consumer->_wpos;
    if (right < needed) {
      if (right !=0) Parent::_consumer->_buffer[ofs] = ~0u;
      ofs = 0;
    }
    Parent::_consumer->_buffer[ofs] = len;
    memcpy(Parent::_consumer->_buffer + ofs + 1, buf, len);
    assert(ofs + needed <= SIZE);
    if (ofs + needed == SIZE)
      Parent::_consumer->_wpos = 0;
    else
      Parent::_consumer->_wpos = ofs + needed;
    Parent::_sem.up();
    return true;
  }
};