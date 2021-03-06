/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <netinet/ip.h>
#include <stdint.h>
#include <unistd.h>
#include <forward_list>
#include <unordered_map>
#include <memory>
#include <vector>
#include "MozQuicStream.h"
#include "NSSHelper.h"
#include "prnetdb.h"

namespace mozquic {

/* socket typedef */
#ifdef WIN32
#define MOZQUIC_SOCKET_BAD INVALID_SOCKET
#else
#define MOZQUIC_SOCKET_BAD -1
#endif

enum connectionState
{
  STATE_UNINITIALIZED,
  CLIENT_STATE_0RTT,
  CLIENT_STATE_1RTT,
  CLIENT_STATE_CONNECTED,
  CLIENT_STATE_CLOSED,  // todo more shutdown states

  SERVER_STATE_BREAK,
 
  SERVER_STATE_LISTEN,
  SERVER_STATE_0RTT,
  SERVER_STATE_1RTT,
  SERVER_STATE_CONNECTED,
  SERVER_STATE_CLOSED,
};

class MozQuicStreamPair;
class MozQuicStreamAck;
  
class MozQuic final : public MozQuicWriter
{
public:
  static const uint32_t kMozQuicMTU = 1252; // todo pmtud and assumes v4
  static const uint32_t kMinClientInitial = 1200; // an assumption
  static const uint32_t kMozQuicMSS = 16384;

  static const uint32_t kRetransmitThresh = 500;
  static const uint32_t kForgetUnAckedThresh = 4000; // ms
  static const uint32_t kForgetInitialConnectionIDsThresh = 4000; // ms
 
  MozQuic(bool handleIO);
  MozQuic();
  ~MozQuic();

  int StartClient();
  int StartServer();
  int StartNewStream(MozQuicStreamPair **outStream, const void *data, uint32_t amount, bool fin);
  int IO();
  void HandshakeOutput(unsigned char *, uint32_t amt);
  void HandshakeComplete(uint32_t errCode, struct mozquic_handshake_info *keyInfo);

  void SetOriginPort(int port) { mOriginPort = port; }
  void SetOriginName(const char *name);
  void SetClosure(void *closure) { mClosure = closure; }
  void SetConnEventCB(int (*fx)(mozquic_connection_t *,
                      uint32_t event, void * param)) { mConnEventCB = fx; }
  void SetFD(mozquic_socket_t fd) { mFD = fd; }
  int  GetFD() { return mFD; }
  void GreaseVersionNegotiation();
  void PreferMilestoneVersion();
  void SetIgnorePKI() { mIgnorePKI = true; }
  void SetTolerateBadALPN() { mTolerateBadALPN = true; }
  void SetAppHandlesSendRecv() { mAppHandlesSendRecv = true; }
  bool IgnorePKI();
  void DeleteStream(uint32_t streamID);
  void Destroy(uint32_t, const char *);
  uint32_t CheckPeer(uint32_t);

  uint32_t DoWriter(std::unique_ptr<MozQuicStreamChunk> &p) override;
private:
  class LongHeaderData;
  class FrameHeaderData;

  void RaiseError(uint32_t err, char *reason);

  void AckScoreboard(uint64_t num, enum keyPhase kp);
  int MaybeSendAck();

  uint32_t Transmit(unsigned char *, uint32_t len, struct sockaddr_in *peer);
  uint32_t RetransmitTimer();
  uint32_t ClearOldInitialConnectIdsTimer();
  void Acknowledge(uint64_t packetNum, keyPhase kp);
  uint32_t AckPiggyBack(unsigned char *pkt, uint64_t pktNumber, uint32_t avail, keyPhase kp, uint32_t &used);
  uint32_t Recv(unsigned char *, uint32_t len, uint32_t &outLen, struct sockaddr_in *peer);
  int ProcessServerCleartext(unsigned char *, uint32_t size, LongHeaderData &, bool &);
  int ProcessClientInitial(unsigned char *, uint32_t size, struct sockaddr_in *peer,
                           LongHeaderData &, MozQuic **outSession, bool &);
  int ProcessClientCleartext(unsigned char *pkt, uint32_t pktSize, LongHeaderData &, bool&);
  uint32_t ProcessGeneralDecoded(unsigned char *, uint32_t size, bool &, bool fromClearText);
  uint32_t ProcessGeneral(unsigned char *, uint32_t size, uint32_t headerSize, uint64_t packetNumber, bool &);
  bool IntegrityCheck(unsigned char *, uint32_t size);
  void ProcessAck(class FrameHeaderData &result, unsigned char *framePtr, bool fromCleartext);

  bool ServerState() { return mConnectionState > SERVER_STATE_BREAK; }
  MozQuic *FindSession(uint64_t cid);
  void RemoveSession(uint64_t cid);
  void Shutdown(uint32_t, const char *);

  uint64_t Timestamp();
  uint32_t Intake();
  uint32_t Flush();
  uint32_t FlushStream0(bool forceAck);
  uint32_t FlushStream(bool forceAck);
  uint32_t CreateStreamAndAckFrames(unsigned char *&framePtr, unsigned char *endpkt, bool justZero);

