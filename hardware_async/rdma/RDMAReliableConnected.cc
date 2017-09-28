// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 
 *
 * Author: 
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <unistd.h>
#include <typeinfo>

#include "include/Context.h"
#include "include/int_types.h"
#include "common/errno.h"
#include "HwMessenger.h"
#include "RDMAReliableConnected.h"

#include "messages/MOSDOp.h"
#include "messages/MOSDOpReply.h"
#include "common/EventTrace.h"

// Constant to limit starting sequence number to 2^31.  Nothing special about it, just a big number.  PLR
#define SEQ_MASK  0x7fffffff 

#define dout_subsys ceph_subsys_ms
#undef dout_prefix
#define dout_prefix _conn_prefix(_dout)
ostream& RDMAReliableConnected::_conn_prefix(std::ostream *_dout) {
  return *_dout << "-- " << hw_msgr->get_myinst().addr << " >> " << peer_addr << " conn(" << this
                << " :" << port
                << " s=" << get_state_name(state)
                << " pgs=" << peer_global_seq
                << " cs=" << connect_seq
                << " l=" << policy.lossy
                << ").";
}

// Notes:
// 1. Don't dispatch any event when closed! It may cause RDMAReliableConnected alive even if HwMessenger dead

class C_time_wakeup : public EventCallback {
  RDMAReliableConnected conn;

 public:
  explicit C_time_wakeup(RDMAReliableConnected c): conn(c) {}
  void do_request(int fd_or_id) override {
    conn->wakeup_from(fd_or_id);
  }
};

class C_handle_read : public EventCallback {
  RDMAReliableConnected conn;

 public:
  explicit C_handle_read(RDMAReliableConnected c): conn(c) {}
  void do_request(int fd_or_id) override {
    conn->process();
  }
};

class C_handle_write : public EventCallback {
  RDMAReliableConnected conn;

 public:
  explicit C_handle_write(RDMAReliableConnected c): conn(c) {}
  void do_request(int fd) override {
    conn->handle_write();
  }
};

class C_clean_handler : public EventCallback {
  RDMAReliableConnected conn;
 public:
  explicit C_clean_handler(RDMAReliableConnected c): conn(c) {}
  void do_request(int id) override {
    conn->cleanup();
    delete this;
  }
};

class C_tick_wakeup : public EventCallback {
  RDMAReliableConnected conn;

 public:
  explicit C_tick_wakeup(RDMAReliableConnected c): conn(c) {}
  void do_request(int fd_or_id) override {
    conn->tick(fd_or_id);
  }
};



RDMAReliableConnected::RDMAReliableConnected(CephContex *cct, HwMessenger *m, DispatchQueue *d, Worker *w)
: RDMAConnection(cct, m, d, w),
  hw_msgr(m),
  conn_id(q->get_id()),
  logger(w->get_perf_counter()),
  global_seq(0),
  connect_seq(0),
  peer_global_seq(0),
  state(RC_State::STATE_NONE),
  state_after_send(RC_State::STATE_NONE),
  port(-1),
  dispatch_queue(q),
  can_write(WriteStatus::NOWRITE),
  keepalive(false),
  last_active(ceph::coarse_mono_clock::now()),
  inactive_timeout_us(cct->_conf->ms_tcp_read_timeout*1000*1000),
  got_bad_auth(false),
  authorizer(nullptr),
  replacing(false),
  is_reset_from_peer(false),
  once_ready(false),
  worker(w),
  center(&(w->center))
{
  read_handler   =  new C_handle_read(this);
  write_handler  =  new C_handle_write(this);
  wakeup_handler =  new C_time_wakeup(this);
  tick_handler   =  new C_tick_wakeup(this);
  memset(msgvec, 0, sizeof(msgvec));
  logger->inc(l_msgr_created_connections);
}

RDMAReliableConnected::~RDMAReliableConnected()
{
  assert(out_q.empty());
  assert(sent.empty());
  
  delete authorizer;

}


// from src/msg/async/AsyncConnection.cc:
/* return -1 means 'fd' occurs error or closed, it should be closed
*  return 0 means EAGAIN or EINTR 
*/
ssize_t RDMAReliableConnected::read_zero_copy(bufferptr &bPtr)
{
  ssize_t read_data;
  
  again:

    // an RDMA socket is used, use zero_copy_read method
    
    // have an error code or a bool value that signal if
    // the socket supports zero_copy_read?
    read_data = cs.zero_copy_read(bPtr);
    
    // an error has occured
    if(read_data < 0)
    {
      if(read_data == -EAGAIN)
      {
        read_data  = 0;
      }
      else
          if(reead_data == -EINTR)
          {
            goto again;
          }
          else
          {
            ldout(hw_msgr->ctt, 1) << __func__ << " reading from fd=" << cs.fd() << " : " << strerror(read_data) << dendl;
            
            return -1;

          }
          
    }
    else 
        if(read_data == 0)
         {
           ldout(hw_msgr->ctt, 1) << __func__ << " peer close file descriptor " << cs.fd() << dendl;
           
           return -1;
         }


    // return read data in bytes
    return read_data; 

}


