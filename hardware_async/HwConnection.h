// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_MSG_HWCONNECTION_H
#define CEPH_MSG_HWCONNECTION_H


#include <boost/intrusive_ptr.hpp>
#include "msg/Connection.h"

// ======================================================


class Message;
class HwMessenger;


/**
 * HwConnection is an abstract class that provides a communication
 * interface for a Hardware Messenger, and hides most low-level
 * implementation details from the messenger. The interface is 
 * supposed to be used with hardware devices which support some
 * kind of a logic communication pattern between two endpoints;
 * for example, an RDMA device that supports a hardware-level 
 * communication with remote RDMA device.
 * The interface shall be implmented by classes related to a particular
 * hardware-aware pattern.
 *
 */


class HwConnection : public Connection 
{
  public:
    HwConnection(CephContext *cct, HwMessenger *m)
      : Connection(cct, m), 
        hw_messenger(m)
    {}

    virtual ~HwConnection override {}
    
    // Connection.h virtual functions
    // For detailed information, refer to msg/Connection.h
    virtual bool is_connected() override = 0;
    virtual int  send_message(Message *m) override = 0;
    virtual void send_keepalive() override = 0;
    virtual void mark_down() override = 0;
    virtual void mark_disposable() override = 0;


    // Below methods are only used by HwMessengers    
    // to interact with a connection.
    
    /**
     * This method is only used on a newly instantiated 
     * hardware connection. The method creates a logical
     * pipe between two end hosts. May not be used by all
     * implmentations.
     */
    virtual void connect(const entity_addr_t& addr, int type) = 0;
 
   /** 
    * This method provides an interface for Hw Messengers
    * for adding new incoming connections.
    */ 
    virtual void accept() = 0;


   /**
    * Method checks if the connection is ready to send
    * data. This method is more hardware specific since
    * harware also supports not connected connections,
    * so we need a way of determining when we can send
    * data to the connection (use it).
    *
    * @return : return true when data can be passed to 
    *           the newly instantiated connection
    */ 
    virtual bool ready_for_data(void) const = 0;


   /**
    * Metod returns performance counters to the 
    * messenger to observe the performance of 
    * a connection.
    *
    * @return : perforamnce counters (specific to the implementation)
    */
    PerfCounters* get_perf_counter(void) = 0; 

    
    friend class boost::intrusive_ptr<HwConnection>;

  protected:
    HwMessenger* hw_messenger;


}; // HwConnection


typedef boost::intrusive_ptr<HwConnection> HwConnectionRef;

#endif /* CEPH_MSG_HWCONNECTION_H */
