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

// system headers
#include <cstdlib>


#include <boost/intrusive_ptr.hpp>
#include "msg/hardware_async/HwConnection.h"


/**
 * An abstract RDMA class from which each of the RDMA specific
 * classes inherits. The interface provides operations that 
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


  public:
 
    // Specific type of an RDMA connection
    enum class RDMAConnType : unsigned
    {
      RC_RDMA = 1, // reliable connected
      UC_RDMA = 2, // unreliable connected
      UD_RDMA = 3  // unreliable datagram
    };

    RDMAConnection(CephContext *c, HwMessenger *m, DispatchQueue *d, Worker *w, const RDMAConnType rdma_type);

    virtual ~RDMAConnection() override;

    virtual bool rdma_read_avail() const = 0;
    virtual bool rdma_write_avail() const = 0;
    virtual bool is_reliable() const = 0;
    virtual bool is_datagram() const = 0;


    virtual std::ssize_t get_memory(const std::ssize_t size_mem, void* addr_mem) {return -1;}
  
    virtual std::ssize_t rdma_read_memory(const std::ssize_t read_bytes, void* addr_mem) {return -1;}
  
    virtual std::ssize_t rdma_write_memory(const std::ssize_t write_bytes, void* addr_mem) {return -1;}


    virtual std::ssize_t rdma_send(const std::ssize_t bytes, void* addr_mem){return -1;}

    virtual std::ssize_t rdma_recv(const std::ssize_t bytes, void* addr_mem){return -1;} 

    virtual RDMAConnType get_RDMA_type(void) const
    { return m_rdma_type; }


    friend class boost::intrusive_ptr<RDMAConnection>;

  protected:
    RDMAConnType m_rdma_type;
    // may add an extra abstraction
    // inheritance may not be the best way of
    // implementing different types
    // of RDMAConnection. May just use an interface
    // of a socket that is passed and the
    // same connection class.
    // This way it is very easy to convert a 
    // connection from one type to a different one.
    // However, now sticking to inheritance due to 
    // already provided RDMA facilities in src/msg/async/rdma/
    
    /* RDMASocket* m_socket*/     


}; // RDMAConnection


    typedef boost::intrusive_ptr<RDMAConnection> RDMAConnectonRef;


#endif /* CEPH_RDMA_CONNECTION_H */
