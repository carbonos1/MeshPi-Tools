/***************************************************************************
 *   Copyright (C) 2004 by Francisco J. Ros                                *
 *   fjrm@dif.um.es                                                        *
 *   Adapted for omnetpp                                                   *
 *   2008 Alfonso Ariza Quintana aarizaq@uma.es                            *
 *                                                                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

///
/// \file   OLSR.cc
/// \brief  Implementation of OLSR agent and related classes.
///
/// This is the main file of this software because %OLSR's behaviour is
/// implemented here.
///




#include <math.h>
#include <limits.h>
#include "inet/routing/extras/olsr/OlrsPkt_m.h"
#include "inet/routing/extras/olsr/Olrs.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/L3Tools.h"
#include "inet/networklayer/ipv4/Ipv4InterfaceData.h"


namespace inet {
namespace inetmanet {

/// Length (in bytes) of UDP header.
#define UDP_HDR_LEN 8
/// Port Number
#define RT_PORT 698
#define IP_DEF_TTL 32

#define MULTIPLE_IFACES_SUPPORT
#define state_      (*state_ptr)

///
/// \brief Function called by MAC layer when cannot deliver a packet.
///
/// \param p Packet which couldn't be delivered.
/// \param arg OLSR agent passed for a callback.
///

Define_Module(Olsr);


std::ostream& operator<<(std::ostream& os, const Olsr_rt_entry& e)
{
    os << "dest:"<< e.dest_addr_.str() << " ";
    os << "gw:" << e.next_addr_.str() << " ";
    os << "iface:" << e.iface_addr_.str() << " ";
    os << "dist:" << e.dist_ << " ";
    return os;
};


uint32_t OlsrAddressSize::ADDR_SIZE = ADDR_SIZE_DEFAULT;
Olsr::GlobalRtable Olsr::globalRtable;
Olsr::DistributionPath Olsr::distributionPath;

/********** Timers **********/


///
/// \brief Sends a HELLO message and reschedules the HELLO timer.
/// \param e The event which has expired.
///
void
Olsr_HelloTimer::expire()
{

    auto ag = dynamic_cast<Olsr*>(agent_);
    ag->send_hello();
    // agent_->scheduleAt(simTime()+agent_->hello_ival_- JITTER,this);
    simtime_t next = simTime() + ag->hello_ival() - ag->jitter();
    this->resched(next);
}

///
/// \brief Sends a TC message (if there exists any MPR selector) and reschedules the TC timer.
/// \param e The event which has expired.
///
void
Olsr_TcTimer::expire()
{
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->mprselset().size() > 0)
        ag->send_tc();
    // agent_->scheduleAt(simTime()+agent_->tc_ival_- JITTER,this);
    simtime_t next = simTime() + ag->tc_ival() - ag->jitter();
    this->resched(next);
}

///
/// \brief Sends a MID message (if the node has more than one interface) and resets the MID timer.
/// \warning Currently it does nothing because there is no support for multiple interfaces.
/// \param e The event which has expired.
///
void
Olsr_MidTimer::expire()
{
#ifdef MULTIPLE_IFACES_SUPPORT
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->isInMacLayer())
        return; // not multi-interface support
    ag->send_mid();
    simtime_t next = simTime() +  ag->mid_ival() - ag->jitter();
    this->resched(next);
#endif
}

///
/// \brief Removes tuple_ if expired. Else timer is rescheduled to expire at tuple_->time().
///
/// The task of actually removing the tuple is left to the OLSR agent.
///
/// \param e The event which has expired.
///


void
Olsr_DupTupleTimer::expire()
{
    Olsr_dup_tuple* tuple = dynamic_cast<Olsr_dup_tuple*> (tuple_);
    double time = tuple->time();
    if (time < SIMTIME_DBL(simTime())) {
        removeTimer();
        delete this;
    }
    else {
        // agent_->scheduleAt (simTime()+DELAY_T(time),this);
        simtime_t next = simTime() +  DELAY_T(time);
        this->resched(next);
    }
}

Olsr_DupTupleTimer::~Olsr_DupTupleTimer()
{
    removeTimer();
    if (!tuple_)
        return;
    Olsr_dup_tuple* tuple = dynamic_cast<Olsr_dup_tuple*> (tuple_);
    tuple->asocTimer = nullptr;
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->state_ptr == nullptr)
        return;
    ag->rm_dup_tuple(tuple);
    delete tuple_;
}

///
/// \brief Removes tuple_ if expired. Else if symmetric time
/// has expired then it is assumed a neighbor loss and agent_->nb_loss()
/// is called. In this case the timer is rescheduled to expire at
/// tuple_->time(). Otherwise the timer is rescheduled to expire at
/// the minimum between tuple_->time() and tuple_->sym_time().
///
/// The task of actually removing the tuple is left to the OLSR agent.
///
/// \param e The event which has expired.
///

Olsr_LinkTupleTimer::Olsr_LinkTupleTimer(ManetRoutingBase* agent, Olsr_link_tuple* tuple) : Olsr_Timer(agent)
{
    tuple_ = tuple;
    tuple->asocTimer = this;
    first_time_ = true;
}

void
Olsr_LinkTupleTimer::expire()
{
    double now;
    now = SIMTIME_DBL(simTime());
    Olsr_link_tuple* tuple = dynamic_cast<Olsr_link_tuple*> (tuple_);
    if (tuple->time() < now)
    {
        removeTimer();
        delete this;
    }
    else if (tuple->sym_time() < now)
    {
        auto ag = dynamic_cast<Olsr*>(agent_);
        if (first_time_)
            first_time_ = false;
        else
            ag->nb_loss(tuple);
        // agent_->scheduleAt (simTime()+DELAY_T(tuple_->time()),this);
        simtime_t next = simTime() +  DELAY_T(tuple->time());
        this->resched(next);
        //agent_->timerQueuePtr->insert(std::pair<simtime_t, OLSR_Timer *>(simTime()+DELAY_T(tuple->time()), this));
    }
    else
    {
        // agent_->scheduleAt (simTime()+DELAY_T(MIN(tuple_->time(), tuple_->sym_time())),this);
        simtime_t next = simTime()+DELAY_T(MIN(tuple->time(), tuple->sym_time()));
        this->resched(next);
        //agent_->timerQueuePtr->insert(std::pair<simtime_t, OLSR_Timer *>(simTime()+DELAY_T(MIN(tuple->time(), tuple->sym_time())), this));
    }
}

Olsr_LinkTupleTimer::~Olsr_LinkTupleTimer()
{
    removeTimer();
    if (!tuple_)
        return;
    Olsr_link_tuple* tuple = dynamic_cast<Olsr_link_tuple*> (tuple_);
    tuple->asocTimer = nullptr;
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->state_ptr==nullptr)
        return;
    ag->rm_link_tuple(tuple);
    ag->setTopologyChanged(true);
    delete tuple_;
}

///
/// \brief Removes tuple_ if expired. Else the timer is rescheduled to expire at tuple_->time().
///
/// The task of actually removing the tuple is left to the OLSR agent.
///
/// \param e The event which has expired.
///

void
Olsr_Nb2hopTupleTimer::expire()
{
    Olsr_nb2hop_tuple* tuple = dynamic_cast<Olsr_nb2hop_tuple*> (tuple_);
    double time = tuple->time();
    if (time < SIMTIME_DBL(simTime()))
    {
        removeTimer();
        delete this;
    }
    else
    {
        simtime_t next = simTime()+DELAY_T(time);
        this->resched(next);
        // agent_->scheduleAt (simTime()+DELAY_T(time),this);
        //agent_->timerQueuePtr->insert(std::pair<simtime_t, OLSR_Timer *>(simTime()+DELAY_T(time), this));
    }
}

Olsr_Nb2hopTupleTimer::~Olsr_Nb2hopTupleTimer()
{
    removeTimer();
    if (!tuple_)
        return;
    Olsr_nb2hop_tuple* tuple = dynamic_cast<Olsr_nb2hop_tuple*> (tuple_);
    tuple->asocTimer = nullptr;
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->state_ptr == nullptr)
        return;
    ag->rm_nb2hop_tuple(tuple);
    ag->setTopologyChanged(true);
    delete tuple_;
}


///
/// \brief Removes tuple_ if expired. Else the timer is rescheduled to expire at tuple_->time().
///
/// The task of actually removing the tuple is left to the OLSR agent.
///
/// \param e The event which has expired.
///


void
Olsr_MprSelTupleTimer::expire()
{
    Olsr_mprsel_tuple* tuple = dynamic_cast<Olsr_mprsel_tuple*> (tuple_);
    double time = tuple->time();
    if (time < SIMTIME_DBL(simTime()))
    {
        removeTimer();
        delete this;
    }
    else
    {
        simtime_t next = simTime()+DELAY_T(time);
        this->resched(next);
//      agent_->scheduleAt (simTime()+DELAY_T(time),this);
        //agent_->timerQueuePtr->insert(std::pair<simtime_t, OLSR_Timer *>(simTime()+DELAY_T(time), this));
    }
}

Olsr_MprSelTupleTimer::~Olsr_MprSelTupleTimer()
{
    removeTimer();
    if (!tuple_)
        return;
    Olsr_mprsel_tuple* tuple = dynamic_cast<Olsr_mprsel_tuple*> (tuple_);
    tuple->asocTimer = nullptr;
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->state_ptr==nullptr)
        return;
    ag->rm_mprsel_tuple(tuple);
    delete tuple_;
}



///
/// \brief Removes tuple_ if expired. Else the timer is rescheduled to expire at tuple_->time().
///
/// The task of actually removing the tuple is left to the OLSR agent.
///
/// \param e The event which has expired.
///

void
Olsr_TopologyTupleTimer::expire()
{
    Olsr_topology_tuple* tuple = dynamic_cast<Olsr_topology_tuple*> (tuple_);
    double time = tuple->time();
    if (time < SIMTIME_DBL(simTime()))
    {
        removeTimer();
        delete this;
    }
    else
    {
        simtime_t next = simTime()+DELAY_T(time);
        this->resched(next);
//      agent_->scheduleAt (simTime()+DELAY_T(time),this);
        //agent_->timerQueuePtr->insert(std::pair<simtime_t, OLSR_Timer *>(simTime()+DELAY_T(time), this));
    }
}

Olsr_TopologyTupleTimer::~Olsr_TopologyTupleTimer()
{
    removeTimer();
    if (!tuple_)
        return;
    Olsr_topology_tuple* tuple = dynamic_cast<Olsr_topology_tuple*> (tuple_);
    tuple->asocTimer = nullptr;
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->state_ptr==nullptr)
        return;
    ag->rm_topology_tuple(tuple);
    ag->setTopologyChanged(true);
    delete tuple_;
}


///
/// \brief Removes tuple_ if expired. Else timer is rescheduled to expire at tuple_->time().
/// \warning Actually this is never invoked because there is no support for multiple interfaces.
/// \param e The event which has expired.
///

void Olsr_IfaceAssocTupleTimer::expire()
{
    Olsr_iface_assoc_tuple* tuple = dynamic_cast<Olsr_iface_assoc_tuple*> (tuple_);
    double time = tuple->time();
    if (time < SIMTIME_DBL(simTime()))
    {
        removeTimer();
        delete this;
    }
    else
    {
        simtime_t next = simTime()+DELAY_T(time);
        this->resched(next);
        //  agent_->scheduleAt (simTime()+DELAY_T(time),this);
       // agent_->timerQueuePtr->insert(std::pair<simtime_t, OLSR_Timer *>(simTime()+DELAY_T(time), this));
    }
}

Olsr_IfaceAssocTupleTimer::~Olsr_IfaceAssocTupleTimer()
{
    removeTimer();
    if (!tuple_)
        return;
    Olsr_iface_assoc_tuple* tuple = dynamic_cast<Olsr_iface_assoc_tuple*> (tuple_);
    tuple->asocTimer = nullptr;
    auto ag = dynamic_cast<Olsr*>(agent_);
    if (ag->state_ptr==nullptr)
        return;
    ag->rm_ifaceassoc_tuple(tuple);
    ag->setTopologyChanged(true);
    delete tuple_;
}


///
/// \brief Sends a control packet which must bear every message in the OLSR agent's buffer.
///
/// The task of actually sending the packet is left to the OLSR agent.
///
/// \param e The event which has expired.
///
void
Olsr_MsgTimer::expire()
{
    auto ag = dynamic_cast<Olsr*>(agent_);
    ag->send_pkt();
    removeTimer();
    delete this;
}


/********** OLSR class **********/