ssize_t RDMAReliableConnected::read_until(const unsigned int len, char *p)
{


}


void RDMAReliableConnected::process()
{
  ssize_t rec_code = 0;

  RC_State prev_state = state;

  bool need_dispatch_writer = false;

  std:lock_guard<std::mutex> l(lock);
  // ensure that only one thread accesses the below code

  last_active = ceph::coarse_mono_clock::now();

  auto recv_start_time = ceph::mono_clock_now();
  // actions depend on the current state of the connection
  do
  {
    ldout(hw_msgr->cct, 20) << __func__ << "prev_state is " << get_state_name(prev_state) << dendl;
    prev_state = state;
    
    // same as AsyncConnection
    // perform similar actions in similar states
    switch(state)
    {
      case RC_State::STATE_CONNECTING:
        {  // follows the protocol stated on the ceph developer's page
           assert(!policy.server);
           // follows AsyncConnection:
           // reset state variables
           got_bad_auth = false;
           delter authorizer;
           authorizer = nullptr;
           authorizer_buf.clear();
           memset(&connect_msg, 0, sizeof(connect_msg));
           memset(&connect_reply, 0, sizeof(connect_reply));
         
           // reset global sequence of this connection
           global_seq = hw_msgr->get_global_seq();
           // close the previous socket. According to comments in
           // src/msg/async/AsyncCnnection.cc, it should be safe
           if(cs)
           {
             center->delete_file_event(cs.fd(), EVENT_READABLE|EVENT_WRITABLE)
;
             cs.close();
           }
          
           // initialize the local connection
           // and try to connect
           SocketOptions opts;

           // remember, this is an RDMA socket
           opt.priority = hw_msgr->get_socket_priority();
           opts.connect_bind_addr = he_msgr->get_myaddr();
           rec_code = worker->connect(get_peer_addr(), opts, &cs);
           if (rec_code < 0)
           {
             goto fail;
           }
           center->create_file_event(cs.fd(), EVENT_READABLE, read_handler);
           state = RC_State::STATE_CONNECTING_RE;
           break;      

         }
       case RC_State::STATE_CONNECTING_RE:
         {
           rec_code = cs.is_connected();
         
           // the handshake phase has failed
           if(rec_code < 0)
           {
             ldout(hw_msgr->cct, 1) << __func__ << " reconnect failed " << dendl;
             if(rec_code == -ECONNREFUSED)
             {
               ldout(hw_msgr->cct, 2) << __func__ << " connection refused!" << dendl;
               dispatch_queue->queue_refused(this);
             }

             goto fail;
           
           }
           else
           {
             if(rec_code == 0)
             {
               ldout(hw_msgr->cct, 10) << __func__ << " nonblock connect in progress" << dendl;
               if(hw_msgr->get_stack()->nonblock_connect_need_writable_event())
               {
                 center->create_file_event(cs.fd(), EVENT_WRITABLE, read_handler);           break;
               }
             }
           }

           // rec_code > 0
           center->delete_file_event(cs.fd(), EVENT_WRITABLE);
           ldout(hw_msgr->cct, 10) << __func__ << " connected successfully, ready to send banner" << dendl;
         
            bufferlist bl; // banner buffer list
            bl.append(CEPH_BANNER, strlen(CEPH_BANNER));
            rec_code = try_send(bl); // try to send the banner
          
            // banner sent, check the status
            if(rec_code == 0)
            {
              state = RC_State::STATE_CONNECTING_WAIT_BANNER_AND_IDENTIFY;
              ldout(hw_msgr->cct, 10) << __func__ << " connect write banner done: " << get_peer_addr() << dendl;

            }
            else
            {
              if(rec_code > 0)
              {
                state = RC_State::STATE_WAIT_SEND;
                state_after_send = STATE_CONNECTING_WAIT_BANNER_AND_IDENTIFY;
                ldout(hw_msgr->cct, 10) << __func__ << " connect wait for write banner: " << get_peer_addr() << dendl;
                
              }
              else
              {
                goto fail;
              }
            }
            
            break; // succesfully sent/enqueued banner

         }     
       case RC_State::STATE_CONNECTING_WAIT_BANNER_AND_IDENTIFY:
         {
           // sent the banner, received a reply
           entity_addr_t paddr, peer_addr_for_me;
           bufferlist myaddrbl;
           const size_t banner_len = strlen(CEPH_BANNER);
           const size_t need_len = banner_len + sizeof(ceph_entity_addr)*2;
           rec_code = read_until(need_len);// changed code for reading/read_zero
           
           if(rec_code < 0)
           {
             ldout(hw_msgr->cct, 1) << __func__ << " read_banner and identify addresses failed" << dendl;
             goto fail;
           }
           else
           {
             // need to check if the number of values have been read
             if(rec_code > 0)
             {
               break;
             }
             
           }
           
           // wrong banner version?
           if(memcmp(..., CEPH_BANNER, banner_len))
           {
             ldout(hw_msgr->cct, 0) << __func__ << " connect protoocol error (bad banner) on peer" << get_peer_addr() << dendl;
             goto fail;
           }
           
           // decode the received data
           bufferlist bl;
           // need to have an RDMA implementation
           // no need for reading the data from memory
           bl.append(banner_len, sizeof(ceph_entity_addr)*2);
           bufferlist::iterator itr = bl.begin();
           
           try
           {
             ::decode(paddr, itr);
             ::decode(peer_addr_for_me, itr);
           }
           catch(const buffer::error &exp)
           {
             lderr(hw_msgr->cct) << __func__ << " decode peer addr failed "  << dendl;
             goto fail;
           }

           // decoded peer address 
           ldout(hw_msgr->cct, 20) << __func__  << " connect read peer addr " << paddr << " on socket " << cs.fd() << dendl;
           
           // check if the addresses used to connect and decoded
           // one match
           if(peer_addr != paddr)
           {
             if(paddr.is_blank_ip() && peer_addr.get_port() == paddr.get_port() && peer_addr.get_nonce() == paddr.get_nonce())
             {
               ldout(hw_msgr->cct, 0) << __func__ << " connect claims to be " << paddr << " not " << peer_addr << " - presumably this is the same node! " << dendl;
               
             }
             else
             {
               ldout(hw_msgr->cct, 10) << __func__ << " connect claims to be " << paddr << " not " << peer_addr << dendl;
               goto fail;
             }
           }
           
           ldout(hw_msgr->cct, 20) << __func__ << " connect peer and for me is " << peer_addr_for_me << dendl;
           
          
           hw_msgr->learned_addr(peer_addr_for_me);
           
           ::encode(hw_msgr->get_myaddr(), myaddrbl, 0); 
           rec_code = try_send(myaddrbl);
           
           if(rec_code == 0)
           {
             state = RC_State::STATE_CONNECTING_SEND_CONNECT_MSG;
             ldout(hw_msgr->cct, 10) << __func__ << " connect sent my addr" << hw_msgr->get_myaddr() << dendl;
           }
           else
           {
             if(rec_code > 0)
             {
               state = RC_State::STATE_WAIT_SEND;
               state_after_send = RC_State::STATE_CONNECTING_SEND_CONNECT_MSG;
               ldout(hw_msgr->cct, 10) << __func__ << " connect my addr done: " << hw_msgr->get_myaddr() << dendl;
               
             }
             else
             {
               ldout(hw_msgr->cct, 2) << __func__ << " connect couldn't write my addr, " << cpp_strerror(rec_code) << dendl;
               goto fail; 
             }
           }           
         }

                 
    }// switch 


  
  }while(state != prev_state);
}



