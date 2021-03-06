/*
 * AIEngine a new generation network intrusion detection system.
 *
 * Copyright (C) 2013-2018  Luis Campo Giralte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Ryadnology Team; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Ryadnology Team, 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * Written by Luis Campo Giralte <me@ryadpasha.com> 
 *
 */
#include "TCPProtocol.h"
#include <iomanip> // setw

namespace aiengine {

TCPProtocol::TCPProtocol(const std::string& name, const std::string& short_name):
	Protocol(name,short_name),
	flow_table_(),
	flow_cache_(),
	rm_(),
	tcp_info_cache_(new Cache<TCPInfo>("TCP info cache")),
	current_flow_(nullptr),
	header_(nullptr),
	total_events_(0),
	total_flags_syn_(0),
	total_flags_synack_(0),
	total_flags_ack_(0),
	total_flags_rst_(0),
	total_flags_fin_(0),
#if defined(HAVE_TCP_QOS_METRICS)
	total_connection_setup_time_(0),
	total_server_reset_rate_(0),
	total_application_response_time_(0),
#endif
	last_timeout_(0),
	packet_time_(0),
#ifdef HAVE_REJECT_FUNCTION
        reject_func_([] (Flow*) {}),
#endif
	anomaly_() {}

TCPProtocol::~TCPProtocol() { 

	anomaly_.reset(); 
}

bool TCPProtocol::tcpChecker(Packet &packet) {

	int length = packet.getLength();

	if (length >= header_size) {
		setHeader(packet.getPayload());

		if (getTcpHdrLength() > length ) {
			// The packet header lengths dont match but there is
			// a minimal TCP header on the packet
			packet.setPacketAnomaly(PacketAnomalyType::TCP_BOGUS_HEADER);
			anomaly_->incAnomaly(PacketAnomalyType::TCP_BOGUS_HEADER);
			++total_events_;
		}
		++total_valid_packets_;
		return true;
	} else {
		++total_invalid_packets_;
		return false;
	}
}

void TCPProtocol::setDynamicAllocatedMemory(bool value) {

	tcp_info_cache_->setDynamicAllocatedMemory(value);
	flow_cache_->setDynamicAllocatedMemory(value);
}

bool TCPProtocol::isDynamicAllocatedMemory() const {

	return tcp_info_cache_->isDynamicAllocatedMemory();
}	

int32_t TCPProtocol::getTotalCacheMisses() const {

	int32_t miss = 0;

	miss += flow_cache_->getTotalFails();
	miss += tcp_info_cache_->getTotalFails();

	return miss;
}

int64_t TCPProtocol::getCurrentUseMemory() const {

	int64_t mem = sizeof(TCPProtocol);

	mem += flow_cache_->getCurrentUseMemory();
	mem += tcp_info_cache_->getCurrentUseMemory();

	return mem;
}

int64_t TCPProtocol::getAllocatedMemory() const {

	int64_t mem = sizeof(TCPProtocol);

        mem += flow_cache_->getAllocatedMemory();
	mem += tcp_info_cache_->getAllocatedMemory();
	mem += flow_table_->getAllocatedMemory();

        return mem;
}

int64_t TCPProtocol::getTotalAllocatedMemory() const {

	return getAllocatedMemory();	
}

void TCPProtocol::increaseAllocatedMemory(int number) {

        flow_cache_->createFlows(number);
        tcp_info_cache_->create(number);
}

void TCPProtocol::decreaseAllocatedMemory(int number) {

        flow_cache_->destroyFlows(number);
        tcp_info_cache_->destroy(number);
}

void TCPProtocol::statistics(std::basic_ostream<char> &out, int level) {

        if (level > 0) {
                int64_t alloc_memory = getAllocatedMemory();
                std::string unit = "Bytes";
		const char *dynamic_memory = (isDynamicAllocatedMemory() ? "yes":"no");

                unitConverter(alloc_memory, unit);

                out << getName() << "(" << this <<") statistics" << std::dec << "\n";
		out << "\t" << "Dynamic memory alloc:   " << std::setw(10) << dynamic_memory << "\n";
		out << "\t" << "Total allocated:        " << std::setw(9 - unit.length()) << alloc_memory << " " << unit << "\n";
                out << "\t" << "Total packets:          " << std::setw(10) << total_packets_ << "\n";
                out << "\t" << "Total bytes:        " << std::setw(14) << total_bytes_ << std::endl;
                if (level > 1) {
                        out << "\t" << "Total valid packets:    " << std::setw(10) << total_valid_packets_ << "\n";
                        out << "\t" << "Total invalid packets:  " << std::setw(10) << total_invalid_packets_ << "\n";
                        if (level > 3) {
                                out << "\t" << "Total syns:             " << std::setw(10) << total_flags_syn_ << "\n";
                                out << "\t" << "Total synacks:          " << std::setw(10) << total_flags_synack_ << "\n";
                                out << "\t" << "Total acks:             " << std::setw(10) << total_flags_ack_ << "\n";
                                out << "\t" << "Total fins:             " << std::setw(10) << total_flags_fin_ << "\n";
                                out << "\t" << "Total rsts:             " << std::setw(10) << total_flags_rst_ << std::endl;
                        }
                        if (level > 2) {
                                if (mux_.lock())
                                        mux_.lock()->statistics(out);
                                if (flow_forwarder_.lock())
                                        flow_forwarder_.lock()->statistics(out);
                                if (level > 3) {
                                        if (flow_table_)
                                                flow_table_->statistics(out);
                                        if (flow_cache_)
                                                flow_cache_->statistics(out);
					if (tcp_info_cache_)
						tcp_info_cache_->statistics(out);
                                 }
                        }
                }
        }
}

SharedPointer<Flow> TCPProtocol::getFlow(const Packet &packet) {

	SharedPointer<Flow> flow; 

        if (flow_table_) {
        	MultiplexerPtrWeak downmux = mux_.lock()->getDownMultiplexer();
        	MultiplexerPtr ipmux = downmux.lock();

        	unsigned long h1 = ipmux->address.getHash(getSourcePort(), IPPROTO_TCP, getDestinationPort());
        	unsigned long h2 = ipmux->address.getHash(getDestinationPort(), IPPROTO_TCP, getSourcePort());
           
		if (packet.haveTag() == true) {
			h1 = h1 ^ packet.getTag();
			h2 = h2 ^ packet.getTag();
		}	
 
		flow = flow_table_->findFlow(h1, h2);
                if (!flow) {
                        if (flow_cache_) {
                                flow = flow_cache_->acquireFlow();
                                if (flow) {
                                        flow->setId(h1);
					flow->regex_mng = rm_; // Sets the default regex set
					if (packet.haveTag() == true) {
						flow->setTag(packet.getTag());
					}

                                       	// The time of the flow must be insert on the FlowManager table
                                       	// in order to keep the index updated
                                       	flow->setArriveTime(packet_time_);
                                       	flow->setLastPacketTime(packet_time_);

					if (ipmux->address.getType() == IPPROTO_IP) {
                                       		flow->setFiveTuple(ipmux->address.getSourceAddress(),
                                        		getSourcePort(),
							IPPROTO_TCP,
                                                	ipmux->address.getDestinationAddress(),
                                                	getDestinationPort());
					} else {
                                       		flow->setFiveTuple6(ipmux->address.getSourceAddress6(),
                                        		getSourcePort(),
							IPPROTO_TCP,
                                                	ipmux->address.getDestinationAddress6(),
                                                	getDestinationPort());
					}
					
					flow_table_->addFlow(flow);

					// Now attach a TCPInfo to the TCP Flow
					SharedPointer<TCPInfo> tcp_info_ptr = tcp_info_cache_->acquire();
					if (tcp_info_ptr) {
						flow->layer4info = tcp_info_ptr;
					}
                                        
#if defined(BINDING)
                                        if (getDatabaseObjectIsSet()) { // There is attached a database object
						databaseAdaptorInsertHandler(flow.get());
                                        }
#endif
                                }
                        }
                } else {
			/* In order to identificate the flow direction we use the port */
			/* May be there is another way to do it, but this way consume low CPU */
			if (getSourcePort() == flow->getSourcePort()) {
				flow->setFlowDirection(FlowDirection::FORWARD);
			} else {
				flow->setFlowDirection(FlowDirection::BACKWARD);
			}
		}
        }
        return flow;
}

bool TCPProtocol::processPacket(Packet &packet) {

	packet_time_ = packet.getPacketTime();
	SharedPointer<Flow> flow = getFlow(packet);
	current_flow_ = flow.get();

	total_bytes_ += packet.getLength(); 
	++total_packets_;

        if (flow) {
		SharedPointer<TCPInfo> tcp_info = flow->getTCPInfo();
		if (tcp_info) {
        		MultiplexerPtrWeak downmux = mux_.lock()->getDownMultiplexer();
        		MultiplexerPtr ipmux = downmux.lock();

			int bytes = (ipmux->total_length - ipmux->getHeaderSize() - getTcpHdrLength());
			
			flow->total_bytes += bytes;
			++flow->total_packets;

			if (flow->getPacketAnomaly() == PacketAnomalyType::NONE) {
				flow->setPacketAnomaly(packet.getPacketAnomaly());
			}

			compute_tcp_state(tcp_info.get(), bytes);
#ifdef DEBUG
                	std::cout << __FILE__ << ":" << __func__ << ":flow(" << current_flow_ << ") pkts:" << flow->total_packets;
			std::cout << " bytes:" << bytes << " " << *tcp_info.get() ;
			std::cout << "ip.size:" << ipmux->getHeaderSize() << " tcp.size:" << getTcpHdrLength() << std::endl;
#endif

                        if (flow->total_packets == 1) { // Just need to check once per flow
                                if (ipset_mng_) {
                                        if (ipset_mng_->lookupIPAddress(flow->getAddress())) {
                                                SharedPointer<IPAbstractSet> ipset = ipset_mng_->getMatchedIPSet();
                                                flow->ipset = ipset;
#ifdef DEBUG
                                                std::cout << __FILE__ << ":" << __func__ << ":flow:" << flow << ":Lookup positive on IPSet:" << ipset->getName() << std::endl;
#endif
						++total_events_;
#if defined(BINDING)
                                                if (ipset->call.haveCallback()) {
                                                        // the ipset have a calblack that needs to be executed
                                                        ipset->call.executeCallback(flow.get());
                                                }
#endif
						if (ipset->haveRegexManager()) {
                                                        // The ipset have attached a regexmanager 
							flow->regex_mng = ipset->getRegexManager();
						}
                                        }
                                }
                        }

			if (!flow_forwarder_.expired()&&(bytes > 0)) {
				SharedPointer<FlowForwarder> ff = flow_forwarder_.lock();

				// Modify the packet for the next level
				packet.setPayload(&packet.getPayload()[getTcpHdrLength()]);
				packet.setPrevHeaderSize(getTcpHdrLength());
				packet.setPayloadLength(bytes);	

				packet.setDestinationPort(getDestinationPort());
				packet.setSourcePort(getSourcePort());

				flow->packet = static_cast<Packet*>(&packet);
				ff->forwardFlow(flow.get());
			} else {
				// Retrieve the flow to the flow cache if the flow have been closed	
				if ((tcp_info->state_prev == static_cast<int>(TcpState::CLOSED))and(tcp_info->state_curr == static_cast<int>(TcpState::CLOSED))) {
					if (flow->total_packets > 4) { // syn, syn/ack , ack and ack with data
#ifdef DEBUG
						std::cout << __FILE__ << ":" << __func__ << ":flow:[" << *flow << "]:retrieving to flow cache" << std::endl; 
#endif

#if defined(BINDING)
                                        	if (getDatabaseObjectIsSet()) { // There is attached a database object
							databaseAdaptorRemoveHandler(flow.get());
                                        	}
#endif
						if (flow_table_->haveReleaseFlows()) { // The FlowManager needs to release the flows from memory
							auto ff = flow->forwarder.lock();

							if (ff) {
								ProtocolPtr l7proto = ff->getProtocol();
								if (l7proto)
									l7proto->releaseFlowInfo(flow.get());
							}

							tcp_info_cache_->release(tcp_info);
							flow_table_->removeFlow(flow);
							flow_cache_->releaseFlow(flow);
						}
					}

#if defined(BINDING)
                        		packet.setAccept(flow->isAccept());
#endif
					return true; // I dont like but sometimes.....
				}
			}

#if defined(BINDING)
                       	if (getDatabaseObjectIsSet()) { // There is attached a database object
                		if ((packet.forceAdaptorWrite())or((flow->total_packets % getPacketSampling()) == 0)) {
					databaseAdaptorUpdateHandler(flow.get());
					packet.setForceAdaptorWrite(false);
                        	}
                	}
                        packet.setAccept(flow->isAccept());
#endif
			// Verify if the flow have been label for forensic analysis
			if (flow->haveEvidence()) {
                        	packet.setEvidence(flow->haveEvidence());
                        }

#ifdef HAVE_REJECT_FLOW
			// Check if the flow have been rejected by the external login in python/ruby
			if (flow->isReject()) {
				reject_func_(flow.get());
				flow->setReject(false);
				flow->setPartialReject(true);
			}
#endif
			// Check if we need to update the timers of the flow manager
               		if ((packet_time_ - flow_table_->getTimeout()) > last_timeout_ ) {
                       		last_timeout_ = packet_time_;
                       		flow_table_->updateTimers(packet_time_);
               		} else {
                		if ((flow->total_packets % FlowManager::flowTimeRefreshRate) == 1) {
                        		flow_table_->updateFlowTime(flow, packet_time_);
                		} else {
                        		flow->setLastPacketTime(packet_time_);
                		}
			}
		}
	}

	return true;
}

void TCPProtocol::compute_tcp_state(TCPInfo *info, int32_t bytes) {

	bool syn = isSyn();
	bool ack = isAck();
	bool fin = isFin();
	bool rst = isRst();
	int flags = static_cast<int>(TcpFlags::INVALID);
	char *str_flag __attribute__((unused)) = (char*)"None";
	char *str_num __attribute__((unused)) = (char*)"None";

	bool bad_flags = false;
	int flowdir = static_cast<int>(current_flow_->getFlowDirection());
	int prev_flowdir __attribute__((unused)) = static_cast<int>(current_flow_->getPrevFlowDirection());
	uint32_t seq_num = getSequence();
	uint32_t ack_num __attribute__((unused)) = getAckSequence();
	uint32_t next_seq_num = 0;
	uint32_t next_ack_num __attribute__((unused)) = 0;
	int state = info->state_curr;

	if (syn) { 
		if (ack) {
			flags = static_cast<int>(TcpFlags::SYNACK);
			str_flag = (char*)"SynAck";
			++ info->syn_ack;
			++ total_flags_synack_;
				
			info->seq_num[flowdir] = seq_num;
		} else {
			flags = static_cast<int>(TcpFlags::SYN);
			str_flag = (char*)"Syn";
			++ info->syn;
			++ total_flags_syn_;

			info->seq_num[flowdir] = seq_num + 1;
			++seq_num;
#if defined(HAVE_TCP_QOS_METRICS)
			info->connection_setup_time = current_flow_->getLastPacketTime();
#endif
		}
                if (fin) { 
			bad_flags = true;
			++ info->fin;
			++ total_flags_fin_;
		}
		if (rst) {
			bad_flags = true;
                }
	} else {
		if ((ack)&&(fin)) {
			flags = static_cast<int>(TcpFlags::FIN);
			str_flag = (char*)"Fin";
			++ total_flags_fin_;
			++ info->fin;
		} else {
			if (fin) {
				flags = static_cast<int>(TcpFlags::FIN);
				str_flag = (char*)"Fin";
				++ total_flags_fin_;
				++ info->fin;
			} else {
				flags = static_cast<int>(TcpFlags::ACK);
				str_flag = (char*)"Ack";
				++ total_flags_ack_;
				++ info->ack;
#if defined(HAVE_TCP_QOS_METRICS)
				if (info->ack == 1) {
					info->connection_setup_time = current_flow_->getLastPacketTime() - info->connection_setup_time;
					total_connection_setup_time_ += info->connection_setup_time;
				}
				// TODO: Application response time, time between client and server with payload
				if (bytes > 0) {
					if ( flowdir == static_cast<int>(FlowDirection::FORWARD)) { // Client data
						info->last_client_data_time = current_flow_->getLastPacketTime();
					} else { // Server data, so compute the values
						info->application_response_time = current_flow_->getLastPacketTime() - info->last_client_data_time;
						total_application_response_time_ += info->application_response_time;
					}
				}
#endif
			}
		}
		if (isPushSet()) {
			++ info->push;
		}
	}

	if (bad_flags) {
		++total_events_;
		if (current_flow_->getPacketAnomaly() == PacketAnomalyType::NONE) {
			current_flow_->setPacketAnomaly(PacketAnomalyType::TCP_BAD_FLAGS);
		}
		anomaly_->incAnomaly(current_flow_,PacketAnomalyType::TCP_BAD_FLAGS);
	}

	// Check if the sequence numbers are fine
	if (seq_num == info->seq_num[flowdir]) {
		str_num = (char*)"numOK";
	} else {
		// Duplicated packets or retransmited
		str_num = (char*)"numBad";
	}
			
	next_seq_num = seq_num + bytes;
	info->seq_num[flowdir] = next_seq_num;

	info->state_prev = info->state_curr;
	
	// Compute the new transition state
	int newstate = ((tcp_states[static_cast<int>(state)]).state)->dir[flowdir].flags[flags];

	if (newstate == -1) {
		// Continue on the same state
		newstate = info->state_prev;
	}
	info->state_curr = newstate;
	if (rst) {
		// Hard reset, close the flow 
		info->state_prev = static_cast<int>(TcpState::CLOSED);
		info->state_curr = static_cast<int>(TcpState::CLOSED);
		++ total_flags_rst_;
		++info->rst;
	}
#if defined(HAVE_TCP_QOS_METRICS)
        // Compute the number of rsts per second
        if (current_flow_->getLastPacketTime() - info->last_sample_time >= 1) {
       		if (current_flow_->getDuration() > 0) { 
	        	info->server_reset_rate = info->rst / current_flow_->getDuration();
			total_server_reset_rate_ += info->server_reset_rate;
		}
        }

        info->last_sample_time = current_flow_->getLastPacketTime();
#endif

#ifdef DEBUG
	const char *prev_state = ((tcp_states[info->state_prev]).state)->name;
	const char *curr_state = ((tcp_states[info->state_curr]).state)->name;
	std::cout << __FILE__ << ":" << __func__ << ":flow:" << current_flow_ << " prev:" << prev_state << " curr:" << curr_state << " flg:" << str_flag << " " << str_num;
	std::cout << " seq(" << seq_num << ")ack(" << ack_num << ") dir:" << flowdir << " bytes:" << bytes;
	std::cout << " nseq(" << next_seq_num << ")nack(" << next_ack_num << ")" << std::endl;
#endif
}

CounterMap TCPProtocol::getCounters() const {
	CounterMap cm;

        cm.addKeyValue("packets", total_packets_);
        cm.addKeyValue("bytes", total_bytes_);
        cm.addKeyValue("syns", total_flags_syn_);
        cm.addKeyValue("synacks", total_flags_synack_);
        cm.addKeyValue("acks", total_flags_ack_);
        cm.addKeyValue("fins", total_flags_fin_);
        cm.addKeyValue("rsts", total_flags_rst_);

        return cm;
}

} // namespace aiengine
