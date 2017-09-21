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

#ifndef CEPH_RDMA_CONN_FACTORY_H
#define CEPH_RDMA_CONN_FACTORY_H


#include <string>
#include "msg/hardware_async/HwMessenger.h"
#include "RDMAConnection.h"

/**
 * RDMA implementation of the HwConnectionFactory interface which
 * is used by HwMessenger to create new connections. The interface
 * hides the implementations from the messenger so that an instnace
 * of the HwConnectionFactory interface could be reused to instantiate
 * different hardware-related connections.
 * This class provides the HwConnectionFactory interface with RDMA
 * connections (RC-RDMA, UC-RDMA, and UG-RDMA). The type of a new
 * RDMA connection is passed to the constructor of the factory.
 */
class RDMAConnFactory : public HwConnectionFactory
{
  public:
   
    RDMAConnFactory(const std::string &connType);
    RDMAConnFactory(const RDMAConnFacotry &other) = delete;
    RDMAConnFactory& operator=(const RDMAConnFacotry &other) = delete;
    virtual ~RDMAConnFactory() override;

    /**
     * The abstract method inherited from the ConnectionFactory
     * interface. THe method instantiates new hardware connections.
     *
     * @param c : reference to the CephContext for creating connections
     * @param m : reference to the messenger for the created connection
     * @param d : dispatch queue used by the crreated connection
     * @param w : reference to the worker that handles the connection
     * 
     *  @return : a new RDMAConnection;
     */

    virtual HwConnection* create_connection(CephContext *c, HwMessenger *m,
                                     DispatchQueue *d, Worker *w) override;

   
    /**
     * Method creates RDMAConnections that are specified by 
     * setting/initialiazing the factory.
     *
     * Same parameters as to the create_connection method.
     */
    RDMAConnection* create_RDMA_connection(CephContext *c, HwMessenger *m,
                                     DispatchQueue *d, Worker *w);


   /**
    * Method for setting the connection type of the factory.
    *
    * @param connType : connection type that will be created
    */
    void setRDMAConnType(const RDMAConnection::RDMAConnType connType);


   /**
    * Returns the socket type that is being used by the factory.
    *
    * @return : RDMA connection type
    **/
    RDMAConnection::RDMAConnType getRDMAConnType(void) const;

  private:
    RDMAConnection::RDMAConnType rdma_type; 

}; // RDMAConnFactory



#endif /* CEPH_RDMA_CONN_FACTORY_H */