///
///
void Olsr::initialize(int stage)
{
    ManetRoutingBase::initialize(stage);

    if (stage == INITSTAGE_ROUTING_PROTOCOLS)
    {

       if (isInMacLayer())
           this->setAddressSize(6);

       OlsrAddressSize::ADDR_SIZE = this->getAddressSize();
	///
	/// \brief Period at which a node must cite every link and every neighbor.
	///
	/// We only use this value in order to define OLSR_NEIGHB_HOLD_TIME.
	///
	    OLSR_REFRESH_INTERVAL=par("OLSR_REFRESH_INTERVAL");

        //
        // Do some initializations
        willingness_ = &par("Willingness");
        hello_ival_ = &par("Hello_ival");
        tc_ival_ = &par("Tc_ival");
        mid_ival_ = &par("Mid_ival");
        use_mac_ = par("use_mac");


        if (par("reduceFuncionality"))
            EV_TRACE << "reduceFuncionality true" << endl;
        else
            EV_TRACE << "reduceFuncionality false" << endl;

        pkt_seq_ = OLSR_MAX_SEQ_NUM;
        msg_seq_ = OLSR_MAX_SEQ_NUM;
        ansn_ = OLSR_MAX_SEQ_NUM;

        registerRoutingModule();

        useIndex = par("UseIndex");

        optimizedMid = par("optimizedMid");

        // Starts all timers

        helloTimer = new Olsr_HelloTimer(); ///< Timer for sending HELLO messages.
        tcTimer = new Olsr_TcTimer();   ///< Timer for sending TC messages.
        midTimer = new Olsr_MidTimer(); ///< Timer for sending MID messages.

        state_ptr = new Olsr_state();


        for (int i = 0; i< getNumWlanInterfaces(); i++)
        {
            // Create never expiring interface association tuple entries for our
            // own network interfaces, so that GetMainAddress () works to
            // translate the node's own interface addresses into the main address.
            Olsr_iface_assoc_tuple* tuple = new Olsr_iface_assoc_tuple;
            int index = getWlanInterfaceIndex(i);
            tuple->iface_addr() = getIfaceAddressFromIndex(index);
            tuple->main_addr() = ra_addr();
            tuple->time() = simtime_t::getMaxTime().dbl();
            tuple->local_iface_index() = index;
            add_ifaceassoc_tuple(tuple);
        }


/*        hello_timer_.resched(hello_ival());
        tc_timer_.resched(hello_ival());
        mid_timer_.resched(hello_ival());*/
        if (use_mac())
        {
            linkLayerFeeback();
        }
        scheduleEvent();

        globalRtable[ra_addr()] = &rtable_;
        computed = false;

        WATCH_PTRMAP(rtable_.rt_);

    }
}


///
/// \brief  This function is called whenever a event  is received. It identifies
///     the type of the received event and process it accordingly.
///
/// If it is an %OLSR packet then it is processed. In other case, if it is a data packet
/// then it is forwarded.
///
/// \param  p the received packet.
/// \param  h a handler (not used).
///

void Olsr::handleMessageWhenUp(cMessage *msg)
{
    if (!isNodeOperational())
    {
        delete msg;
        return;
    }
    if (msg->isSelfMessage())
    {
        checkTimer(msg);
    }
    else
        socket.processMessage(msg);

    scheduleEvent();
}


void Olsr::socketDataArrived(UdpSocket *socket, Packet *packet)
{
    // process incoming packet
    recv_olsr(packet);
}

void Olsr::socketErrorArrived(UdpSocket *socket, Indication *indication)
{
    EV_WARN << "Ignoring UDP error report " << indication->getName() << endl;
    delete indication;
}
///
/// \brief Check if packet is OLSR
/// \param p received packet.
///

bool
Olsr::check_packet(Packet* msg, nsaddr_t &src_addr, int &index)
{
    index = getWlanInterfaceIndex(0);
    if (isInMacLayer()) {
/*        op = dynamic_cast<OLSR_pkt  *>(msg);
        if (!op) // Check if olsr packet
        {
            delete  msg;
            return nullptr;
        }
        else
        {
            if (op->reduceFuncionality() && par("reduceFuncionality").boolValue())
            {
                delete msg;
                return nullptr;
            }
            Ieee802Ctrl* ctrl = check_and_cast<Ieee802Ctrl*>(msg->removeControlInfo());
            src_addr = L3Address(ctrl->getSrc());
            delete ctrl;
            return dynamic_cast<OLSR_pkt  *>(msg);
        }*/
        throw cRuntimeError("Not allowed yet");
    }

    if (msg->getDataLength() == b(0)) {
          return false;
    }

    const auto &op = msg->peekAtFront<OlsrPkt>();

    if (op == nullptr) {
        return false;
    }

    if (op->reduceFuncionality() && par("reduceFuncionality"))  {
        return false;
    }

    auto l3AddressInd = msg->getTag<L3AddressInd>();
    src_addr = l3AddressInd->getSrcAddress();

    auto interfaceId = msg->getTag<InterfaceInd>()->getInterfaceId();
    index = -1;
    NetworkInterface * ie;

    for (int i=0; i<getNumWlanInterfaces(); i++) {
        ie = getWlanInterfaceEntry(i);
        if (ie->getInterfaceId() == interfaceId) {
            index = getWlanInterfaceIndex(i);
            break;
        }
    }
    return true;
}
///
/// \brief Processes an incoming %OLSR packet following RFC 3626 specification.
/// \param p received packet.
///
void
Olsr::recv_olsr(Packet* msg)
{

    nsaddr_t src_addr;
    int index;

    // All routing messages are sent from and to port RT_PORT,
    // so we check it.

    bool proc = check_packet(msg, src_addr, index);
    if (!proc) {
        delete msg;
        return;
    }
    emit(packetReceivedSignal, msg);
    auto &op = msg->popAtFront<OlsrPkt>();

    // If the packet contains no messages must be silently discarded.
    // There could exist a message with an empty body, so the size of
    // the packet would be pkt-hdr-size + msg-hdr-size.

    if (op->getChunkLength() < B(OLSR_PKT_HDR_SIZE + OLSR_MSG_HDR_SIZE))
    {
        delete msg;
        return;
    }

// Process Olsr information
    assert(op->msgArraySize() >= 0 && op->msgArraySize() <= OLSR_MAX_MSGS);
    nsaddr_t receiverIfaceAddr = getIfaceAddressFromIndex(index);
    for (int i = 0; i < (int) op->msgArraySize(); i++)
    {
         auto msgAux = op->msg(i);
         OlsrMsg msg = msgAux;

        // If ttl is less than or equal to zero, or
        // the receiver is the same as the originator,
        // the message must be silently dropped
        // if (msg.ttl() <= 0 || msg.orig_addr() == ra_addr())
        if (msg.ttl() <= 0 || isLocalAddress(msg.orig_addr()))
            continue;

        // If the message has been processed it must not be
        // processed again
        bool do_forwarding = true;

        Olsr_dup_tuple* duplicated = state_.find_dup_tuple(msg.orig_addr(), msg.msg_seq_num());
        if (duplicated == nullptr)
        {
            // Process the message according to its type
            if (msg.msg_type() == OLSR_HELLO_MSG)
                process_hello(msg, receiverIfaceAddr, src_addr, index);
            else if (msg.msg_type() == OLSR_TC_MSG)
                process_tc(msg, src_addr, index);
            else if (msg.msg_type() == OLSR_MID_MSG)
                process_mid(msg, src_addr, index);
            else
            {
                debug("%f: Node %s can not process OLSR packet because does not "
                      "implement OLSR type (%x)\n",
                      CURRENT_TIME,
                      getNodeId(ra_addr()),
                      msg.msg_type());
            }
        }
        else
        {
            // If the message has been considered for forwarding, it should
            // not be retransmitted again
            for (auto it = duplicated->iface_list().begin();
                    it != duplicated->iface_list().end();
                    it++)
            {
                if (*it == receiverIfaceAddr)
                {
                    do_forwarding = false;
                    break;
                }
            }
        }

        if (do_forwarding)
        {
            // HELLO messages are never forwarded.
            // TC and MID messages are forwarded using the default algorithm.
            // Remaining messages are also forwarded using the default algorithm.
            if (msg.msg_type() != OLSR_HELLO_MSG)
                forward_default(msg, duplicated, receiverIfaceAddr, src_addr);
        }

    }
    delete msg;

    // After processing all OLSR messages, we must recompute routing table
    rtable_computation();
}


void Olsr::CoverTwoHopNeighbors(const nsaddr_t &neighborMainAddr, nb2hopset_t & N2)
{ // first gather all 2-hop neighbors to be removed
    std::set<nsaddr_t> toRemove;
    for (auto it = N2.begin(); it != N2.end(); it++)
    {
        Olsr_nb2hop_tuple* twoHopNeigh = *it;
        if (twoHopNeigh->nb_main_addr() == neighborMainAddr)
        {
            toRemove.insert(twoHopNeigh->nb2hop_addr());
        }
    }
    // Now remove all matching records from N2
    for (auto it = N2.begin(); it != N2.end();)
    {
        Olsr_nb2hop_tuple* twoHopNeigh = *it;
        if (toRemove.find(twoHopNeigh->nb2hop_addr()) != toRemove.end())
            it = N2.erase(it);
        else
            it++;
    }
}

