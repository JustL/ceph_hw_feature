// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2017 HKSUT SING LAB
 *
 * Author: 
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_RDMA_SOCKET_INTERFACE_H
#define CEPH_RDMA_SOCKET_INTERFACE_H
#include "msg/hardware_async/Stack.h"

/**
 * RDMA socket is an interface that hides the implementation
 * of the socket and provides an interface for specific implementations
 * of the RDMA sockets.
 */

class RDMASocket : public ConnectedSocketImpl
{
  public:

    enum class RDMASocketType : unsigned int
    {
      RC_SOCK = 0, // reliable connected
      UC_SOCK = 1, // unreliabel connected
      UD_SOCK = 2  // unreliable datagram
    };

    virtual RDMASocket() {}
    virtual ~RDMASocket() override {}
    
    // repeat the interfaces from the ConnectedSocketImpl
    // implementation
    virtual int is_connected() override = 0;
    virtual ssize_t read(char*, size_t) override = 0;
    virtual ssize_t zero_copy_read(bufferptr&) override = 0;
    virtual ssize_t send(bufferlist &bl, bool more) override = 0;
    virtual void shutdown() override = 0;
    virtual void close() override = 0;
    virtual int fd() const override = 0;
    virtual bool can_read_memory() const override = 0;
    virtual bool can_remotely_write() const override = 0;
    virtual ssize_t get_memory_chunk(const size_t, bufferptr&) override = 0;
    virtual ssize_t write_remotely(const size_t, bufferptr&) override = 0;
    virtual bool is_reliable() const override = 0;
    
    // the below methods are taken from the RDMAConnectedSocketImpl
    // (msg/async/rdma/RDMAStack.h)
    virtual void fault()  = 0; // taken from the RDMAConnectedSocketImpl
                               // source: msg/async/rmda/RDMAStack.h

    virtual ssize_t submit(const bool more) = 0; // taken from the 
                                                // RDMAConnectedSocketImpl
    virtual void set_pending(const bool val) = 0;
    virtual bool is_pending() const = 0;
    virtual int try_connect(const entity_addr &, const SocketOptions &) = 0;
    virtual void pass_wc(std::vector<ibv_wc> &&v);
    virtual void get_wc(std::vector<ibv_wc>& wc);
    virtual cont char* get_qp_state() const = 0;
    virtual int activate() = 0;
    virtual void fin() = 0;
    virtual void handle_connection() = 0;
    virtual void cleanup() = 0;
    virtual void set_accept_fd(const int sd) = 0;
   

    virtual RDMASocketType get_sock_type() const = 0;
    virtual void release_memory(const size_t, bufferptr &addr) = 0; 

}; // RDMASocket

#endif /* CEPH_RDMA_SOCKET_INTERFACE_H */
