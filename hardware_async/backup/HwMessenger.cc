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

#include "acconfig.h"

#include <iostream>
#include <fstream>

#include "HwMessenger.h"
#include "rdma/RDMAProcessor.h"

#include "common/config.h"
#include "common/Timer.h"
#include "common/errno.h"

#include "messages/MOSDOp.h"
#include "messages/MOSDOpReply.h"
#include "common/EventTrace.h"

#define dout_subsys ceph_subsys_ms
#undef dout_prefix
#define dout_prefix _prefix(_dout, this)
static ostream& _prefix(std::ostream *_dout, AsyncMessenger *m) {
  return *_dout << "-- " << m->get_myaddr() << " ";
}

static ostream& _prefix(std::ostream *_dout, Processor *p) {
  return *_dout << " Processor -- ";
}


/*******************
 * Processor interface used by HwMEssengers to handle
 * incoming messages.
 */

Processor* Processor::create_processor(HwMessenger *msgr, Worker *wrk,
                                       CephContext *cct, const string &type)
{
  // At the moment, only the RDMAProcessor is supported

  if(type == "rdma") // taken from the constructor 
                     // of the AsyncMessenger class
  {
    return new RDMAProcessor(msgr, wrk, cct);
  }

  return nullptr;

}


struct StackSingleton {
  CephContext *cct;
  std::shared_ptr<NetworkStack> stack;

  StackSingleton(CephContext *c): cct(c) {}
  void ready(std::string &type) {
    if (!stack)
      stack = NetworkStack::create(cct, type);
  }
  ~StackSingleton() {
    stack->stop();
  }
};


class C_handle_reap : public EventCallback {
  AsyncMessenger *msgr;

  public:
  explicit C_handle_reap(AsyncMessenger *m): msgr(m) {}
  void do_request(int id) override {
    // judge whether is a time event
    msgr->reap_dead();
  }
};

/*******************
 * AsyncMessenger
 */

AsyncMessenger::AsyncMessenger(CephContext *cct, entity_name_t name,
                               const std::string &type, string mname, uint64_t _nonce)
  : SimplePolicyMessenger(cct, name,mname, _nonce),
    dispatch_queue(cct, this, mname),
    lock("AsyncMessenger::lock"),
    nonce(_nonce), need_addr(true), did_bind(false),
    global_seq(0), deleted_lock("AsyncMessenger::deleted_lock"),
    cluster_protocol(0), stopped(true)
{
  std::string transport_type = "posix";
  if (type.find("rdma") != std::string::npos)
    transport_type = "rdma";
  else if (type.find("dpdk") != std::string::npos)
    transport_type = "dpdk";

  StackSingleton *single;
  cct->lookup_or_create_singleton_object<StackSingleton>(single, "AsyncMessenger::NetworkStack::"+transport_type);
  single->ready(transport_type);
  stack = single->stack.get();
  stack->start();
  local_worker = stack->get_worker();
  local_connection = new AsyncConnection(cct, this, &dispatch_queue, local_worker);
  init_local_connection();
  reap_handler = new C_handle_reap(this);
  unsigned processor_num = 1;
  if (stack->support_local_listen_table())
    processor_num = stack->get_num_worker();
  for (unsigned i = 0; i < processor_num; ++i)
    processors.push_back(new Processor(this, stack->get_worker(i), cct));
}

/**
 * Destroy the AsyncMessenger. Pretty simple since all the work is done
 * elsewhere.
 */
AsyncMessenger::~AsyncMessenger()
{
  delete reap_handler;
  assert(!did_bind); // either we didn't bind or we shut down the Processor
  local_connection->mark_down();
  for (auto &&p : processors)
    delete p;
}

void AsyncMessenger::ready()
{
  ldout(cct,10) << __func__ << " " << get_myaddr() << dendl;

  stack->ready();
  if (pending_bind) {
    int err = bind(pending_bind_addr);
    if (err) {
      lderr(cct) << __func__ << " postponed bind failed" << dendl;
      ceph_abort();
    }
  }

  Mutex::Locker l(lock);
  for (auto &&p : processors)
    p->start();
  dispatch_queue.start();
}

int AsyncMessenger::shutdown()
{
  ldout(cct,10) << __func__ << " " << get_myaddr() << dendl;

  // done!  clean up.
  for (auto &&p : processors)
    p->stop();
  mark_down_all();
  // break ref cycles on the loopback connection
  local_connection->set_priv(NULL);
  did_bind = false;
  lock.Lock();
  stop_cond.Signal();
  stopped = true;
  lock.Unlock();
  stack->drain();
  return 0;
}