///
/// \brief Computates MPR set of a node following RFC 3626 hints.
///
#if 0
void
Olsr::mpr_computation()
{
    // MPR computation should be done for each interface. See section 8.3.1
    // (RFC 3626) for details.
    bool increment;

    state_.clear_mprset();

    nbset_t N; nb2hopset_t N2;
    // N is the subset of neighbors of the node, which are
    // neighbor "of the interface I"
    for (auto it = nbset().begin(); it != nbset().end(); it++)
        if ((*it)->getStatus() == OLSR_STATUS_SYM) // I think that we need this check
            N.push_back(*it);

    // N2 is the set of 2-hop neighbors reachable from "the interface
    // I", excluding:
    // (i)   the nodes only reachable by members of N with willingness WILL_NEVER
    // (ii)  the node performing the computation
    // (iii) all the symmetric neighbors: the nodes for which there exists a symmetric
    //       link to this node on some interface.
    for (auto it = nb2hopset().begin(); it != nb2hopset().end(); it++)
    {
        Olsr_nb2hop_tuple* nb2hop_tuple = *it;
        bool ok = true;

        Olsr_nb_tuple* nb_tuple = state_.find_sym_nb_tuple(nb2hop_tuple->nb_main_addr());
        if (nb_tuple == nullptr)
            ok = false;
        else
        {
            nb_tuple = state_.find_nb_tuple(nb2hop_tuple->nb_main_addr(), OLSR_WILL_NEVER);
            if (nb_tuple != nullptr)
                ok = false;
            else
            {
                nb_tuple = state_.find_sym_nb_tuple(nb2hop_tuple->nb2hop_addr());
                if (nb_tuple != nullptr)
                    ok = false;
            }
        }

        if (ok)
            N2.push_back(nb2hop_tuple);
    }

    // 1. Start with an MPR set made of all members of N with
    // N_willingness equal to WILL_ALWAYS
    for (auto it = N.begin(); it != N.end(); it++)
    {
        Olsr_nb_tuple* nb_tuple = *it;
        if (nb_tuple->willingness() == OLSR_WILL_ALWAYS)
            state_.insert_mpr_addr(nb_tuple->nb_main_addr());
    }

    // 2. Calculate D(y), where y is a member of N, for all nodes in N.
    // We will do this later.

    // 3. Add to the MPR set those nodes in N, which are the *only*
    // nodes to provide reachability to a node in N2. Remove the
    // nodes from N2 which are now covered by a node in the MPR set.
    mprset_t foundset;
    std::set<nsaddr_t> deleted_addrs;
    for (auto it = N2.begin(); it != N2.end();)
    {
        increment = true;
        Olsr_nb2hop_tuple* nb2hop_tuple1 = *it;

        auto pos = foundset.find(nb2hop_tuple1->nb2hop_addr());
        if (pos != foundset.end())
        {
            it++;
            continue;
        }

        bool found = false;
        for (auto it2 = N.begin(); it2 != N.end(); it2++)
        {
            if ((*it2)->nb_main_addr() == nb2hop_tuple1->nb_main_addr())
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            it++;
            continue;
        }

        found = false;

        for (auto it2 = it + 1; it2 != N2.end(); it2++)
        {
            Olsr_nb2hop_tuple* nb2hop_tuple2 = *it2;
            if (nb2hop_tuple1->nb2hop_addr() == nb2hop_tuple2->nb2hop_addr())
            {
                foundset.insert(nb2hop_tuple1->nb2hop_addr());
                found = true;
                break;
            }
        }

        if (!found)
        {
            state_.insert_mpr_addr(nb2hop_tuple1->nb_main_addr());

            for (auto it2 = it + 1; it2 != N2.end();)
            {
                Olsr_nb2hop_tuple* nb2hop_tuple2 = *it2;
                if (nb2hop_tuple1->nb_main_addr() == nb2hop_tuple2->nb_main_addr())
                {
                    deleted_addrs.insert(nb2hop_tuple2->nb2hop_addr());
                    it2 = N2.erase(it2);
                }
                else
                    it2++;

            }

            int distanceFromEnd = std::distance(it, N2.end());
            int distance = std::distance(N2.begin(), it);
            int i = 0;
            for (auto it2 = N2.begin(); i < distance; i++) // check now the first section
            {

                Olsr_nb2hop_tuple* nb2hop_tuple2 = *it2;
                if (nb2hop_tuple1->nb_main_addr() == nb2hop_tuple2->nb_main_addr())
                {
                    deleted_addrs.insert(nb2hop_tuple2->nb2hop_addr());
                    it2 = N2.erase(it2);
                }
                else
                    it2++;

            }
            it = N2.end() - distanceFromEnd; // the standard doesn't guarantee that the iterator is valid if we have delete something in the vector, reload the iterator.

            it = N2.erase(it);
            increment = false;
        }

        for (auto it2 = deleted_addrs.begin();
                it2 != deleted_addrs.end();
                it2++)
        {
            for (auto it3 = N2.begin();
                    it3 != N2.end();)
            {
                if ((*it3)->nb2hop_addr() == *it2)
                {
                    it3 = N2.erase(it3);
                    // I have to reset the external iterator because it
                    // may have been invalidated by the latter deletion
                    it = N2.begin();
                    increment = false;
                }
                else
                    it3++;
            }
        }
        deleted_addrs.clear();
        if (increment)
            it++;
    }

    // 4. While there exist nodes in N2 which are not covered by at
    // least one node in the MPR set:
    while (N2.begin() != N2.end())
    {
        // 4.1. For each node in N, calculate the reachability, i.e., the
        // number of nodes in N2 which are not yet covered by at
        // least one node in the MPR set, and which are reachable
        // through this 1-hop neighbor
        std::map<int, std::vector<Olsr_nb_tuple*> > reachability;
        std::set<int> rs;
        for (auto it = N.begin(); it != N.end(); it++)
        {
            Olsr_nb_tuple* nb_tuple = *it;
            int r = 0;
            for (auto it2 = N2.begin(); it2 != N2.end(); it2++)
            {
                Olsr_nb2hop_tuple* nb2hop_tuple = *it2;
                if (nb_tuple->nb_main_addr() == nb2hop_tuple->nb_main_addr())
                    r++;
            }
            rs.insert(r);
            reachability[r].push_back(nb_tuple);
        }

        // 4.2. Select as a MPR the node with highest N_willingness among
        // the nodes in N with non-zero reachability. In case of
        // multiple choice select the node which provides
        // reachability to the maximum number of nodes in N2. In
        // case of multiple nodes providing the same amount of
        // reachability, select the node as MPR whose D(y) is
        // greater. Remove the nodes from N2 which are now covered
        // by a node in the MPR set.
        Olsr_nb_tuple* max = nullptr;
        int max_r = 0;
        for (auto it = rs.begin(); it != rs.end(); it++)
        {
            int r = *it;
            if (r > 0)
            {
                for (auto it2 = reachability[r].begin();
                        it2 != reachability[r].end();
                        it2++)
                {
                    Olsr_nb_tuple* nb_tuple = *it2;
                    if (max == nullptr || nb_tuple->willingness() > max->willingness())
                    {
                        max = nb_tuple;
                        max_r = r;
                    }
                    else if (nb_tuple->willingness() == max->willingness())
                    {
                        if (r > max_r)
                        {
                            max = nb_tuple;
                            max_r = r;
                        }
                        else if (r == max_r)
                        {
                            if (degree(nb_tuple) > degree(max))
                            {
                                max = nb_tuple;
                                max_r = r;
                            }
                        }
                    }
                }
            }
        }
        if (max != nullptr)
        {
            state_.insert_mpr_addr(max->nb_main_addr());
            std::set<nsaddr_t> nb2hop_addrs;
            for (auto it = N2.begin(); it != N2.end(); )
            {
                Olsr_nb2hop_tuple* nb2hop_tuple = *it;
                if (nb2hop_tuple->nb_main_addr() == max->nb_main_addr())
                {
                    nb2hop_addrs.insert(nb2hop_tuple->nb2hop_addr());
                    it = N2.erase(it);
                }
                else
                    it++;

            }
            for (auto it = N2.begin(); it != N2.end();)
            {
                Olsr_nb2hop_tuple* nb2hop_tuple = *it;
                auto it2 =
                    nb2hop_addrs.find(nb2hop_tuple->nb2hop_addr());
                if (it2 != nb2hop_addrs.end())
                {
                    it = N2.erase(it);
                }
                else
                    it++;

            }
        }
    }
}


#else
void
Olsr::mpr_computation()
{
    // MPR computation should be done for each interface. See section 8.3.1
    // (RFC 3626) for details.
    state_.clear_mprset();

    nbset_t N; nb2hopset_t N2;
    // N is the subset of neighbors of the node, which are
    // neighbor "of the interface I"
    for (auto it = nbset().begin(); it != nbset().end(); it++)
        if ((*it)->getStatus() == OLSR_STATUS_SYM) // I think that we need this check
            N.push_back(*it);

    // N2 is the set of 2-hop neighbors reachable from "the interface
    // I", excluding:
    // (i)   the nodes only reachable by members of N with willingness WILL_NEVER
    // (ii)  the node performing the computation
    // (iii) all the symmetric neighbors: the nodes for which there exists a symmetric
    //       link to this node on some interface.
    for (auto it = nb2hopset().begin(); it != nb2hopset().end(); it++)
    {
        Olsr_nb2hop_tuple* nb2hop_tuple = *it;
        // (ii)  the node performing the computation
        if (isLocalAddress(nb2hop_tuple->nb2hop_addr()))
        {
            continue;
        }
        // excluding:
        // (i) the nodes only reachable by members of N with willingness WILL_NEVER
        bool ok = false;
        for (nbset_t::const_iterator it2 = N.begin(); it2 != N.end(); it2++)
        {
            Olsr_nb_tuple* neigh = *it2;
            if (neigh->nb_main_addr() == nb2hop_tuple->nb_main_addr())
            {
                if (neigh->willingness() == OLSR_WILL_NEVER)
                {
                    ok = false;
                    break;
                }
                else
                {
                    ok = true;
                    break;
                }
            }
        }
        if (!ok)
        {
            continue;
        }

        // excluding:
        // (iii) all the symmetric neighbors: the nodes for which there exists a symmetric
        //       link to this node on some interface.
        for (auto it2 = N.begin(); it2 != N.end(); it2++)
        {
            Olsr_nb_tuple* neigh = *it2;
            if (neigh->nb_main_addr() == nb2hop_tuple->nb2hop_addr())
            {
                ok = false;
                break;
            }
        }
        if (ok)
            N2.push_back(nb2hop_tuple);
    }

    // 1. Start with an MPR set made of all members of N with
    // N_willingness equal to WILL_ALWAYS
    for (auto it = N.begin(); it != N.end(); it++)
    {
        Olsr_nb_tuple* nb_tuple = *it;
        if (nb_tuple->willingness() == OLSR_WILL_ALWAYS)
        {
            state_.insert_mpr_addr(nb_tuple->nb_main_addr());
            // (not in RFC but I think is needed: remove the 2-hop
            // neighbors reachable by the MPR from N2)
            CoverTwoHopNeighbors (nb_tuple->nb_main_addr(), N2);
        }
    }

    // 2. Calculate D(y), where y is a member of N, for all nodes in N.
    // We will do this later.

    // 3. Add to the MPR set those nodes in N, which are the *only*
    // nodes to provide reachability to a node in N2. Remove the
    // nodes from N2 which are now covered by a node in the MPR set.

    std::set<nsaddr_t> coveredTwoHopNeighbors;
    for (auto it = N2.begin(); it != N2.end(); it++)
    {
        Olsr_nb2hop_tuple* twoHopNeigh = *it;
        bool onlyOne = true;
        // try to find another neighbor that can reach twoHopNeigh->twoHopNeighborAddr
        for (nb2hopset_t::const_iterator it2 = N2.begin(); it2 != N2.end(); it2++)
        {
            Olsr_nb2hop_tuple* otherTwoHopNeigh = *it2;
            if (otherTwoHopNeigh->nb2hop_addr() == twoHopNeigh->nb2hop_addr()
                    && otherTwoHopNeigh->nb_main_addr() != twoHopNeigh->nb_main_addr())
            {
                onlyOne = false;
                break;
            }
        }
        if (onlyOne)
        {
            state_.insert_mpr_addr(twoHopNeigh->nb_main_addr());

            // take note of all the 2-hop neighbors reachable by the newly elected MPR
            for (nb2hopset_t::const_iterator it2 = N2.begin(); it2 != N2.end(); it2++)
            {
                Olsr_nb2hop_tuple* otherTwoHopNeigh = *it2;
                if (otherTwoHopNeigh->nb_main_addr() == twoHopNeigh->nb_main_addr())
                {
                    coveredTwoHopNeighbors.insert(otherTwoHopNeigh->nb2hop_addr());
                }
            }
        }
    }
    // Remove the nodes from N2 which are now covered by a node in the MPR set.
    for (auto it = N2.begin(); it != N2.end();)
    {
        Olsr_nb2hop_tuple* twoHopNeigh = *it;
        if (coveredTwoHopNeighbors.find(twoHopNeigh->nb2hop_addr()) != coveredTwoHopNeighbors.end())
        {
            // This works correctly only because it is known that twoHopNeigh is reachable by exactly one neighbor,
            // so only one record in N2 exists for each of them. This record is erased here.
            it = N2.erase(it);
        }
        else
        {
            it++;
        }
    }
    // 4. While there exist nodes in N2 which are not covered by at
    // least one node in the MPR set:

    while (N2.begin() != N2.end())
    {

        // 4.1. For each node in N, calculate the reachability, i.e., the
        // number of nodes in N2 which are not yet covered by at
        // least one node in the MPR set, and which are reachable
        // through this 1-hop neighbor
        std::map<int, std::vector<Olsr_nb_tuple*> > reachability;
        std::set<int> rs;
        for (auto it = N.begin(); it != N.end(); it++)
        {
            Olsr_nb_tuple* nb_tuple = *it;
            int r = 0;
            for (auto it2 = N2.begin(); it2 != N2.end(); it2++)
            {
                Olsr_nb2hop_tuple* nb2hop_tuple = *it2;

                if (nb_tuple->nb_main_addr() == nb2hop_tuple->nb_main_addr())
                    r++;
            }
            rs.insert(r);
            reachability[r].push_back(nb_tuple);
        }

        // 4.2. Select as a MPR the node with highest N_willingness among
        // the nodes in N with non-zero reachability. In case of
        // multiple choice select the node which provides
        // reachability to the maximum number of nodes in N2. In
        // case of multiple nodes providing the same amount of
        // reachability, select the node as MPR whose D(y) is
        // greater. Remove the nodes from N2 which are now covered
        // by a node in the MPR set.
        Olsr_nb_tuple *max = nullptr;
        int max_r = 0;
        for (auto it = rs.begin(); it != rs.end(); it++)
        {
            int r = *it;
            if (r == 0)
            {
                continue;
            }
            for (auto it2 = reachability[r].begin();
                    it2 != reachability[r].end(); it2++)
            {
                Olsr_nb_tuple *nb_tuple = *it2;
                if (max == nullptr || nb_tuple->willingness() > max->willingness())
                {
                    max = nb_tuple;
                    max_r = r;
                }
                else if (nb_tuple->willingness() == max->willingness())
                {
                    if (r > max_r)
                    {
                        max = nb_tuple;
                        max_r = r;
                    }
                    else if (r == max_r)
                    {
                        if (degree(nb_tuple) > degree(max))
                        {
                            max = nb_tuple;
                            max_r = r;
                        }
                    }
                }
            }
        }
        if (max != nullptr)
        {
            state_.insert_mpr_addr(max->nb_main_addr());
            CoverTwoHopNeighbors(max->nb_main_addr(), N2);
            EV_DETAIL << N2.size () << " 2-hop neighbors left to cover! \n";
        }
    }
}
#endif
///
/// \brief Creates the routing table of the node following RFC 3626 hints.
///
void
Olsr::rtable_computation()
{
    nsaddr_t netmask(Ipv4Address::ALLONES_ADDRESS);
    // 1. All the entries from the routing table are removed.
    //

    if (par("DelOnlyRtEntriesInrtable_").boolValue())
    {
        for (rtable_t::const_iterator itRtTable = rtable_.getInternalTable()->begin();itRtTable != rtable_.getInternalTable()->begin();++itRtTable)
        {
            nsaddr_t addr = itRtTable->first;
            omnet_chg_rte(addr, addr,netmask,1, true, addr);
        }
    }
    else
        omnet_clean_rte(); // clean IP tables

    rtable_.clear();


    // 2. The new routing entries are added starting with the
    // symmetric neighbors (h=1) as the destination nodes.

    for (auto it = nbset().begin(); it != nbset().end(); it++)
    {
        Olsr_nb_tuple* nb_tuple = *it;
        if (nb_tuple->getStatus() == OLSR_STATUS_SYM)
        {
            bool nb_main_addr = false;
            Olsr_link_tuple* lt = nullptr;
            for (auto it2 = linkset().begin(); it2 != linkset().end(); it2++)
            {
                Olsr_link_tuple* link_tuple = *it2;
                if (get_main_addr(link_tuple->nb_iface_addr()) == nb_tuple->nb_main_addr() && link_tuple->time() >= CURRENT_TIME)
                {
                    lt = link_tuple;
                    rtable_.add_entry(link_tuple->nb_iface_addr(),
                                      link_tuple->nb_iface_addr(),
                                      link_tuple->local_iface_addr(),
                                      1, link_tuple->local_iface_index());
                    if (!useIndex)
                        omnet_chg_rte(link_tuple->nb_iface_addr(),
                                       link_tuple->nb_iface_addr(),
                                       netmask,
                                       1, false, link_tuple->local_iface_addr());
                    else
                        omnet_chg_rte(link_tuple->nb_iface_addr(),
                                       link_tuple->nb_iface_addr(),
                                       netmask,
                                       1, false, link_tuple->local_iface_index());

                    if (link_tuple->nb_iface_addr() == nb_tuple->nb_main_addr())
                        nb_main_addr = true;
                }
            }
            if (!nb_main_addr && lt != nullptr)
            {
                rtable_.add_entry(nb_tuple->nb_main_addr(),
                                  lt->nb_iface_addr(),
                                  lt->local_iface_addr(),
                                  1, lt->local_iface_index());

                if (!useIndex)
                    omnet_chg_rte(nb_tuple->nb_main_addr(),
                                   lt->nb_iface_addr(),
                                   netmask,// Default mask
                                   1, false, lt->local_iface_addr());

                else
                    omnet_chg_rte(nb_tuple->nb_main_addr(),
                                   lt->nb_iface_addr(),
                                   netmask,// Default mask
                                   1, false, lt->local_iface_index());
            }
        }
    }

    // N2 is the set of 2-hop neighbors reachable from this node, excluding:
    // (i)   the nodes only reachable by members of N with willingness WILL_NEVER
    // (ii)  the node performing the computation
    // (iii) all the symmetric neighbors: the nodes for which there exists a symmetric
    //       link to this node on some interface.
    for (auto it = nb2hopset().begin(); it != nb2hopset().end(); it++)
    {
        Olsr_nb2hop_tuple* nb2hop_tuple = *it;
        bool ok = true;
        Olsr_nb_tuple* nb_tuple = state_.find_sym_nb_tuple(nb2hop_tuple->nb_main_addr());
        if (nb_tuple == nullptr)
            ok = false;
        else
        {
            nb_tuple = state_.find_nb_tuple(nb2hop_tuple->nb_main_addr(), OLSR_WILL_NEVER);
            if (nb_tuple != nullptr)
                ok = false;
            else
            {
                nb_tuple = state_.find_sym_nb_tuple(nb2hop_tuple->nb2hop_addr());
                if (nb_tuple != nullptr)
                    ok = false;
            }
        }

        // 3. For each node in N2 create a new entry in the routing table
        if (ok)
        {
            Olsr_rt_entry* entry = rtable_.lookup(nb2hop_tuple->nb_main_addr());
            assert(entry != nullptr);
            // check if the entry is already in the routing table and the new is a better alternative
            bool insert = true;
            Olsr_rt_entry* entry2hop = rtable_.lookup(nb2hop_tuple->nb2hop_addr());
            if (entry2hop)
            {
                // check if the node is a better alternative
                Olsr_nb_tuple* nb_tupleNew = state_.find_sym_nb_tuple(nb2hop_tuple->nb_main_addr());
                Olsr_nb_tuple* nb_tupleOld = state_.find_sym_nb_tuple(entry2hop->next_addr());
                if (nb_tupleOld != nullptr && nb_tupleOld->willingness() > nb_tupleNew->willingness())
                    insert = false;
            }
            if (insert)
            {
                rtable_.add_entry(nb2hop_tuple->nb2hop_addr(),
                        entry->next_addr(),
                        entry->iface_addr(),
                        2, entry->local_iface_index());
                if (!useIndex)
                    omnet_chg_rte(nb2hop_tuple->nb2hop_addr(),
                            entry->next_addr(),
                            netmask,
                            2, false, entry->iface_addr());
                else
                    omnet_chg_rte(nb2hop_tuple->nb2hop_addr(),
                            entry->next_addr(),
                            netmask,
                            2, false, entry->local_iface_index());
            }
        }
    }

    for (uint32_t h = 2;; h++)
    {
        bool added = false;

        // 4.1. For each topology entry in the topology table, if its
        // T_dest_addr does not correspond to R_dest_addr of any
        // route entry in the routing table AND its T_last_addr
        // corresponds to R_dest_addr of a route entry whose R_dist
        // is equal to h, then a new route entry MUST be recorded in
        // the routing table (if it does not already exist)
        for (auto it = topologyset().begin();
                it != topologyset().end();
                it++)
        {
            Olsr_topology_tuple* topology_tuple = *it;
            Olsr_rt_entry* entry1 = rtable_.lookup(topology_tuple->dest_addr());
            Olsr_rt_entry* entry2 = rtable_.lookup(topology_tuple->last_addr());
            if (entry1 == nullptr && entry2 != nullptr && entry2->dist() == h)
            {
                rtable_.add_entry(topology_tuple->dest_addr(),
                                  entry2->next_addr(),
                                  entry2->iface_addr(),
                                  h+1, entry2->local_iface_index(), entry2);

                if (!useIndex)
                    omnet_chg_rte(topology_tuple->dest_addr(),
                                   entry2->next_addr(),
                                   netmask,
                                   h+1, false, entry2->iface_addr());

                else
                    omnet_chg_rte(topology_tuple->dest_addr(),
                                   entry2->next_addr(),
                                   netmask,
                                   h+1, false, entry2->local_iface_index());

                added = true;
            }
        }

        // 5. For each entry in the multiple interface association base
        // where there exists a routing entry such that:
        //  R_dest_addr  == I_main_addr  (of the multiple interface association entry)
        // AND there is no routing entry such that:
        //  R_dest_addr  == I_iface_addr
        // then a route entry is created in the routing table
        for (auto it = ifaceassocset().begin();
                it != ifaceassocset().end();
                it++)
        {
            Olsr_iface_assoc_tuple* tuple = *it;
            Olsr_rt_entry* entry1 = rtable_.lookup(tuple->main_addr());
            Olsr_rt_entry* entry2 = rtable_.lookup(tuple->iface_addr());
            if (entry1 != nullptr && entry2 == nullptr)
            {
                rtable_.add_entry(tuple->iface_addr(),
                                  entry1->next_addr(),
                                  entry1->iface_addr(),
                                  entry1->dist(), entry1->local_iface_index(), entry1);

                if (!useIndex)
                    omnet_chg_rte(tuple->iface_addr(),
                                   entry1->next_addr(),
                                   netmask,
                                   entry1->dist(), false, entry1->iface_addr());

                else
                    omnet_chg_rte(tuple->iface_addr(),
                                   entry1->next_addr(),
                                   netmask,
                                   entry1->dist(), false, entry1->local_iface_index());
                added = true;
            }
        }

        if (!added)
            break;
    }
    setTopologyChanged(false);
    computeDistributionPath(ra_addr());
}

