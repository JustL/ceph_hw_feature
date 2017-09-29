// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2016 XSKY <haomai@xsky.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_RDMA_RELIABLE_CONNECTED_SOCKET_IMPL_H
#define CEPH_RDMA_RELIABLE_CONNECTED_SOCKET_IMPL_H

#include "RDMASocket.h"
#include "RDMAStack.h"

class RDMAReliableConnectedSocketImpl : public RDMASocket {
 public:
  typedef Infiniband::MemoryManager::Chunk Chunk;
  typedef Infiniband::CompletionChannel CompletionChannel;
  typedef Infiniband::CompletionQueue CompletionQueue;

 private:
  static const RDMASocket::RDMATypeSocket RDMA_SOCKET_TYPE = RDMASocket::RDMATypeSocket::RC_SOCK; 


  CephContext *cct;
  Infiniband::QueuePair *qp;
  IBSYNMsg peer_msg;
  IBSYNMsg my_msg;
  int connected;
  int error;
  Infiniband* infiniband;
  RDMADispatcher* dispatcher;
  RDMAWorker* worker;
  std::vector<Chunk*> buffers; // overall memory of this socket
  std::map<char*, Chunk*> res_mem; // zero copy memory which is passed to
                                  // upper layer to process
  int notify_fd;
  bufferlist pending_bl;

  Mutex lock;
  std::vector<ibv_wc> wc;
  bool is_server;
  EventCallbackRef con_handler;
  int tcp_fd;
  bool active;// qp is active ?
  bool pending;

  void notify();
  ssize_t read_buffers(char* buf, size_t len);
  ssize_t read_buffers_zero_copy(bufferptr &addr);

  int post_work_request(std::vector<Chunk*>&);

 public:
  RDMAReliableConnectedSocketImpl(CephContext *cct, Infiniband* ib, RDMADispatcher* s,
                          RDMAWorker *w);
  virtual ~RDMAReliableConnectedSocketImpl() override;

  virtual void pass_wc(std::vector<ibv_wc> &&v) override;
  virtual void get_wc(std::vector<ibv_wc> &w) override;
  virtual int is_connected() override { return connected; }
  
  // RDMA related ConnectedSocket methods
  virtual bool can_read_memory() const override { return true; }
  virtual bool can_remotely_write() const override { return true; }
  virtual ssize_t get_memory_chunk(const size_t, bufferptr &addr) override;
  virtual ssize_t write_remotely(const size_t, bufferptr &locMem) override;
  virtual bool is_reliable() const override { return true; }
  virtual RDMASocket::RDMASocketType get_sock_type() const { return RDMAReliableConnectedSocketImpl::RDMA_SOCKET_TYPE; }
  virtual void release_memory(const size_t, bufferptr &addr) override;

  virtual ssize_t read(char* buf, size_t len) override;
  virtual ssize_t zero_copy_read(bufferptr &data) override;
  virtual ssize_t send(bufferlist &bl, bool more) override;
  virtual void shutdown() override;
  virtual void close() override;
  virtual int fd() const override { return notify_fd; }
  virtual void fault() override;
  virtual const char* get_qp_state() const { return Infiniband::qp_state_string(qp->get_state()); }
  virtual ssize_t submit(const bool more) override;
  virtual int activate() override;
  virtual void fin() override;
  virtual void handle_connection() override;
  virtual void cleanup() override;
  virtual void set_accept_fd(const int sd) override;
  virtual int try_connect(const entity_addr_t&, const SocketOptions &opt) override;
  virtual bool is_pending() const override {return pending;}
  virtual void set_pending(const bool val) override {pending = val;}
  class C_handle_connection : public EventCallback {
    RDMAConnectedSocketImpl *csi;
    bool active;
   public:
    C_handle_connection(RDMAReliableConnectedSocketImpl *w): csi(w), active(true) {}
    void do_request(int fd) {
      if (active)
        csi->handle_connection();
    }
    void close() {
      active = false;
    }
  };
};


#endif /* CEPH_RDMA_RELIABLE_CONNECTED_SOCKET_IMPL_H */