int AsyncMessenger::bind(const entity_addr_t &bind_addr)
{
  lock.Lock();

  if (!pending_bind && started) {
    ldout(cct,10) << __func__ << " already started" << dendl;
    lock.Unlock();
    return -1;
  }

  ldout(cct,10) << __func__ << " bind " << bind_addr << dendl;

  if (!stack->is_ready()) {
    ldout(cct, 10) << __func__ << " Network Stack is not ready for bind yet - postponed" << dendl;
    pending_bind_addr = bind_addr;
    pending_bind = true;
    lock.Unlock();
    return 0;
  }

  lock.Unlock();

  // bind to a socket
  set<int> avoid_ports;
  entity_addr_t bound_addr;
  unsigned i = 0;
  for (auto &&p : processors) {
    int r = p->bind(bind_addr, avoid_ports, &bound_addr);
    if (r) {
      // Note: this is related to local tcp listen table problem.
      // Posix(default kernel implementation) backend shares listen table
      // in the kernel, so all threads can use the same listen table naturally
      // and only one thread need to bind. But other backends(like dpdk) uses local
      // listen table, we need to bind/listen tcp port for each worker. So if the
      // first worker failed to bind, it could be think the normal error then handle
      // it, like port is used case. But if the first worker successfully to bind
      // but the second worker failed, it's not expected and we need to assert
      // here
      assert(i == 0);
      return r;
    }
    ++i;
  }
  _finish_bind(bind_addr, bound_addr);
  return 0;
}

int AsyncMessenger::rebind(const set<int>& avoid_ports)
{
  ldout(cct,1) << __func__ << " rebind avoid " << avoid_ports << dendl;
  assert(did_bind);

  for (auto &&p : processors)
    p->stop();
  mark_down_all();

  // adjust the nonce; we want our entity_addr_t to be truly unique.
  nonce += 1000000;
  ldout(cct, 10) << __func__ << " new nonce " << nonce
		 << " and inst " << get_myinst() << dendl;

  entity_addr_t bound_addr;
  entity_addr_t bind_addr = get_myaddr();
  bind_addr.set_port(0);
  set<int> new_avoid(avoid_ports);
  new_avoid.insert(bind_addr.get_port());
  ldout(cct, 10) << __func__ << " will try " << bind_addr
		 << " and avoid ports " << new_avoid << dendl;
  unsigned i = 0;
  for (auto &&p : processors) {
    int r = p->bind(bind_addr, avoid_ports, &bound_addr);
    if (r) {
      assert(i == 0);
      return r;
    }
    ++i;
  }
  _finish_bind(bind_addr, bound_addr);
  for (auto &&p : processors) {
    p->start();
  }
  return 0;
}

int AsyncMessenger::client_bind(const entity_addr_t &bind_addr)
{
  if (!cct->_conf->ms_bind_before_connect)
    return 0;
  Mutex::Locker l(lock);
  if (did_bind) {
    assert(my_inst.addr == bind_addr);
    return 0;
  }
  if (started) {
    ldout(cct, 10) << __func__ << " already started" << dendl;
    return -1;
  }
  ldout(cct, 10) << __func__ << " " << bind_addr << dendl;

  set_myaddr(bind_addr);
  return 0;
}

void AsyncMessenger::_finish_bind(const entity_addr_t& bind_addr,
				  const entity_addr_t& listen_addr)
{
  set_myaddr(bind_addr);
  if (bind_addr != entity_addr_t())
    learned_addr(bind_addr);

  if (get_myaddr().get_port() == 0) {
    set_myaddr(listen_addr);
  }
  entity_addr_t addr = get_myaddr();
  addr.set_nonce(nonce);
  set_myaddr(addr);

  init_local_connection();

  ldout(cct,1) << __func__ << " bind my_inst.addr is " << get_myaddr() << dendl;
  did_bind = true;
}

int AsyncMessenger::start()
{
  lock.Lock();
  ldout(cct,1) << __func__ << " start" << dendl;

  // register at least one entity, first!
  assert(my_inst.name.type() >= 0);

  assert(!started);
  started = true;
  stopped = false;

  if (!did_bind) {
    my_inst.addr.nonce = nonce;
    _init_local_connection();
  }

  lock.Unlock();
  return 0;
}

