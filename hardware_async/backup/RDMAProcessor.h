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

#ifndef NEW_RDMA_CONNECTION_PROCESSOR_H
#define NEW_RDMA_CONNECTION_PROCESSOR_H


#include "msg/hardware_async/HwMessenger.h"


// ====================================================


/**
 * RDMAProcessor uses an RDMA library to initiate an RDMA
 * connection. The class is used in the HwMessenger class
 * as the implmentation of the Processor interface which is
 * used by the HwMessenger class as the handler of new 
 * incoming connection requests. The implementation is 
 * RDMA-specific, hence it assumes that the machine that is
 * running the process and the machines generating connection
 * requests have RDMA NICs.
 */

 class RDMAProcessor: public Processor
 {
   public:
     RDMAProcessor(HwMessenger *msgr, Worker *w, CephContext *c);
     virtual void stop() override;
     virtual int  bind(const entity_addr_t &bind_addr,
                       const set<int>& avoid_ports,
                       entity_addr_t* bound_addr) override;
     virtual void start()  override; 
     virtual void accept() override;
     virtual ~RDMAProcessor() override;

 }; // RDMAProcessor

#endif /* NEW_RDMA_CONNECTION_PROCESSOR_H */