  int Client1RTT();
  int Server1RTT();
  void Log(char *);
  int Bind();
  bool VersionOK(uint32_t proposed);
  uint32_t GenerateVersionNegotiation(LongHeaderData &clientHeader, struct sockaddr_in *peer);
  uint32_t ProcessVersionNegotiation(unsigned char *pkt, uint32_t pktSize, LongHeaderData &header);
  int CreateShortPacketHeader(unsigned char *pkt, uint32_t pktSize, uint32_t &used);

  MozQuic *Accept(struct sockaddr_in *peer, uint64_t aConnectionID);

  int FindStream(uint32_t streamID, std::unique_ptr<MozQuicStreamChunk> &d);

  mozquic_socket_t mFD;
  bool mHandleIO;
  bool mIsClient;
  bool mIsChild;
  bool mReceivedServerClearText;
  bool mIgnorePKI;
  bool mTolerateBadALPN;
  bool mAppHandlesSendRecv;
  bool mIsLoopback;
  enum connectionState mConnectionState;
  int mOriginPort;
  std::unique_ptr<char []> mOriginName;
  struct sockaddr_in mPeer; // todo not a v4 world

  uint32_t mVersion;

  // todo mvp lifecycle.. stuff never comes out of here
  std::unordered_map<uint64_t, MozQuic *> mConnectionHash;
  // This maps connectionId sent by a client and connectionId chosen by the
  // server. This is used to detect dup client initial packets.
  // The elemets are going to be removed using a timer.
  struct InitialClientPacketInfo {
    uint64_t mServerConnectionID;
    uint64_t mTimestamp;
  };
  std::unordered_map<uint64_t, struct InitialClientPacketInfo> mConnectionHashOriginalNew;

  uint64_t mConnectionID;
  uint64_t mNextTransmitPacketNumber;
  uint64_t mOriginalTransmitPacketNumber;
  uint64_t mNextRecvPacketNumber; // expected

  void *mClosure;
  int  (*mConnEventCB)(void *, uint32_t, void *);
 
  std::unique_ptr<MozQuicStreamPair> mStream0;
  std::unique_ptr<NSSHelper>         mNSSHelper;

  uint32_t mNextStreamId;
  uint32_t mNextRecvStreamId;
  std::unordered_map<uint32_t, MozQuicStreamPair *> mStreams;

  // wrt munwrittendata and munackeddata. retransmit happens off of
  // munackeddata by duplicating it and placing it in munwrittendata. The
  // dup'd entry is marked retransmitted so it doesn't repeat that. After a
  // certain amount of time the retransmitted packet is just forgotten (as
  // it won't be retransmitted again - that happens to the dup'd
  // incarnation)
  // mUnackedData is sorted by the packet number it was sent in.
  std::list<std::unique_ptr<MozQuicStreamChunk>> mUnWrittenData;
  std::list<std::unique_ptr<MozQuicStreamChunk>> mUnAckedData;

  // macklist is the current state of all unacked acks - maybe written out,
  // maybe not. ordered with the highest packet ack'd at front.Each time
  // the whole set needs to be written out. each entry in acklist contains
  // a vector of pairs (transmitTime, transmitID) representing each time it
  // is written. Upon receipt of an ack we need to find transmitID and
  // remove the entry from the acklist. TODO index by transmitID, but for
  // now iterate from rear (oldest data being acknowledged).
  //
  // acks ordered {1,2,5,6,7} as 7/2, 2/1 (biggest at head)
  std::list<MozQuicStreamAck>                    mAckList;

  // parent and children are only defined on the server
  MozQuic *mParent; // only in child
  std::shared_ptr<MozQuic> mAlive;
  std::list<std::shared_ptr<MozQuic>> mChildren; // only in parent

  // The beginning of a connection.
  uint64_t mTimestampConnBegin;

  uint64_t mPingDeadline;
  bool     mDecodedOK;

  // need other frame 2 list
public: // callbacks from nsshelper
  int32_t NSSInput(void *buf, int32_t amount);
  int32_t NSSOutput(const void *buf, int32_t amount);

public:
  enum FrameType {
    FRAME_TYPE_PADDING           = 0x0,
    FRAME_TYPE_RST_STREAM        = 0x1,
    FRAME_TYPE_CLOSE             = 0x2,
    FRAME_TYPE_GOAWAY            = 0x3,
    FRAME_TYPE_MAX_DATA          = 0x4,
    FRAME_TYPE_MAX_STREAM_DATA   = 0x5,
    FRAME_TYPE_MAX_STREAM_ID     = 0x6,
    FRAME_TYPE_PING              = 0x7,
    FRAME_TYPE_BLOCKED           = 0x8,
    FRAME_TYPE_STREAM_BLOCKED    = 0x9,
    FRAME_TYPE_STREAM_ID_NEEDED  = 0xA,
    FRAME_TYPE_NEW_CONNECTION_ID = 0xB,
    // ACK                       = 0xa0 - 0xbf
    FRAME_MASK_ACK               = 0xe0,
    FRAME_TYPE_ACK               = 0xa0, // 101. ....
    // STREAM                    = 0xc0 - 0xff
    FRAME_MASK_STREAM            = 0xc0,
    FRAME_TYPE_STREAM            = 0xc0, // 11.. ....
  };
 