void AsyncMessenger::wait()
{
  lock.Lock();
  if (!started) {
    lock.Unlock();
    return;
  }
  if (!stopped)
    stop_cond.Wait(lock);

  lock.Unlock();

  dispatch_queue.shutdown();
  if (dispatch_queue.is_started()) {
    ldout(cct, 10) << __func__ << ": waiting for dispatch queue" << dendl;
    dispatch_queue.wait();
    dispatch_queue.discard_local();
    ldout(cct, 10) << __func__ << ": dispatch queue is stopped" << dendl;
  }

  // close all connections
  shutdown_connections(false);
  stack->drain();

  ldout(cct, 10) << __func__ << ": done." << dendl;
  ldout(cct, 1) << __func__ << " complete." << dendl;
  started = false;
}

void AsyncMessenger::add_accept(Worker *w, ConnectedSocket cli_socket, entity_addr_t &addr)
{
  lock.Lock();
  AsyncConnectionRef conn = new AsyncConnection(cct, this, &dispatch_queue, w);
  conn->accept(std::move(cli_socket), addr);
  accepting_conns.insert(conn);
  lock.Unlock();
}

AsyncConnectionRef AsyncMessenger::create_connect(const entity_addr_t& addr, int type)
{
  assert(lock.is_locked());
  assert(addr != my_inst.addr);

  ldout(cct, 10) << __func__ << " " << addr
      << ", creating connection and registering" << dendl;

  // create connection
  Worker *w = stack->get_worker();
  AsyncConnectionRef conn = new AsyncConnection(cct, this, &dispatch_queue, w);
  conn->connect(addr, type);
  assert(!conns.count(addr));
  conns[addr] = conn;
  w->get_perf_counter()->inc(l_msgr_active_connections);

  return conn;
}

ConnectionRef AsyncMessenger::get_connection(const entity_inst_t& dest)
{
  Mutex::Locker l(lock);
  if (my_inst.addr == dest.addr) {
    // local
    return local_connection;
  }

  AsyncConnectionRef conn = _lookup_conn(dest.addr);
  if (conn) {
    ldout(cct, 10) << __func__ << " " << dest << " existing " << conn << dendl;
  } else {
    conn = create_connect(dest.addr, dest.name.type());
    ldout(cct, 10) << __func__ << " " << dest << " new " << conn << dendl;
  }

  return conn;
}

ConnectionRef AsyncMessenger::get_loopback_connection()
{
  return local_connection;
}

int AsyncMessenger::_send_message(Message *m, const entity_inst_t& dest)
{
  FUNCTRACE();
  assert(m);

  if (m->get_type() == CEPH_MSG_OSD_OP)
    OID_EVENT_TRACE(((MOSDOp *)m)->get_oid().name.c_str(), "SEND_MSG_OSD_OP");
  else if (m->get_type() == CEPH_MSG_OSD_OPREPLY)
    OID_EVENT_TRACE(((MOSDOpReply *)m)->get_oid().name.c_str(), "SEND_MSG_OSD_OP_REPLY");

  ldout(cct, 1) << __func__ << "--> " << dest.name << " "
      << dest.addr << " -- " << *m << " -- ?+"
      << m->get_data().length() << " " << m << dendl;

  if (dest.addr == entity_addr_t()) {
    ldout(cct,0) << __func__ <<  " message " << *m
        << " with empty dest " << dest.addr << dendl;
    m->put();
    return -EINVAL;
  }

  AsyncConnectionRef conn = _lookup_conn(dest.addr);
  submit_message(m, conn, dest.addr, dest.name.type());
  return 0;
}

void AsyncMessenger::submit_message(Message *m, AsyncConnectionRef con,
                                    const entity_addr_t& dest_addr, int dest_type)
{
  if (cct->_conf->ms_dump_on_send) {
    m->encode(-1, MSG_CRC_ALL);
    ldout(cct, 0) << __func__ << "submit_message " << *m << "\n";
    m->get_payload().hexdump(*_dout);
    if (m->get_data().length() > 0) {
      *_dout << " data:\n";
      m->get_data().hexdump(*_dout);
    }
    *_dout << dendl;
    m->clear_payload();
  }

  // existing connection?
  if (con) {
    con->send_message(m);
    return ;
  }

  // local?
  if (my_inst.addr == dest_addr) {
    // local
    local_connection->send_message(m);
    return ;
  }

  // remote, no existing connection.
  const Policy& policy = get_policy(dest_type);
  if (policy.server) {
    ldout(cct, 20) << __func__ << " " << *m << " remote, " << dest_addr
        << ", lossy server for target type "
        << ceph_entity_type_name(dest_type) << ", no session, dropping." << dendl;
    m->put();
  } else {
    ldout(cct,20) << __func__ << " " << *m << " remote, " << dest_addr << ", new connection." << dendl;
    con = create_connect(dest_addr, dest_type);
    con->send_message(m);
  }
}