///
/// \brief Processes a HELLO message following RFC 3626 specification.
///
/// Link sensing and population of the Neighbor Set, 2-hop Neighbor Set and MPR
/// Selector Set are performed.
///
/// \param msg the %OLSR message which contains the HELLO message.
/// \param receiver_iface the address of the interface where the message was received from.
/// \param sender_iface the address of the interface where the message was sent from.
///
bool
Olsr::process_hello(OlsrMsg& msg, const nsaddr_t &receiver_iface, const nsaddr_t &sender_iface, const int &index)
{
    assert(msg.msg_type() == OLSR_HELLO_MSG);

    link_sensing(msg, receiver_iface, sender_iface, index);
    populate_nbset(msg);
    populate_nb2hopset(msg);
    mpr_computation();
    populate_mprselset(msg);
    return false;
}

///
/// \brief Processes a TC message following RFC 3626 specification.
///
/// The Topology Set is updated (if needed) with the information of
/// the received TC message.
///
/// \param msg the %OLSR message which contains the TC message.
/// \param sender_iface the address of the interface where the message was sent from.
///
bool
Olsr::process_tc(OlsrMsg& msg, const nsaddr_t &sender_iface, const int &index)
{
    assert(msg.msg_type() == OLSR_TC_MSG);
    double now = CURRENT_TIME;
    Olsr_tc& tc = msg.tc();

    // 1. If the sender interface of this message is not in the symmetric
    // 1-hop neighborhood of this node, the message MUST be discarded.
    Olsr_link_tuple* link_tuple = state_.find_sym_link_tuple(sender_iface, now);
    if (link_tuple == nullptr)
        return false;

    // 2. If there exist some tuple in the topology set where:
    //  T_last_addr == originator address AND
    //  T_seq       >  ANSN,
    // then further processing of this TC message MUST NOT be
    // performed.
    Olsr_topology_tuple* topology_tuple =
        state_.find_newer_topology_tuple(msg.orig_addr(), tc.ansn());
    if (topology_tuple != nullptr)
        return false;

    // 3. All tuples in the topology set where:
    //  T_last_addr == originator address AND
    //  T_seq       <  ANSN
    // MUST be removed from the topology set.
    state_.erase_older_topology_tuples(msg.orig_addr(), tc.ansn());

    // 4. For each of the advertised neighbor main address received in
    // the TC message:
    for (int i = 0; i < tc.count; i++)
    {
        assert(i >= 0 && i < OLSR_MAX_ADDRS);
        nsaddr_t addr = tc.nb_main_addr(i);
        // 4.1. If there exist some tuple in the topology set where:
        //  T_dest_addr == advertised neighbor main address, AND
        //  T_last_addr == originator address,
        // then the holding time of that tuple MUST be set to:
        //  T_time      =  current time + validity time.
        Olsr_topology_tuple* topology_tuple =
            state_.find_topology_tuple(addr, msg.orig_addr());
        if (topology_tuple != nullptr)
            topology_tuple->time() = now + Olsr::emf_to_seconds(msg.vtime());
        // 4.2. Otherwise, a new tuple MUST be recorded in the topology
        // set where:
        //  T_dest_addr = advertised neighbor main address,
        //  T_last_addr = originator address,
        //  T_seq       = ANSN,
        //  T_time      = current time + validity time.
        else
        {
            Olsr_topology_tuple* topology_tuple = new Olsr_topology_tuple;
            topology_tuple->dest_addr() = addr;
            topology_tuple->last_addr() = msg.orig_addr();
            topology_tuple->seq() = tc.ansn();
            topology_tuple->time() = now + Olsr::emf_to_seconds(msg.vtime());
            topology_tuple->local_iface_index() = index;
            add_topology_tuple(topology_tuple);
            // Schedules topology tuple deletion
            Olsr_TopologyTupleTimer* topology_timer =
                new Olsr_TopologyTupleTimer(this, topology_tuple);
            topology_timer->resched(DELAY(topology_tuple->time()));
        }
    }
    return false;
}

///
/// \brief Processes a MID message following RFC 3626 specification.
///
/// The Interface Association Set is updated (if needed) with the information
/// of the received MID message.
///
/// \param msg the %OLSR message which contains the MID message.
/// \param sender_iface the address of the interface where the message was sent from.
///
void
Olsr::process_mid(OlsrMsg& msg, const nsaddr_t &sender_iface, const int &index)
{
    assert(msg.msg_type() == OLSR_MID_MSG);
    double now = CURRENT_TIME;
    Olsr_mid& mid = msg.mid();

    // 1. If the sender interface of this message is not in the symmetric
    // 1-hop neighborhood of this node, the message MUST be discarded.
    Olsr_link_tuple* link_tuple = state_.find_sym_link_tuple(sender_iface, now);
    if (link_tuple == nullptr)
        return;

    // 2. For each interface address listed in the MID message
    for (int i = 0; i < mid.count; i++)
    {
        bool updated = false;
        for (auto it = ifaceassocset().begin();
                it != ifaceassocset().end();
                it++)
        {
            Olsr_iface_assoc_tuple* tuple = *it;
            if (tuple->iface_addr() == mid.iface_addr(i)
                    && tuple->main_addr() == msg.orig_addr())
            {
                tuple->time() = now + Olsr::emf_to_seconds(msg.vtime());
                updated = true;
            }
        }
        if (!updated)
        {
            Olsr_iface_assoc_tuple* tuple = new Olsr_iface_assoc_tuple;
            tuple->iface_addr() = msg.mid().iface_addr(i);
            tuple->main_addr() = msg.orig_addr();
            tuple->time() = now + Olsr::emf_to_seconds(msg.vtime());
            tuple->local_iface_index() = index;
            add_ifaceassoc_tuple(tuple);
            // Schedules iface association tuple deletion
            Olsr_IfaceAssocTupleTimer* ifaceassoc_timer =
                new Olsr_IfaceAssocTupleTimer(this, tuple);
            ifaceassoc_timer->resched(DELAY(tuple->time()));
        }
    }
}