void RDMAReliableConnected::_connect()
{
  ldout(hw_msgr->cct, 10) << __func__ << " csq=" << connect_seq << dendl;

  state = RC_State::STATE_CONNECTING;


  // same as AsyncConnection
  center->dispatch_event_external(read_handler);

}


void RDMAReliableConnected::accept(ConnectedSocket socket, entity_addr_t &addr)
{
  ldout(async_msgr->>cct, 10) << __func__ << " sd=" << sokcet.fd() << dendl;

  assert(socket.fd() >=0);

  std::lock_guard<std::mutex> l(lock);
  cs = std::move(socket);
  socket_addr = addr;
  state = RC_State::STATE_ACCEPTING;
  // reshedule connection in order to avoid lock dep
  center->dispatch_event_external(read_handler);


}


// src/msg/async/AsyncConnection.cc:
// return the remaining bytes, it may be larger than the length
// of ptr; else return < 0 for an error
ssize_t RDMAReliableConnected::_try_send(const bool more)
{

  assert(center->in_thread());
  // underlying sokcet is an RDMA socket, so
  // it does not copy the data, but only claims
  // pointers to the data --> no data copying
  const ssize_t sent_size = cs.send(outcoming_bl, more); 
  
  if(sent_size < 0)
  {
    ldout(hw_msgr->cct, 1) << __func__ << " send error: " << cpp_strerror(sent_size) << dendl;

    return sent_size;
  }

  ldout(hw_msgr->cct, 10) << __func__ << " sent bytes " << sent_size 
  << " remaining  bytes " << outcoming_bl.length() << dendl;

  // need to write to the file descriptor (socket)
  if(!open_write && is_queued())
  {
    center->create_file_event(cs.fd(), EVENT_WRITABLE, write_handler);
    open_write = true;
  }

  // nothing to send
  if(open_write && !is_queued())
  {
    center->delete_file_event(cs.fd(), EVENT_WRITABLE);
    open_write = false;

    if(state_after_sent != START_NONE)
      center->dispatch_event_external(read_handler);
  }

  // succesfully sent that many bytes
  return outcoming_bl.length();
  
}

