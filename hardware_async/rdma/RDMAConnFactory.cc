// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 UnitedStack <haomai@unitedstack.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */


#include <memory>

#include "RDMAConnFactory.h"

// RDMA Connections
#include "RDMAReliableConnected.h"


RDMAConnFactory::RDMAConnFactory(const std::string &connType)
  HwConnectionFactory(std::move("rdma")), rdma_type(RDMAConnType::RC_RDMA)
{
  // Set the type that this factory will use for creating
  // new connections. Default is RC_RDMA
  
  if(connType.find("uc-rdma"))
  {
    rdma_type = RDMAConnType::UC_RDMA;
  }
  else
  {
    if(connType.find("ud-rdma"))
    {
      rdma_type = RDMAConnType::UD_RDMA;
    }
  }

}

RDMAConnFactory::~RDMAConnFactory()
{}


HwConnection* RDMAConnFactory::create_connection(CephContext *c,
                                                 HwMessenger *m,
                                                 DispatchQueue *d,
                                                 Worker w*)

{
  return create_RDMA_connection(c,m,d,w);
}

RDMAConnection* RDMAConnFactory::create_RDMA_connection(CephContext *c, 
                                                 HwMessenger *m,
                                               DispatchQueue *d, 
                                                      Worker *w)
{
  // Instantiate a particular RDMA connection

  // At the moment, only the RDMAReliableConnected is supported
  return new RDMAReliableConnected(c, m, d, w);

  /*

  switch(rdma_type)
  {
    RDMAConnType::UC_RDMA: return new RDMAUnreliableConnected(c, m, d, w);
    RDMAConnType::UD_RDMA: return new RDMAUnreliableDatagram(c, m, d, w);
    RDMAConnType::RC_RDMA:
    default:               return new RDMAReliableConnected(c, m, d, w);
  }
  
  */
}


void RDMAConnFactory::setRDMAConnType(const RDMAConnFactory::RDMAConnType type)
{
  rdma_type = type;
}


RDMAConnFactory::RDMAConnType RDMAConnFactory::getRDMAConnType(void) const
{
  return rdma_type;
}