///
/// \brief OLSR's default forwarding algorithm.
///
/// See RFC 3626 for details.
///
/// \param p the %OLSR packet which has been received.
/// \param msg the %OLSR message which must be forwarded.
/// \param dup_tuple nullptr if the message has never been considered for forwarding,
/// or a duplicate tuple in other case.
/// \param local_iface the address of the interface where the message was received from.
///
void
Olsr::forward_default(OlsrMsg& msg, Olsr_dup_tuple* dup_tuple, const nsaddr_t &local_iface, const nsaddr_t &src_addr)
{
    double now = CURRENT_TIME;

    // If the sender interface address is not in the symmetric
    // 1-hop neighborhood the message must not be forwarded


    Olsr_link_tuple* link_tuple = state_.find_sym_link_tuple(src_addr, now);
    if (link_tuple == nullptr)
        return;

    // If the message has already been considered for forwarding,
    // it must not be retransmitted again
    if (dup_tuple != nullptr && dup_tuple->retransmitted())
    {
        debug("%f: Node %s does not forward a message received"
              " from %s because it is duplicated\n",
              CURRENT_TIME,
              getNodeId(ra_addr()),
              getNodeId(dup_tuple->getAddr()));
        return;
    }

    // If the sender interface address is an interface address
    // of a MPR selector of this node and ttl is greater than 1,
    // the message must be retransmitted
    bool retransmitted = false;
    if (msg.ttl() > 1)
    {
        Olsr_mprsel_tuple* mprsel_tuple =
            state_.find_mprsel_tuple(get_main_addr(src_addr));
        if (mprsel_tuple != nullptr)
        {
            OlsrMsg& new_msg = msg;
            new_msg.ttl()--;
            new_msg.hop_count()++;
            // We have to introduce a random delay to avoid
            // synchronization with neighbors.
            enque_msg(new_msg, JITTER);
            retransmitted = true;
        }
    }

    // Update duplicate tuple...
    if (dup_tuple != nullptr)
    {
        dup_tuple->time() = now + OLSR_DUP_HOLD_TIME;
        dup_tuple->retransmitted() = retransmitted;
        dup_tuple->iface_list().push_back(local_iface);
    }
    // ...or create a new one
    else
    {
        Olsr_dup_tuple* new_dup = new Olsr_dup_tuple;
        new_dup->getAddr() = msg.orig_addr();
        new_dup->seq_num() = msg.msg_seq_num();
        new_dup->time() = now + OLSR_DUP_HOLD_TIME;
        new_dup->retransmitted() = retransmitted;
        new_dup->iface_list().push_back(local_iface);
        add_dup_tuple(new_dup);
        // Schedules dup tuple deletion
        Olsr_DupTupleTimer* dup_timer =
            new Olsr_DupTupleTimer(this, new_dup);
        dup_timer->resched(DELAY(new_dup->time()));
    }
}

///
/// \brief Forwards a data packet to the appropiate next hop indicated by the routing table.
///
/// \param p the packet which must be forwarded.
///
#if 0
void
Olsr::forward_data(cMessage* p, nsaddr_t addr)
{
    struct hdr_cmn* ch = HDR_CMN(p);
    struct hdr_ip* ih = HDR_IP(p);

    if (ch->direction() == hdr_cmn::UP &&
            ((uint32_t)ih->daddr() == IP_BROADCAST || ih->daddr() == ra_addr()))
    {
        dmux_->recv(p, 0);
        return;
    }
    else
    {
        if ((uint32_t)ih->daddr() != IP_BROADCAST)
        {
            Olsr_rt_entry* entry = rtable_.lookup(ih->daddr());
            if (entry == nullptr)
            {
                debug("%f: Node %d can not forward a packet destined to %d\n",
                      CURRENT_TIME,
                      Olsr::node_id(ra_addr()),
                      Olsr::node_id(ih->daddr()));
                drop(p, DROP_RTR_NO_ROUTE);
                return;
            }
            else
            {
                entry = rtable_.find_send_entry(entry);
                assert(entry != nullptr);
                ch->next_hop() = entry->next_addr();

            }
        }
        sendToIp();
        Scheduler::getInstance().schedule(target_, p, 0.0);
    }
}
#endif
///
/// \brief Enques an %OLSR message which will be sent with a delay of (0, delay].
///
/// This buffering system is used in order to piggyback several %OLSR messages in
/// a same %OLSR packet.
///
/// \param msg the %OLSR message which must be sent.
/// \param delay maximum delay the %OLSR message is going to be buffered.
///
void
Olsr::enque_msg(OlsrMsg& msg, double delay)
{
    assert(delay >= 0);

    msgs_.push_back(msg);
    Olsr_MsgTimer* timer = new Olsr_MsgTimer(this);
    timer->resched(delay);
}

///
/// \brief Creates as many %OLSR packets as needed in order to send all buffered
/// %OLSR messages.
///
/// Maximum number of messages which can be contained in an %OLSR packet is
/// dictated by OLSR_MAX_MSGS constant.
///
void
Olsr::send_pkt()
{
    int num_msgs = msgs_.size();
    if (num_msgs == 0)
        return;

    // Calculates the number of needed packets
    int num_pkts = (num_msgs%OLSR_MAX_MSGS == 0) ? num_msgs/OLSR_MAX_MSGS :
                   (num_msgs/OLSR_MAX_MSGS + 1);

    L3Address destAdd;
    if (!this->isInMacLayer())
        destAdd.set(Ipv4Address::ALLONES_ADDRESS);
    else
        destAdd.set(MacAddress::BROADCAST_ADDRESS);

    for (int i = 0; i < num_pkts; i++)
    {
        const auto& op = makeShared<OlsrPkt>();


        op->setChunkLength(B(OLSR_PKT_HDR_SIZE));
        //op->setByteLength( OLSR_PKT_HDR_SIZE );
        op->setPkt_seq_num(pkt_seq());
        op->setReduceFuncionality(par("reduceFuncionality").boolValue());

        int j = 0;
        for (auto it = msgs_.begin(); it != msgs_.end();)
        {
            if (j == OLSR_MAX_MSGS)
                break;

            op->setMsgArraySize(j+1);
            op->setMsg(j++, *it);
            op->setChunkLength(op->getChunkLength()+ B((*it).size()));

            it = msgs_.erase(it);
        }
        Packet *outPacket = new Packet("OLSR Pkt");
        outPacket->insertAtBack(op);

        /// Get the index used in the general interface table
        if (getNumWlanInterfaces() > 1) {
           for(int i = 0; i < getNumWlanInterfaces() ; i++) {
               auto ie = getWlanInterfaceEntry(i);
               sendToIp(outPacket, par("UdpPort"), destAdd, par("UdpPort"), IP_DEF_TTL, 0.0, L3Address(ie->getProtocolData<Ipv4InterfaceData>()->getIPAddress()));
           }
        }
        else
            sendToIp(outPacket, par("UdpPort"), destAdd, par("UdpPort"), IP_DEF_TTL, 0.0, L3Address());
    }
}

///
/// \brief Creates a new %OLSR HELLO message which is buffered to be sent later on.
///
void
Olsr::send_hello()
{
    OlsrMsg msg;
    double now = CURRENT_TIME;
    msg.msg_type() = OLSR_HELLO_MSG;
    msg.vtime() = Olsr::seconds_to_emf(OLSR_NEIGHB_HOLD_TIME);
    msg.orig_addr() = ra_addr();
    msg.ttl() = 1;
    msg.hop_count() = 0;
    msg.msg_seq_num() = msg_seq();

    msg.hello().reserved() = 0;
    msg.hello().htime() = Olsr::seconds_to_emf(hello_ival());
    msg.hello().willingness() = willingness();
    msg.hello().count = 0;

    std::map<uint8_t, int> linkcodes_count;
    for (auto it = linkset().begin(); it != linkset().end(); it++)
    {
        Olsr_link_tuple* link_tuple = *it;
        if (get_main_addr(link_tuple->local_iface_addr()) == ra_addr() && link_tuple->time() >= now)
        {
            uint8_t link_type, nb_type, link_code;

            // Establishes link type
            if (use_mac() && link_tuple->lost_time() >= now)
                link_type = OLSR_LOST_LINK;
            else if (link_tuple->sym_time() >= now)
                link_type = OLSR_SYM_LINK;
            else if (link_tuple->asym_time() >= now)
                link_type = OLSR_ASYM_LINK;
            else
                link_type = OLSR_LOST_LINK;
            // Establishes neighbor type.
            if (state_.find_mpr_addr(get_main_addr(link_tuple->nb_iface_addr())))
                nb_type = OLSR_MPR_NEIGH;
            else
            {
                bool ok = false;
                for (auto nb_it = nbset().begin();
                        nb_it != nbset().end();
                        nb_it++)
                {
                    Olsr_nb_tuple* nb_tuple = *nb_it;
                    if (nb_tuple->nb_main_addr() == get_main_addr(link_tuple->nb_iface_addr()))
                    {
                        if (nb_tuple->getStatus() == OLSR_STATUS_SYM)
                            nb_type = OLSR_SYM_NEIGH;
                        else if (nb_tuple->getStatus() == OLSR_STATUS_NOT_SYM)
                            nb_type = OLSR_NOT_NEIGH;
                        else
                        {
                            throw cRuntimeError("There is a neighbor tuple with an unknown status!");
                        }
                        ok = true;
                        break;
                    }
                }
                if (!ok)
                {
                    EV_INFO << "I don't know the neighbor " << get_main_addr(link_tuple->nb_iface_addr()) << "!!! \n";
                    continue;
                }
            }

            int count = msg.hello().count;
            link_code = (link_type & 0x03) | ((nb_type << 2) & 0x0f);
            auto pos = linkcodes_count.find(link_code);
            if (pos == linkcodes_count.end())
            {
                linkcodes_count[link_code] = count;
                assert(count >= 0 && count < OLSR_MAX_HELLOS);
                msg.hello().hello_msg(count).count = 0;
                msg.hello().hello_msg(count).link_code() = link_code;
                msg.hello().hello_msg(count).reserved() = 0;
                msg.hello().count++;
            }
            else
                count = (*pos).second;

            int i = msg.hello().hello_msg(count).count;
            assert(count >= 0 && count < OLSR_MAX_HELLOS);
            assert(i >= 0 && i < OLSR_MAX_ADDRS);

            msg.hello().hello_msg(count).nb_iface_addr(i) =
                link_tuple->nb_iface_addr();
            msg.hello().hello_msg(count).count++;
            msg.hello().hello_msg(count).link_msg_size() =
                msg.hello().hello_msg(count).size();
        }
    }

    msg.msg_size() = msg.size();

    enque_msg(msg, JITTER);
}

///
/// \brief Creates a new %OLSR TC message which is buffered to be sent later on.
///
void
Olsr::send_tc()
{
    OlsrMsg msg;
    msg.msg_type() = OLSR_TC_MSG;
    msg.vtime() = Olsr::seconds_to_emf(OLSR_TOP_HOLD_TIME);
    msg.orig_addr() = ra_addr();
    msg.ttl() = 255;
    msg.hop_count() = 0;
    msg.msg_seq_num() = msg_seq();

    msg.tc().ansn() = ansn_;
    msg.tc().reserved() = 0;
    msg.tc().count = 0;

    for (auto it = mprselset().begin(); it != mprselset().end(); it++)
    {
        Olsr_mprsel_tuple* mprsel_tuple = *it;
        int count = msg.tc().count;

        assert(count >= 0 && count < OLSR_MAX_ADDRS);
        msg.tc().nb_main_addr(count) = mprsel_tuple->main_addr();
        msg.tc().count++;
    }

    msg.msg_size() = msg.size();

    enque_msg(msg, JITTER);
}

///
/// \brief Creates a new %OLSR MID message which is buffered to be sent later on.
/// \warning This message is never invoked because there is no support for multiple interfaces.
///
void
Olsr::send_mid()
{
    if (getNumWlanInterfaces() <= 1 && optimizedMid)
        return;
    OlsrMsg msg;
    msg.msg_type() = OLSR_MID_MSG;
    msg.vtime() = Olsr::seconds_to_emf(OLSR_MID_HOLD_TIME);
    msg.orig_addr() = ra_addr();
    msg.ttl() = 255;
    msg.hop_count() = 0;
    msg.msg_seq_num() = msg_seq();

    msg.mid().count = 0;
    for (int i = 0; i< getNumWlanInterfaces(); i++)
    {
        int index = getWlanInterfaceIndex(i);
        nsaddr_t addr = getIfaceAddressFromIndex(index);
        msg.mid().setIface_addr(i,addr);
        msg.mid().count++;
    }
    //foreach iface in this_node do
    //  msg.mid().iface_addr(i) = iface
    //  msg.mid().count++
    //done

    msg.msg_size() = msg.size();

    enque_msg(msg, JITTER);
}