  enum FrameTypeLengths {
    FRAME_TYPE_PADDING_LENGTH           = 1,
    FRAME_TYPE_RST_STREAM_LENGTH        = 17,
    FRAME_TYPE_CLOSE_LENGTH             = 7,
    FRAME_TYPE_GOAWAY_LENGTH            = 9,
    FRAME_TYPE_MAX_DATA_LENGTH          = 9,
    FRAME_TYPE_MAX_STREAM_DATA_LENGTH   = 13,
    FRAME_TYPE_MAX_STREAM_ID_LENGTH     = 5,
    FRAME_TYPE_PING_LENGTH              = 1,
    FRAME_TYPE_BLOCKED_LENGTH           = 1,
    FRAME_TYPE_STREAM_BLOCKED_LENGTH    = 5,
    FRAME_TYPE_STREAM_ID_NEEDED_LENGTH  = 1,
    FRAME_TYPE_NEW_CONNECTION_ID_LENGTH = 11
  };

  enum LongHeaderType {
    PACKET_TYPE_VERSION_NEGOTIATION    = 1,
    PACKET_TYPE_CLIENT_INITIAL         = 2,
    PACKET_TYPE_SERVER_STATELESS_RETRY = 3,
    PACKET_TYPE_SERVER_CLEARTEXT       = 4,
    PACKET_TYPE_CLIENT_CLEARTEXT       = 5,
    PACKET_TYPE_0RTT_PROTECTED         = 6,
    PACKET_TYPE_1RTT_PROTECTED_KP0     = 7,
    PACKET_TYPE_1RTT_PROTECTED_KP1     = 8,
    PACKET_TYPE_PUBLIC_RESET           = 9,
  };

private:
  class LongHeaderData
  {
  public:
    LongHeaderData(unsigned char *, uint32_t);
    enum LongHeaderType mType;
    uint64_t mConnectionID;
    uint64_t mPacketNumber;
    uint32_t mVersion;
  };

  class ShortHeaderData
  {
  public:
    ShortHeaderData(unsigned char *, uint32_t, uint64_t);
    uint32_t mHeaderSize;
    uint64_t mConnectionID;
    uint64_t mPacketNumber;
  };

  class FrameHeaderData
  {
  public:
    FrameHeaderData(unsigned char *, uint32_t, MozQuic *);
    FrameType mType;
    uint32_t  mValid;
    uint32_t  mFrameLen;
    union {
      struct {
        bool mFinBit;
        uint16_t mDataLen;
        uint32_t mStreamID;
        uint64_t mOffset;
      } mStream;
      struct {
        uint8_t mAckBlockLengthLen;
        uint8_t mNumBlocks;
        uint8_t mNumTS;
        uint64_t mLargestAcked;
        uint16_t mAckDelay;
      } mAck;
      struct {
        uint32_t mErrorCode;
        uint32_t mStreamID;
        uint64_t mFinalOffset;
      } mRstStream;
      struct {
        uint32_t mErrorCode;
      } mClose;
      struct {
        uint32_t mClientStreamID;
        uint32_t mServerStreamID;
      } mGoaway;
      struct {
        uint64_t mMaximumData;
      } mMaxData;
      struct {
        uint32_t mStreamID;
        uint64_t mMaximumStreamData;
      } mMaxStreamData;
      struct {
        uint32_t mMaximumStreamID;
      } mMaxStreamID;
      struct {
        uint32_t mStreamID;
      } mStreamBlocked;
      struct {
        uint16_t mSequence;
        uint64_t mConnectionID;
      } mNewConnectionID;
    } u;
  };
  bool Unprotected(MozQuic::LongHeaderType type);
  static uint64_t DecodePacketNumber(unsigned char *pkt, int pnSize, uint64_t next);

};

class MozQuicStreamAck
{
public:
  MozQuicStreamAck(uint64_t num, uint64_t rtime, enum keyPhase kp)
    : mPacketNumber(num)
    , mExtra(0)
    , mPhase (kp)
    , mTimestampTransmitted(false)
  {
    mReceiveTime.push_front(rtime);
  }

  // num=10, mExtra=3 means we are acking 10, 9, 8, 7
  // and ReceiveTime applies to 10
  uint64_t mPacketNumber; // being ACKd
  uint64_t mExtra;
  std::list<uint64_t> mReceiveTime;
  enum keyPhase mPhase;
  bool mTimestampTransmitted;

  // pair.first is packet number of transmitted ack
  // pair.second is transmission time
  std::vector<std::pair<uint64_t, uint64_t>> mTransmits;
  
  bool Transmitted() { return !mTransmits.empty(); }
};

} //namespace
