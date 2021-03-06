/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <list>
#include <stdint.h>
#include <unistd.h>
#include <memory>

namespace mozquic {

enum keyPhase {
  keyPhaseUnknown,
  keyPhaseUnprotected,
  keyPhase0Rtt,
  keyPhase1Rtt
};

class MozQuicStreamChunk
{
public:
  MozQuicStreamChunk(uint32_t id, uint64_t offset, const unsigned char *data,
                     uint32_t len, bool fin);

  // This form of ctor steals the data pointer. used for retransmit
  MozQuicStreamChunk(MozQuicStreamChunk &);

  ~MozQuicStreamChunk();

  std::unique_ptr<const unsigned char []>mData;
  uint32_t mLen;
  uint32_t mStreamID;
  uint64_t mOffset;
  bool     mFin;

  // when unacked these are set
  uint64_t mPacketNumber;
  uint64_t mTransmitTime; // todo.. hmm if this gets queued for any cc/fc reason (same for ack)
  uint16_t mTransmitCount;
  bool     mRetransmitted; // no data after retransmitted
  enum keyPhase mTransmitKeyPhase;
};

class MozQuicWriter 
{
public:
  // the caller owns the unique_ptr if it returns 0
  virtual uint32_t DoWriter(std::unique_ptr<MozQuicStreamChunk> &p) = 0;
};
  
class MozQuicStreamOut
{
public:
  MozQuicStreamOut(uint32_t id, MozQuicWriter *w);
  ~MozQuicStreamOut();
  uint32_t Write(const unsigned char *data, uint32_t len, bool fin);
  int EndStream();
  bool Done()
  {
    return mFin;
  }

private:
  MozQuicWriter *mWriter;
  uint32_t mStreamID;
  uint64_t mOffset;
  bool mFin;
};

class MozQuicStreamIn
{
public:
  MozQuicStreamIn(uint32_t id);
  ~MozQuicStreamIn();
  uint32_t Read(unsigned char *buffer, uint32_t avail, uint32_t &amt, bool &fin);
  uint32_t Supply(std::unique_ptr<MozQuicStreamChunk> &p);
  bool     Empty();

  bool Done() {
    return (mOffset == mFinOffset) && mFinGivenToApp;
  }

private:
  uint64_t mOffset;
  uint64_t mFinOffset;
  bool     mFinRecvd;
  bool     mFinGivenToApp;
  
  std::list<std::unique_ptr<MozQuicStreamChunk>> mAvailable;
};

class MozQuic;

class MozQuicStreamPair
{
public:
  MozQuicStreamPair(uint32_t id, MozQuicWriter *, MozQuic *);
  ~MozQuicStreamPair();

  uint32_t Supply(std::unique_ptr<MozQuicStreamChunk> &p) {
    return mIn.Supply(p);
  }

  // todo it would be nice to have a zero copy interface
  uint32_t Read(unsigned char *buffer, uint32_t avail, uint32_t &amt, bool &fin) {
    return mIn.Read(buffer, avail, amt, fin);
  }

  bool Empty() {
    return mIn.Empty();
  }

  uint32_t Write(const unsigned char *data, uint32_t len, bool fin) {
    return mOut.Write(data, len, fin);
  }

  int EndStream() {
    return mOut.EndStream();
  }

  bool Done(); // All data and fin bit given to an application and all data are transmitted and acked.
               // todo(or stream has been reseted)
               // the stream can be removed from the stream list.

  uint32_t mStreamID;
  MozQuicStreamOut mOut;
  MozQuicStreamIn  mIn;
  MozQuic *mMozQuic;
};

} //namespace