///
/// \brief  Updates Link Set according to a new received HELLO message (following RFC 3626
///     specification). Neighbor Set is also updated if needed.
///
/// \param msg the OLSR message which contains the HELLO message.
/// \param receiver_iface the address of the interface where the message was received from.
/// \param sender_iface the address of the interface where the message was sent from.
///
bool
Olsr::link_sensing(OlsrMsg& msg, const nsaddr_t &receiver_iface, const nsaddr_t &sender_iface, const int &index)
{
    Olsr_hello& hello = msg.hello();
    double now = CURRENT_TIME;
    bool updated = false;
    bool created = false;

    Olsr_link_tuple* link_tuple = state_.find_link_tuple(sender_iface);
    if (link_tuple == nullptr)
    {
        // We have to create a new tuple
        link_tuple = new Olsr_link_tuple;
        link_tuple->nb_iface_addr() = sender_iface;
        link_tuple->local_iface_addr() = receiver_iface;
        link_tuple->local_iface_index() = index;
        link_tuple->sym_time() = now - 1;
        link_tuple->lost_time() = 0.0;
        link_tuple->time() = now + Olsr::emf_to_seconds(msg.vtime());
        add_link_tuple(link_tuple, hello.willingness());
        created = true;
    }
    else
        updated = true;

    link_tuple->asym_time() = now + Olsr::emf_to_seconds(msg.vtime());
    assert(hello.count >= 0 && hello.count <= OLSR_MAX_HELLOS);
    for (int i = 0; i < hello.count; i++)
    {
        Olsr_hello_msg& hello_msg = hello.hello_msg(i);
        int lt = hello_msg.link_code() & 0x03;
        int nt = hello_msg.link_code() >> 2;

        // We must not process invalid advertised links
        if ((lt == OLSR_SYM_LINK && nt == OLSR_NOT_NEIGH) ||
                (nt != OLSR_SYM_NEIGH && nt != OLSR_MPR_NEIGH
                 && nt != OLSR_NOT_NEIGH))
            continue;

        assert(hello_msg.count >= 0 && hello_msg.count <= OLSR_MAX_ADDRS);
        for (int j = 0; j < hello_msg.count; j++)
        {
            if (hello_msg.nb_iface_addr(j) == receiver_iface)
            {
                if (lt == OLSR_LOST_LINK)
                {
                    link_tuple->sym_time() = now - 1;
                    updated = true;
                }
                else if (lt == OLSR_SYM_LINK || lt == OLSR_ASYM_LINK)
                {
                    link_tuple->sym_time() =
                        now + Olsr::emf_to_seconds(msg.vtime());
                    link_tuple->time() =
                        link_tuple->sym_time() + OLSR_NEIGHB_HOLD_TIME;
                    link_tuple->lost_time() = 0.0;
                    updated = true;
                }
                break;
            }
        }

    }
    link_tuple->time() = MAX(link_tuple->time(), link_tuple->asym_time());

    if (updated)
        updated_link_tuple(link_tuple, hello.willingness());

    // Schedules link tuple deletion
    if (created && link_tuple != nullptr)
    {
        Olsr_LinkTupleTimer* link_timer =
            new Olsr_LinkTupleTimer(this, link_tuple);
        link_timer->resched(DELAY(MIN(link_tuple->time(), link_tuple->sym_time())));
    }
    return false;
}

///
/// \brief  Updates the Neighbor Set according to the information contained in a new received
///     HELLO message (following RFC 3626).
///
/// \param msg the %OLSR message which contains the HELLO message.
///
bool
Olsr::populate_nbset(OlsrMsg& msg)
{
    Olsr_hello& hello = msg.hello();

    Olsr_nb_tuple* nb_tuple = state_.find_nb_tuple(msg.orig_addr());
    if (nb_tuple != nullptr)
        nb_tuple->willingness() = hello.willingness();
    return false;
}

///
/// \brief  Updates the 2-hop Neighbor Set according to the information contained in a new
///     received HELLO message (following RFC 3626).
///
/// \param msg the %OLSR message which contains the HELLO message.
///
bool
Olsr::populate_nb2hopset(OlsrMsg& msg)
{
    double now = CURRENT_TIME;
    Olsr_hello& hello = msg.hello();

    for (auto it_lt = linkset().begin(); it_lt != linkset().end(); it_lt++)
    {
        Olsr_link_tuple* link_tuple = *it_lt;
        if (get_main_addr(link_tuple->nb_iface_addr()) == msg.orig_addr())
        {
            if (link_tuple->sym_time() >= now)
            {
                assert(hello.count >= 0 && hello.count <= OLSR_MAX_HELLOS);
                for (int i = 0; i < hello.count; i++)
                {
                    Olsr_hello_msg& hello_msg = hello.hello_msg(i);
                    int nt = hello_msg.link_code() >> 2;
                    assert(hello_msg.count >= 0 &&
                           hello_msg.count <= OLSR_MAX_ADDRS);

                    for (int j = 0; j < hello_msg.count; j++)
                    {
                        nsaddr_t nb2hop_addr = get_main_addr(hello_msg.nb_iface_addr(j));
                        if (nt == OLSR_SYM_NEIGH || nt == OLSR_MPR_NEIGH)
                        {
                            // if the main address of the 2-hop
                            // neighbor address = main address of
                            // the receiving node: silently
                            // discard the 2-hop neighbor address
                            if (nb2hop_addr != ra_addr())
                            {
                                // Otherwise, a 2-hop tuple is created
                                Olsr_nb2hop_tuple* nb2hop_tuple =
                                    state_.find_nb2hop_tuple(msg.orig_addr(), nb2hop_addr);
                                if (nb2hop_tuple == nullptr)
                                {
                                    nb2hop_tuple =
                                        new Olsr_nb2hop_tuple;
                                    nb2hop_tuple->nb_main_addr() =
                                        msg.orig_addr();
                                    nb2hop_tuple->nb2hop_addr() =
                                        nb2hop_addr;
                                    add_nb2hop_tuple(nb2hop_tuple);
                                    nb2hop_tuple->time() =
                                        now + Olsr::emf_to_seconds(msg.vtime());
                                    // Schedules nb2hop tuple
                                    // deletion
                                    Olsr_Nb2hopTupleTimer* nb2hop_timer =
                                        new Olsr_Nb2hopTupleTimer(this, nb2hop_tuple);
                                    nb2hop_timer->resched(DELAY(nb2hop_tuple->time()));
                                }
                                else
                                {
                                    nb2hop_tuple->time() =
                                        now + Olsr::emf_to_seconds(msg.vtime());
                                }

                            }
                        }
                        else if (nt == OLSR_NOT_NEIGH)
                        {
                            // For each 2-hop node listed in the HELLO
                            // message with Neighbor Type equal to
                            // NOT_NEIGH all 2-hop tuples where:
                            // N_neighbor_main_addr == Originator
                            // Address AND N_2hop_addr  == main address
                            // of the 2-hop neighbor are deleted.
                            state_.erase_nb2hop_tuples(msg.orig_addr(),
                                                       nb2hop_addr);
                        }
                    }
                }
            }
        }
    }
    return false;
}

///
/// \brief  Updates the MPR Selector Set according to the information contained in a new
///     received HELLO message (following RFC 3626).
///
/// \param msg the %OLSR message which contains the HELLO message.
///
void
Olsr::populate_mprselset(OlsrMsg& msg)
{
    double now = CURRENT_TIME;
    Olsr_hello& hello = msg.hello();

    assert(hello.count >= 0 && hello.count <= OLSR_MAX_HELLOS);
    for (int i = 0; i < hello.count; i++)
    {
        Olsr_hello_msg& hello_msg = hello.hello_msg(i);
        int nt = hello_msg.link_code() >> 2;
        if (nt == OLSR_MPR_NEIGH)
        {
            assert(hello_msg.count >= 0 && hello_msg.count <= OLSR_MAX_ADDRS);
            for (int j = 0; j < hello_msg.count; j++)
            {
                if (get_main_addr(hello_msg.nb_iface_addr(j)) == ra_addr())
                {
                    // We must create a new entry into the mpr selector set
                    Olsr_mprsel_tuple* mprsel_tuple =
                        state_.find_mprsel_tuple(msg.orig_addr());
                    if (mprsel_tuple == nullptr)
                    {
                        mprsel_tuple = new Olsr_mprsel_tuple;
                        mprsel_tuple->main_addr() = msg.orig_addr();
                        mprsel_tuple->time() =
                            now + Olsr::emf_to_seconds(msg.vtime());
                        add_mprsel_tuple(mprsel_tuple);
                        // Schedules mpr selector tuple deletion
                        Olsr_MprSelTupleTimer* mprsel_timer =
                            new Olsr_MprSelTupleTimer(this, mprsel_tuple);
                        mprsel_timer->resched(DELAY(mprsel_tuple->time()));
                    }
                    else
                        mprsel_tuple->time() =
                            now + Olsr::emf_to_seconds(msg.vtime());
                }
            }
        }
    }
}

///
/// \brief  Drops a given packet because it couldn't be delivered to the corresponding
///     destination by the MAC layer. This may cause a neighbor loss, and appropiate
///     actions are then taken.
///
/// \param p the packet which couldn't be delivered by the MAC layer.
///
void
Olsr::mac_failed(Packet* p)
{
    double now = CURRENT_TIME;


    const auto& networkHeader = getNetworkProtocolHeader(p);
    const L3Address& destAddr = networkHeader->getDestinationAddress();


    EV_WARN <<"Node " << Olsr::node_id(ra_addr()) << "MAC Layer detects a breakage on link to "  << destAddr;


    if (destAddr == L3Address(Ipv4Address(IP_BROADCAST)))
        return;

    deleteIpEntry(destAddr);
    Olsr_rt_entry*  entry = rtable_.lookup(destAddr);
    std::vector<L3Address> delAddress;
    if (entry)
    {
        Olsr_link_tuple* link_tuple = state_.find_link_tuple(entry->next_addr());
        if (link_tuple != nullptr)
        {
            link_tuple->lost_time() = now + OLSR_NEIGHB_HOLD_TIME;
            link_tuple->time() = now + OLSR_NEIGHB_HOLD_TIME;
            nb_loss(link_tuple);
        }
    }
}

///
/// \brief Schedule the timer used for sending HELLO messages.
///
void
Olsr::set_hello_timer()
{
    hello_timer_.resched(hello_ival() - JITTER);
}

///
/// \brief Schedule the timer used for sending TC messages.
///
void
Olsr::set_tc_timer()
{
    tc_timer_.resched(tc_ival() - JITTER);
}

///
/// \brief Schedule the timer used for sending MID messages.
///
void
Olsr::set_mid_timer()
{
    mid_timer_.resched(mid_ival() - JITTER);
}

