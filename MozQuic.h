/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

/* This interface is straight C - the library implementation is not. */
/* Eventually this will form an ABI - but right now its a construction zone
   so buyer beware
*/
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

  const char mozquic_alpn[] = "hq-05";
  const uint32_t mozquic_library_version = 1;

  enum {
    MOZQUIC_OK                   = 0,
    MOZQUIC_ERR_GENERAL          = 1,
    MOZQUIC_ERR_INVALID          = 2,
    MOZQUIC_ERR_MEMORY           = 3,
    MOZQUIC_ERR_IO               = 4,
    MOZQUIC_ERR_CRYPTO           = 5,
    MOZQUIC_ERR_VERSION          = 6,
    MOZQUIC_ERR_ALREADY_FINISHED = 7,
  };

  // The event Callbacks receive an application specified closure,
  // an ID (this enum), and the argument pointer defined here
  enum {
    // NAME                              ID     POINTER TYPE
    // --------------------------        ----   -------------------
    MOZQUIC_EVENT_NEW_STREAM_DATA        =  0, // mozquic_stream_t *
    MOZQUIC_EVENT_STREAM_RESET           =  1, // mozquic_stream_t *
    MOZQUIC_EVENT_CONNECTED              =  2, // mozquic_connection_t *
    MOZQUIC_EVENT_ACCEPT_NEW_CONNECTION  =  3, // mozquic_connection_t *
    MOZQUIC_EVENT_CLOSE_CONNECTION       =  4, // mozquic_connection_t *
    MOZQUIC_EVENT_IO                     =  5, // mozquic_connection_t *
    MOZQUIC_EVENT_ERROR                  =  6, // mozquic_connection_t *
    MOZQUIC_EVENT_LOG                    =  7, // char *
    MOZQUIC_EVENT_TRANSMIT               =  8, // mozquic_eventdata_transmit
    MOZQUIC_EVENT_RECV                   =  9, // mozquic_eventdata_recv
    MOZQUIC_EVENT_TLSINPUT               = 10, // mozquic_eventdata_tlsinput
  };

  enum {
    MOZQUIC_AES_128_GCM_SHA256 = 1,
    MOZQUIC_AES_256_GCM_SHA384 = 2,
    MOZQUIC_CHACHA20_POLY1305_SHA256 = 3,
  };
  
  typedef void mozquic_connection_t;
  typedef void mozquic_stream_t;

  struct mozquic_config_t 
  {
    const char *originName;
    int originPort;
    int handleIO; // true if library should schedule read and write events
    void *closure;

    unsigned int greaseVersionNegotiation; // flag
    unsigned int preferMilestoneVersion; // flag
    unsigned int ignorePKI; // flag
    unsigned int tolerateBadALPN; // flag
    unsigned int appHandlesSendRecv; // flag to control TRANSMIT/RECV/TLSINPUT events

    int  (*connection_event_callback)(void *, uint32_t event, void *aParam);
  };

  // this is a hack. it will be come a 'crypto config' and allow server key/cert and
  // some kind of client ca root
  int mozquic_nss_config(char *dir);

  int mozquic_new_connection(mozquic_connection_t **outSession, struct mozquic_config_t *inConfig);
  int mozquic_destroy_connection(mozquic_connection_t *inSession);
  int mozquic_start_client(mozquic_connection_t *inSession); // client rename todo
  int mozquic_start_server(mozquic_connection_t *inSession);
  int mozquic_start_new_stream(mozquic_stream_t **outStream, mozquic_connection_t *conn, void *data, uint32_t amount, int fin);
  int mozquic_send(mozquic_stream_t *stream, void *data, uint32_t amount, int fin);
  int mozquic_end_stream(mozquic_stream_t *stream);
  int mozquic_recv(mozquic_stream_t *stream, void *data, uint32_t aval, uint32_t *amount, int *fin);
  int mozquic_set_event_callback(mozquic_connection_t *conn, int (*fx)(mozquic_connection_t *, uint32_t event, void * param));
  int mozquic_set_event_callback_closure(mozquic_connection_t *conn, void *closure);
  int mozquic_check_peer(mozquic_connection_t *conn, uint32_t deadline);

  ////////////////////////////////////////////////////
  // IO handlers
  // if library is handling IO this does not need to be called
  // otherwise call it to indicate IO should be handled
  int mozquic_IO(mozquic_connection_t *inSession);
  // todo need one to get the pollset

  /* socket typedef */
#ifdef WIN32
  typedef SOCKET mozquic_socket_t;
#else
  typedef int mozquic_socket_t;
#endif

  struct mozquic_eventdata_recv
  {
    unsigned char *pkt;
    uint32_t avail;
    uint32_t *written;
  };

  struct mozquic_eventdata_transmit
  {
    unsigned char *pkt;
    uint32_t len;
    struct sockaddr_in *explicitPeer;
  };

  struct mozquic_eventdata_tlsinput
  {
    unsigned char *data;
    uint32_t len;
  };

  mozquic_socket_t mozquic_osfd(mozquic_connection_t *inSession);
  void mozquic_setosfd(mozquic_connection_t *inSession, mozquic_socket_t fd);

  // the mozquic application may either delegate TLS handling to the lib
  // or may imlement the TLS API : mozquic_handshake_input/output and then
  // mozquic_handshake_complete(ERRORCODE)
  struct mozquic_handshake_info 
  {
    // this is going to form an ABI, so revisit this before v1 release
    // it should probably take the form of having the lib caller do hkdf

    // ciphersuite one of MOZQUIC_AES_128_GCM_SHA256, MOZQUIC_AES_256_GCM_SHA384,
    // MOZQUIC_CHACHA20_POLY1305_SHA256
        
    unsigned int ciphersuite;
    unsigned char sendSecret[48];
    unsigned char recvSecret[48];
  };
    
  void mozquic_handshake_output(mozquic_connection_t *session,
                                unsigned char *data, uint32_t data_len);
  void mozquic_handshake_complete(mozquic_connection_t *session, uint32_t err,
                                  struct mozquic_handshake_info *keyInfo);

#ifdef __cplusplus
}
#endif