/**
 * If my_inst.addr doesn't have an IP set, this function
 * will fill it in from the passed addr. Otherwise it does nothing and returns.
 */
void AsyncMessenger::set_addr_unknowns(const entity_addr_t &addr)
{
  Mutex::Locker l(lock);
  if (my_inst.addr.is_blank_ip()) {
    int port = my_inst.addr.get_port();
    my_inst.addr.u = addr.u;
    my_inst.addr.set_port(port);
    _init_local_connection();
  }
}

void AsyncMessenger::set_addr(const entity_addr_t &addr)
{
  Mutex::Locker l(lock);
  entity_addr_t t = addr;
  t.set_nonce(nonce);
  set_myaddr(t);
  _init_local_connection();
}

void AsyncMessenger::shutdown_connections(bool queue_reset)
{
  ldout(cct,1) << __func__ << " " << dendl;
  lock.Lock();
  for (set<AsyncConnectionRef>::iterator q = accepting_conns.begin();
       q != accepting_conns.end(); ++q) {
    AsyncConnectionRef p = *q;
    ldout(cct, 5) << __func__ << " accepting_conn " << p.get() << dendl;
    p->stop(queue_reset);
  }
  accepting_conns.clear();

  while (!conns.empty()) {
    ceph::unordered_map<entity_addr_t, AsyncConnectionRef>::iterator it = conns.begin();
    AsyncConnectionRef p = it->second;
    ldout(cct, 5) << __func__ << " mark down " << it->first << " " << p << dendl;
    conns.erase(it);
    p->get_perf_counter()->dec(l_msgr_active_connections);
    p->stop(queue_reset);
  }

  {
    Mutex::Locker l(deleted_lock);
    while (!deleted_conns.empty()) {
      set<AsyncConnectionRef>::iterator it = deleted_conns.begin();
      AsyncConnectionRef p = *it;
      ldout(cct, 5) << __func__ << " delete " << p << dendl;
      deleted_conns.erase(it);
    }
  }
  lock.Unlock();
}

void AsyncMessenger::mark_down(const entity_addr_t& addr)
{
  lock.Lock();
  AsyncConnectionRef p = _lookup_conn(addr);
  if (p) {
    ldout(cct, 1) << __func__ << " " << addr << " -- " << p << dendl;
    p->stop(true);
  } else {
    ldout(cct, 1) << __func__ << " " << addr << " -- connection dne" << dendl;
  }
  lock.Unlock();
}

int AsyncMessenger::get_proto_version(int peer_type, bool connect) const
{
  int my_type = my_inst.name.type();

  // set reply protocol version
  if (peer_type == my_type) {
    // internal
    return cluster_protocol;
  } else {
    // public
    switch (connect ? peer_type : my_type) {
      case CEPH_ENTITY_TYPE_OSD: return CEPH_OSDC_PROTOCOL;
      case CEPH_ENTITY_TYPE_MDS: return CEPH_MDSC_PROTOCOL;
      case CEPH_ENTITY_TYPE_MON: return CEPH_MONC_PROTOCOL;
    }
  }
  return 0;
}

void AsyncMessenger::learned_addr(const entity_addr_t &peer_addr_for_me)
{
  // be careful here: multiple threads may block here, and readers of
  // my_inst.addr do NOT hold any lock.

  // this always goes from true -> false under the protection of the
  // mutex.  if it is already false, we need not retake the mutex at
  // all.
  if (!need_addr)
    return ;
  lock.Lock();
  if (need_addr) {
    need_addr = false;
    entity_addr_t t = peer_addr_for_me;
    t.set_port(my_inst.addr.get_port());
    t.set_nonce(my_inst.addr.get_nonce());
    my_inst.addr = t;
    ldout(cct, 1) << __func__ << " learned my addr " << my_inst.addr << dendl;
    _init_local_connection();
  }
  lock.Unlock();
}

int AsyncMessenger::reap_dead()
{
  ldout(cct, 1) << __func__ << " start" << dendl;
  int num = 0;

  Mutex::Locker l1(lock);
  Mutex::Locker l2(deleted_lock);

  while (!deleted_conns.empty()) {
    auto it = deleted_conns.begin();
    AsyncConnectionRef p = *it;
    ldout(cct, 5) << __func__ << " delete " << p << dendl;
    auto conns_it = conns.find(p->peer_addr);
    if (conns_it != conns.end() && conns_it->second == p)
      conns.erase(conns_it);
    accepting_conns.erase(p);
    deleted_conns.erase(it);
    ++num;
  }

  return num;
}