///
/// \brief Performs all actions needed when a neighbor loss occurs.
///
/// Neighbor Set, 2-hop Neighbor Set, MPR Set and MPR Selector Set are updated.
///
/// \param tuple link tuple with the information of the link to the neighbor which has been lost.
///
void
Olsr::nb_loss(Olsr_link_tuple* tuple)
{
    debug("%f: Node %s detects neighbor %s loss\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->nb_iface_addr()));

    updated_link_tuple(tuple,OLSR_WILL_DEFAULT);
    state_.erase_nb2hop_tuples(get_main_addr(tuple->nb_iface_addr()));
    state_.erase_mprsel_tuples(get_main_addr(tuple->nb_iface_addr()));

    mpr_computation();
    rtable_computation();
}

///
/// \brief Adds a duplicate tuple to the Duplicate Set.
///
/// \param tuple the duplicate tuple to be added.
///
void
Olsr::add_dup_tuple(Olsr_dup_tuple* tuple)
{
    /*debug("%f: Node %d adds dup tuple: addr = %d seq_num = %d\n",
        CURRENT_TIME,
        OLSR::node_id(ra_addr()),
        OLSR::node_id(tuple->getAddr()),
        tuple->seq_num());*/

    state_.insert_dup_tuple(tuple);
}

///
/// \brief Removes a duplicate tuple from the Duplicate Set.
///
/// \param tuple the duplicate tuple to be removed.
///
void
Olsr::rm_dup_tuple(Olsr_dup_tuple* tuple)
{
    /*debug("%f: Node %d removes dup tuple: addr = %d seq_num = %d\n",
        CURRENT_TIME,
        OLSR::node_id(ra_addr()),
        OLSR::node_id(tuple->getAddr()),
        tuple->seq_num());*/

    state_.erase_dup_tuple(tuple);
}

///
/// \brief Adds a link tuple to the Link Set (and an associated neighbor tuple to the Neighbor Set).
///
/// \param tuple the link tuple to be added.
/// \param willingness willingness of the node which is going to be inserted in the Neighbor Set.
///
void
Olsr::add_link_tuple(Olsr_link_tuple* tuple, uint8_t  willingness)
{
    double now = CURRENT_TIME;

    debug("%f: Node %s adds link tuple: nb_addr = %s\n",
          now,
          getNodeId(ra_addr()),
          getNodeId(tuple->nb_iface_addr()));

    state_.insert_link_tuple(tuple);
    // Creates associated neighbor tuple
    Olsr_nb_tuple* nb_tuple = new Olsr_nb_tuple;
    nb_tuple->nb_main_addr() = get_main_addr(tuple->nb_iface_addr());
    nb_tuple->willingness() = willingness;
    if (tuple->sym_time() >= now)
        nb_tuple->getStatus() = OLSR_STATUS_SYM;
    else
        nb_tuple->getStatus() = OLSR_STATUS_NOT_SYM;
    add_nb_tuple(nb_tuple);
}

///
/// \brief Removes a link tuple from the Link Set.
///
/// \param tuple the link tuple to be removed.
///
void
Olsr::rm_link_tuple(Olsr_link_tuple* tuple)
{
    nsaddr_t nb_addr = get_main_addr(tuple->nb_iface_addr());
    double now = CURRENT_TIME;

    debug("%f: Node %s removes link tuple: nb_addr = %s\n",
          now,
          getNodeId(ra_addr()),
          getNodeId(tuple->nb_iface_addr()));
    // Prints this here cause we are not actually calling rm_nb_tuple() (efficiency stuff)
    debug("%f: Node %s removes neighbor tuple: nb_addr = %s\n",
          now,
          getNodeId(ra_addr()),
          getNodeId(nb_addr));

    state_.erase_link_tuple(tuple);

    Olsr_nb_tuple* nb_tuple = state_.find_nb_tuple(nb_addr);
    state_.erase_nb_tuple(nb_tuple);
    delete nb_tuple;
}

///
/// \brief  This function is invoked when a link tuple is updated. Its aim is to
///     also update the corresponding neighbor tuple if it is needed.
///
/// \param tuple the link tuple which has been updated.
///
void
Olsr::updated_link_tuple(Olsr_link_tuple* tuple, uint8_t willingness)
{
    double now = CURRENT_TIME;

    // Each time a link tuple changes, the associated neighbor tuple must be recomputed
    Olsr_nb_tuple* nb_tuple = find_or_add_nb(tuple, willingness);

    if (nb_tuple != nullptr)
    {
        if (use_mac() && tuple->lost_time() >= now)
            nb_tuple->getStatus() = OLSR_STATUS_NOT_SYM;
        else if (tuple->sym_time() >= now)
            nb_tuple->getStatus() = OLSR_STATUS_SYM;
        else
            nb_tuple->getStatus() = OLSR_STATUS_NOT_SYM;

        debug("%f: Node %s has updated link tuple: nb_addr = %s status = %s\n", now, getNodeId(ra_addr()),
                getNodeId(tuple->nb_iface_addr()), ((nb_tuple->getStatus() == OLSR_STATUS_SYM) ? "sym" : "not_sym"));
    }
}
// Auxiliary method
// add NB based in the link tuple information if the Nb doen't exist

Olsr_nb_tuple* Olsr::find_or_add_nb(Olsr_link_tuple* tuple, uint8_t willingness)
{
    Olsr_nb_tuple* nb_tuple = state_.find_nb_tuple(get_main_addr(tuple->nb_iface_addr()));
    if (nb_tuple == nullptr)
    {
        double now = CURRENT_TIME;
        state_.erase_nb_tuple(tuple->nb_iface_addr());
        // Creates associated neighbor tuple
        nb_tuple = new Olsr_nb_tuple;
        nb_tuple->nb_main_addr() = get_main_addr(tuple->nb_iface_addr());
        nb_tuple->willingness() = willingness;
        if (tuple->sym_time() >= now)
            nb_tuple->getStatus() = OLSR_STATUS_SYM;
        else
            nb_tuple->getStatus() = OLSR_STATUS_NOT_SYM;
        add_nb_tuple(nb_tuple);
        nb_tuple = state_.find_nb_tuple(get_main_addr(tuple->nb_iface_addr()));
    }
    return nb_tuple;
}

///
/// \brief Adds a neighbor tuple to the Neighbor Set.
///
/// \param tuple the neighbor tuple to be added.
///
void
Olsr::add_nb_tuple(Olsr_nb_tuple* tuple)
{
    debug("%f: Node %s adds neighbor tuple: nb_addr = %s status = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->nb_main_addr()),
          ((tuple->getStatus() == OLSR_STATUS_SYM) ? "sym" : "not_sym"));
    state_.erase_nb_tuple(tuple->nb_main_addr());
    state_.insert_nb_tuple(tuple);
}

///
/// \brief Removes a neighbor tuple from the Neighbor Set.
///
/// \param tuple the neighbor tuple to be removed.
///
void
Olsr::rm_nb_tuple(Olsr_nb_tuple* tuple)
{
    debug("%f: Node %s removes neighbor tuple: nb_addr = %s status = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->nb_main_addr()),
          ((tuple->getStatus() == OLSR_STATUS_SYM) ? "sym" : "not_sym"));

    state_.erase_nb_tuple(tuple);
}

///
/// \brief Adds a 2-hop neighbor tuple to the 2-hop Neighbor Set.
///
/// \param tuple the 2-hop neighbor tuple to be added.
///
void
Olsr::add_nb2hop_tuple(Olsr_nb2hop_tuple* tuple)
{
    debug("%f: Node %s adds 2-hop neighbor tuple: nb_addr = %s nb2hop_addr = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->nb_main_addr()),
          getNodeId(tuple->nb2hop_addr()));

    state_.insert_nb2hop_tuple(tuple);
}

///
/// \brief Removes a 2-hop neighbor tuple from the 2-hop Neighbor Set.
///
/// \param tuple the 2-hop neighbor tuple to be removed.
///
void
Olsr::rm_nb2hop_tuple(Olsr_nb2hop_tuple* tuple)
{
    debug("%f: Node %s removes 2-hop neighbor tuple: nb_addr = %s nb2hop_addr = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->nb_main_addr()),
          getNodeId(tuple->nb2hop_addr()));

    state_.erase_nb2hop_tuple(tuple);
}

///
/// \brief Adds an MPR selector tuple to the MPR Selector Set.
///
/// Advertised Neighbor Sequence Number (ANSN) is also updated.
///
/// \param tuple the MPR selector tuple to be added.
///
void
Olsr::add_mprsel_tuple(Olsr_mprsel_tuple* tuple)
{
    debug("%f: Node %s adds MPR selector tuple: nb_addr = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->main_addr()));

    state_.insert_mprsel_tuple(tuple);
    ansn_ = (ansn_ + 1)%(OLSR_MAX_SEQ_NUM + 1);
}

///
/// \brief Removes an MPR selector tuple from the MPR Selector Set.
///
/// Advertised Neighbor Sequence Number (ANSN) is also updated.
///
/// \param tuple the MPR selector tuple to be removed.
///
void
Olsr::rm_mprsel_tuple(Olsr_mprsel_tuple* tuple)
{
    debug("%f: Node %s removes MPR selector tuple: nb_addr = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->main_addr()));

    state_.erase_mprsel_tuple(tuple);
    ansn_ = (ansn_ + 1)%(OLSR_MAX_SEQ_NUM + 1);
}

///
/// \brief Adds a topology tuple to the Topology Set.
///
/// \param tuple the topology tuple to be added.
///
void
Olsr::add_topology_tuple(Olsr_topology_tuple* tuple)
{
    debug("%f: Node %s adds topology tuple: dest_addr = %s last_addr = %s seq = %d\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->dest_addr()),
          getNodeId(tuple->last_addr()),
          tuple->seq());

    state_.insert_topology_tuple(tuple);
}

///
/// \brief Removes a topology tuple from the Topology Set.
///
/// \param tuple the topology tuple to be removed.
///
void
Olsr::rm_topology_tuple(Olsr_topology_tuple* tuple)
{
    debug("%f: Node %s removes topology tuple: dest_addr = %s last_addr = %s seq = %d\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->dest_addr()),
          getNodeId(tuple->last_addr()),
          tuple->seq());

    state_.erase_topology_tuple(tuple);
}

///
/// \brief Adds an interface association tuple to the Interface Association Set.
///
/// \param tuple the interface association tuple to be added.
///
void
Olsr::add_ifaceassoc_tuple(Olsr_iface_assoc_tuple* tuple)
{
    debug("%f: Node %s adds iface association tuple: main_addr = %s iface_addr = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->main_addr()),
          getNodeId(tuple->iface_addr()));

    state_.insert_ifaceassoc_tuple(tuple);
}

///
/// \brief Removes an interface association tuple from the Interface Association Set.
///
/// \param tuple the interface association tuple to be removed.
///
void
Olsr::rm_ifaceassoc_tuple(Olsr_iface_assoc_tuple* tuple)
{
    debug("%f: Node %s removes iface association tuple: main_addr = %s iface_addr = %s\n",
          CURRENT_TIME,
          getNodeId(ra_addr()),
          getNodeId(tuple->main_addr()),
          getNodeId(tuple->iface_addr()));

    state_.erase_ifaceassoc_tuple(tuple);
}

///
/// \brief Gets the main address associated with a given interface address.
///
/// \param iface_addr the interface address.
/// \return the corresponding main address.
///
const nsaddr_t &
Olsr::get_main_addr(const nsaddr_t &iface_addr) const
{
    Olsr_iface_assoc_tuple* tuple =
        state_.find_ifaceassoc_tuple(iface_addr);

    if (tuple != nullptr)
        return tuple->main_addr();
    return iface_addr;
}

///
/// \brief Determines which sequence number is bigger (as it is defined in RFC 3626).
///
/// \param s1 a sequence number.
/// \param s2 a sequence number.
/// \return true if s1 > s2, false in other case.
///
bool
Olsr::seq_num_bigger_than(uint16_t s1, uint16_t s2)
{
    return (s1 > s2 && s1-s2 <= OLSR_MAX_SEQ_NUM/2)
           || (s2 > s1 && s2-s1 > OLSR_MAX_SEQ_NUM/2);
}

///
/// \brief This auxiliary function (defined in RFC 3626) is used for calculating the MPR Set.
///
/// \param tuple the neighbor tuple which has the main address of the node we are going to calculate its degree to.
/// \return the degree of the node.
///
int
Olsr::degree(Olsr_nb_tuple* tuple)
{
    int degree = 0;
    for (auto it = nb2hopset().begin(); it != nb2hopset().end(); it++)
    {
        Olsr_nb2hop_tuple* nb2hop_tuple = *it;
        if (nb2hop_tuple->nb_main_addr() == tuple->nb_main_addr())
        {
            //OLSR_nb_tuple* nb_tuple =
            //    state_.find_nb_tuple(nb2hop_tuple->nb_main_addr());
            Olsr_nb_tuple* nb_tuple = state_.find_nb_tuple(nb2hop_tuple->nb2hop_addr());
            if (nb_tuple == nullptr)
                degree++;
        }
    }
    return degree;
}

///
/// \brief Converts a decimal number of seconds to the mantissa/exponent format.
///
/// \param seconds decimal number of seconds we want to convert.
/// \return the number of seconds in mantissa/exponent format.
///
uint8_t
Olsr::seconds_to_emf(double seconds)
{
    // This implementation has been taken from unik-olsrd-0.4.5 (mantissa.c),
    // licensed under the GNU Public License (GPL)

    int a, b = 0;
    while (seconds/OLSR_C >= pow((double)2, (double)b))
        b++;
    b--;

    if (b < 0)
    {
        a = 1;
        b = 0;
    }
    else if (b > 15)
    {
        a = 15;
        b = 15;
    }
    else
    {
        a = (int)(16*((double)seconds/(OLSR_C*(double)pow((double)2, b))-1));
        while (a >= 16)
        {
            a -= 16;
            b++;
        }
    }

    return (uint8_t)(a*16+b);
}

///
/// \brief Converts a number of seconds in the mantissa/exponent format to a decimal number.
///
/// \param olsr_format number of seconds in mantissa/exponent format.
/// \return the decimal number of seconds.
///
double
Olsr::emf_to_seconds(uint8_t olsr_format)
{
    // This implementation has been taken from unik-olsrd-0.4.5 (mantissa.c),
    // licensed under the GNU Public License (GPL)
    int a = olsr_format >> 4;
    int b = olsr_format - a*16;
    return (double)(OLSR_C*(1+(double)a/16)*(double)pow((double)2, b));
}

///
/// \brief Returns the identifier of a node given the address of the attached OLSR agent.
///
/// \param addr the address of the OLSR routing agent.
/// \return the identifier of the node.
///
int
Olsr::node_id(const nsaddr_t &addr)
{
    return addr.toIpv4().getInt();
    /*
        // Preventing a bad use for this function
            if ((uint32_t)addr == IP_BROADCAST)
            return addr;
        // Getting node id
        Node* node = Node::get_node_by_address(addr);
        assert(node != nullptr);
        return node->nodeid();
    */
}

const char * Olsr::getNodeId(const nsaddr_t &addr)
{
    return addr.str().c_str();
}

// Interfaces with other Inet
void Olsr:: processLinkBreak(const Packet *dgram)
{
    if (use_mac())
    {

        const auto& networkHeader = findNetworkProtocolHeader(const_cast<Packet *>(dgram));
        if (networkHeader)
            mac_failed(const_cast<Packet *>(dgram));
    }
}

void Olsr::finish()
{
    /*
    rtable_.clear();
    msgs_.clear();
    delete state_ptr;
    state_ptr=nullptr;
    cancelAndDelete(&hello_timer_);
    cancelAndDelete(&tc_timer_);
    cancelAndDelete(&mid_timer_);

    helloTimer= nullptr;   ///< Timer for sending HELLO messages.
    tcTimer= nullptr;  ///< Timer for sending TC messages.
    midTimer = nullptr;    ///< Timer for sending MID messages.
    */
}

Olsr::~Olsr()
{
    rtable_.clear();
    msgs_.clear();
    if (state_ptr)
    {
        delete state_ptr;
        state_ptr = nullptr;
    }
    /*
        mprset().clear();
        mprselset().clear();
        linkset().clear();
        nbset().clear();
        nb2hopset().clear();
        topologyset().clear();
        dupset().clear();
        ifaceassocset().clear();
    */
    /*
        if (&hello_timer_!=nullptr)
            cancelAndDelete(&hello_timer_);
        if (&tc_timer_!=nullptr)
            cancelAndDelete(&tc_timer_);
        if (&mid_timer_!=nullptr)
            cancelAndDelete(&mid_timer_);
    */
    if (getTimerMultimMap())
    {
        while (getTimerMultimMap()->size()>0)
        {
            Olsr_Timer * timer = check_and_cast<Olsr_Timer *>(getTimerMultimMap()->begin()->second);
            getTimerMultimMap()->erase(getTimerMultimMap()->begin());
            timer->setTuple(nullptr);
            if (helloTimer==timer)
                helloTimer = nullptr;
            else if (tcTimer==timer)
                tcTimer = nullptr;
            else if (midTimer==timer)
                midTimer = nullptr;
            delete timer;

        }
    }

    if (helloTimer)
    {
        delete helloTimer;
        helloTimer = nullptr;
    }
    if (tcTimer)
    {
        delete tcTimer;
        tcTimer = nullptr;
    }
    if (midTimer)
    {
        delete midTimer;
        midTimer = nullptr;
    }
}


uint32_t Olsr::getRoute(const L3Address &dest, std::vector<L3Address> &add)
{
    add.clear();
    Olsr_rt_entry* rt_entry = rtable_.lookup(dest);
    L3Address apAddr;
    if (!rt_entry)
    {
        if (getAp(dest, apAddr))
        {
            Olsr_rt_entry* rt_entry = rtable_.lookup(apAddr);
            if (!rt_entry)
                return 0;
            for (int i = 0; i < (int) rt_entry->route.size(); i++)
                add.push_back(rt_entry->route[i]);
            add.push_back(apAddr);
            Olsr_rt_entry* rt_entry_aux = rtable_.find_send_entry(rt_entry);
            if (rt_entry_aux->next_addr() != add[0])
                throw cRuntimeError("OLSR Data base error");
            return rt_entry->dist();
        }
        return 0;
    }

    for (int i=0; i<(int)rt_entry->route.size(); i++)
        add.push_back(rt_entry->route[i]);
    add.push_back(dest);
    Olsr_rt_entry* rt_entry_aux = rtable_.find_send_entry(rt_entry);
    if (rt_entry_aux->next_addr() != add[0])
        throw cRuntimeError("OLSR Data base error");
    return rt_entry->dist();
}


bool Olsr::getNextHop(const L3Address &dest, L3Address &add, int &iface, double &cost)
{
    Olsr_rt_entry* rt_entry = rtable_.lookup(dest);
    if (!rt_entry)
    {
        L3Address apAddr;
        if (getAp(dest, apAddr))
        {

            Olsr_rt_entry* rt_entry = rtable_.lookup(apAddr);
            if (!rt_entry)
                return false;
            if (rt_entry->route.size())
                add = rt_entry->route[0];
            else
                add = rt_entry->next_addr();
            Olsr_rt_entry* rt_entry_aux = rtable_.find_send_entry(rt_entry);
            if (rt_entry_aux->next_addr() != add)
                throw cRuntimeError("OLSR Data base error");

            NetworkInterface * ie = getInterfaceWlanByAddress(rt_entry->iface_addr());
            iface = ie->getInterfaceId();
            cost = rt_entry->dist();
            return true;
        }
        return false;
    }

    if (rt_entry->route.size())
        add = rt_entry->route[0];
    else
        add = rt_entry->next_addr();
    Olsr_rt_entry* rt_entry_aux = rtable_.find_send_entry(rt_entry);
    if (rt_entry_aux->next_addr() != add)
        throw cRuntimeError("OLSR Data base error");

    NetworkInterface * ie = getInterfaceWlanByAddress(rt_entry->iface_addr());
    iface = ie->getInterfaceId();
    cost = rt_entry->dist();
    return true;
}

bool Olsr::isProactive()
{
    return true;
}


bool Olsr::isOurType(const Packet * msg)
{
    const auto &chunk = msg->peekAtFront<Chunk>();
    if (dynamicPtrCast<const OlsrPkt>(chunk))
        return true;
    return false;
}

bool Olsr::getDestAddress(Packet *msg, L3Address &dest)
{
    return false;
}


// Group methods, allow the anycast procedure
int Olsr::getRouteGroup(const AddressGroup &gr, std::vector<L3Address> &add)
{

    int distance = 1000;
    add.clear();
    for (AddressGroupConstIterator it = gr.begin(); it!=gr.end(); it++)
    {
        L3Address dest = *it;
        Olsr_rt_entry* rt_entry = rtable_.lookup(dest);
        if (!rt_entry)
            continue;
        if (distance<(int)rt_entry->dist() || (distance==(int)rt_entry->dist() && intrand(1)))
            continue;
        distance = rt_entry->dist();
        add.clear();
        for (int i=0; i<(int)rt_entry->route.size(); i++)
            add.push_back(rt_entry->route[i]);
        add.push_back(dest);

        add[rt_entry->route.size()] = dest;
        Olsr_rt_entry* rt_entry_aux = rtable_.find_send_entry(rt_entry);
        if (rt_entry_aux->next_addr() != add[0])
            throw cRuntimeError("OLSR Data base error");
    }
    if (distance==1000)
        return 0;
    return distance;
}

bool Olsr::getNextHopGroup(const AddressGroup &gr, L3Address &add, int &iface, L3Address &gw)
{

    int distance = 1000;
    for (AddressGroupConstIterator it = gr.begin(); it!=gr.end(); it++)
    {
        L3Address dest = *it;
        Olsr_rt_entry* rt_entry = rtable_.lookup(dest);
        if (!rt_entry)
            continue;
        if (distance<(int)rt_entry->dist() || (distance==(int)rt_entry->dist() && intrand(1)))
            continue;
        distance = rt_entry->dist();
        if (rt_entry->route.size())
            add = rt_entry->route[0];
        else
            add = rt_entry->next_addr();
        Olsr_rt_entry* rt_entry_aux = rtable_.find_send_entry(rt_entry);
        if (rt_entry_aux->next_addr() != add)
            throw cRuntimeError("OLSR Data base error");
        NetworkInterface * ie = getInterfaceWlanByAddress(rt_entry->iface_addr());
        iface = ie->getInterfaceId();
        gw = dest;
    }
    if (distance==1000)
        return false;
    return true;
}





int  Olsr::getRouteGroup(const L3Address& dest, std::vector<L3Address> &add, L3Address& gateway, bool &isGroup, int group)
{
    AddressGroup gr;
    int distance = 0;
    if (findInAddressGroup(dest, group))
    {
        getAddressGroup(gr, group);
        distance = getRouteGroup(gr, add);
        if (distance == 0)
            return 0;
        gateway = add.back();
        isGroup = true;

     }
    else
    {
        distance = getRoute(dest, add);
        isGroup = false;
    }
    return distance;
}

bool Olsr::getNextHopGroup(const L3Address& dest, L3Address &next, int &iface, L3Address& gw, bool &isGroup, int group)
{
    AddressGroup gr;
    bool find = false;
    if (findInAddressGroup(dest, group))
    {
        getAddressGroup(gr, group);
        find = getNextHopGroup(gr, next, iface, gw);
        isGroup = true;
     }
    else
    {
        double cost;
        find = getNextHop(dest, next, iface, cost);
        isGroup = false;
    }
    return find;
}



L3Address Olsr::getIfaceAddressFromIndex(int index)
{
    NetworkInterface * entry = getInterfaceEntry(index);
    if (this->isInMacLayer())
        return L3Address(entry->getMacAddress());
    else
        return L3Address(entry->getProtocolData<Ipv4InterfaceData>()->getIPAddress());
}

void Olsr::computeDistributionPath(const nsaddr_t &initNode)
{
    std::vector<nsaddr_t> route;
    mprset_t mpr = state_.mprset();
    nsaddr_t actualNode= initNode;
    while(!mpr.empty())
    {
        // search the minimum
        auto it = globalRtable.find(actualNode);
        if (it==globalRtable.end())
            return;
        Olsr_rtable *val = it->second;
        auto itMin = mpr.end();
        int hops = 1000;
        Olsr_rt_entry* segmentRoute = nullptr;
        for (auto it2 = mpr.begin();it2 != mpr.end();++it2)
        {

            if (*it2 == actualNode)
            {
                continue;
            }
            else
            {
                Olsr_rt_entry*  entry = val->lookup(*it2);
                if (entry == nullptr)
                    return;
                if (hops > (int)entry->dist())
                {
                    hops = entry->dist();
                    itMin = it2;
                    segmentRoute = entry;
                }
            }
        }
        //
        actualNode = *itMin;
        mpr.erase(itMin);
        if (segmentRoute)
        {
            if (segmentRoute->dist() > 1 && segmentRoute->route.empty())
                throw cRuntimeError("error in entry route");
            for (unsigned int i = 0; i< segmentRoute->route.size(); i++)
            {
                route.push_back(segmentRoute->route[i]);
            }
        }
        route.push_back(actualNode);
    }
    distributionPath[initNode] = route;
}

void Olsr::getDistributionPath(const L3Address &addr,std::vector<L3Address> &path)
{

    path.clear();
    auto it = distributionPath.find(addr);
    if (it != distributionPath.end())
    {
        if (it->second.empty())
            throw cRuntimeError("error in distribution route");
        path = it->second;
    }
}



bool
Olsr::isNodeCandidate(const nsaddr_t &src_addr)
{
    double now = CURRENT_TIME;

    // If the sender interface address is not in the symmetric
    // 1-hop neighborhood the message must not be forwarded


    Olsr_link_tuple* link_tuple = state_.find_sym_link_tuple(src_addr, now);
    if (link_tuple == nullptr)
        return false;


    Olsr_mprsel_tuple* mprsel_tuple = state_.find_mprsel_tuple(get_main_addr(src_addr));
    if (mprsel_tuple != nullptr)
        return true;
    return false;
}

void Olsr::handleStartOperation(LifecycleOperation *operation)
{
    hello_timer_.resched(hello_ival());
    tc_timer_.resched(hello_ival());
    mid_timer_.resched(hello_ival());
    scheduleEvent();}

void Olsr::handleStopOperation(LifecycleOperation *operation)
{

    rtable_.clear();
    msgs_.clear();
    while (getTimerMultimMap()->size()>0)
    {
        Olsr_Timer * timer = check_and_cast<Olsr_Timer *>(getTimerMultimMap()->begin()->second);
        getTimerMultimMap()->erase(getTimerMultimMap()->begin());
        timer->setTuple(nullptr);
        if (helloTimer==timer)
            continue;
        else if (tcTimer==timer)
            continue;
        else if (midTimer==timer)
            continue;
        delete timer;
    }
    scheduleEvent();
}

void Olsr::handleCrashOperation(LifecycleOperation *operation)
{
    rtable_.clear();
    msgs_.clear();

    while (getTimerMultimMap()->size()>0)
    {
        Olsr_Timer * timer = check_and_cast<Olsr_Timer *>(getTimerMultimMap()->begin()->second);
        getTimerMultimMap()->erase(getTimerMultimMap()->begin());
        timer->setTuple(nullptr);
        if (helloTimer==timer)
            continue;
        else if (tcTimer==timer)
            continue;
        else if (midTimer==timer)
            continue;
        delete timer;
    }
    scheduleEvent();
}
} // namespace inetmanet

} // namespace inet
