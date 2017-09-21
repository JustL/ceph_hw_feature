// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2017 SING LAB, HKUST
 *
 * Author: Justinas Lingys <jlingys@connect.ust.hk>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_RDMA_CONNECTION_H
#define CEPH_RDMA_CONNECTION_H

#include "msg/hardware_async/HwConnection.h"


/**
 * An abstract RDMA class from which each of the RDMA specific
 * classes ingerits. The interface provides operations that 
 * applications can directly use. The underlying implementations
 * (RDMAReliableConnected, RDMAUnreliableConnected, RDMAUnreliableDatagram)
 * provide extra services to the applications (for more information 
 * refer to their files). Since applications can directly use 
 * RDMA libraries for building the networked infrastructure,
 * the strenght of this interface is that it hides the memory
 * management and other low-level procedures from its users.
 * Also, it uses already integrated RDMA components 
 * (src/msg/async/rdma).  
 */
class RDMAConnection : public HwConnection
{


};



#endif /* CEPH_RDMA_CONNECTION_H */
