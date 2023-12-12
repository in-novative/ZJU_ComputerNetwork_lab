#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>
#include <map>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    bool wait_check{false};
    ARPMessage arp_request;
    EthernetFrame eth_frame;
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetAddress next_hop_mac = {00, 00, 00, 00, 00, 00};
    //初始化以太帧的发送地址
    eth_frame.header().src=_ethernet_address;
    //在arp表中查找ip对应的mac地址    
    for(arp_iter=arp_table.begin(); arp_iter!=arp_table.end(); arp_iter++){
        if(arp_iter->ipv4==next_hop_ip){
            next_hop_mac = arp_iter->mac;
            break;
        }
    }
    //没有找到对应的mac地址
    if(next_hop_mac==std::array<uint8_t, 6>{00, 00, 00, 00, 00, 00}){
        for(wait_iter=wait_queue.begin();wait_iter!=wait_queue.end();wait_iter++){      //检查目标ipv4是否在等待队列中
            if(wait_iter->ipv4==next_hop_ip){
                wait_check = true;
                break;
            }
        }
        if(!wait_check || (wait_check && wait_iter->ttl+5*1000<time)){                  //不在等待序列中 或 上一次发送超时需要重发
            if(!wait_check){
                wait_queue.push_back({next_hop_ip, dgram, time});
            }
            else {
                wait_iter->ttl=time;
            }
            //组装广播以太帧
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = next_hop_mac;
            arp_request.target_ip_address = next_hop_ip;
            eth_frame.header().type=EthernetHeader::TYPE_ARP;
            eth_frame.header().dst = ETHERNET_BROADCAST;
            eth_frame.payload() = arp_request.serialize();
            _frames_out.push(eth_frame);
        }
    }
    else{
        //找到对应的mac地址
        //组装发往目标地址的以太帧
        eth_frame.header().type=EthernetHeader::TYPE_IPv4;
        eth_frame.header().dst = arp_iter->mac;
        eth_frame.payload() = dgram.serialize();
        _frames_out.push(eth_frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if(frame.header().dst!=_ethernet_address && frame.header().dst!=ETHERNET_BROADCAST){              //不是该帧的目标地址，忽略
        return nullopt;
    }
    if(frame.header().type==EthernetHeader::TYPE_ARP){
        ARPMessage arp_receive;
        if(arp_receive.parse(frame.payload()) == ParseResult::NoError){
            for(arp_iter=arp_table.begin();arp_iter!=arp_table.end();arp_iter++){       //更新arp表中mac和ip的对应关系
                if(arp_iter->ipv4==arp_receive.sender_ip_address){
                    arp_iter->mac = arp_receive.sender_ethernet_address;
                    arp_iter->ttl = time;
                    break;
                }
            }
            if(arp_iter==arp_table.end()){                                              //arp表中不存在该ipv4和mac的对应关系
                arp_table.push_back({arp_receive.sender_ethernet_address, arp_receive.sender_ip_address, time});
            }
            for(wait_iter=wait_queue.begin();wait_iter!=wait_queue.end();wait_iter++){
                if(wait_iter->ipv4==arp_receive.sender_ip_address){                     //响应并删除该ipv4对应请求
                    EthernetFrame eth_frame;
                    eth_frame.header().type = EthernetHeader::TYPE_IPv4;
                    eth_frame.header().src = _ethernet_address;
                    eth_frame.header().dst = arp_receive.sender_ethernet_address;
                    eth_frame.payload() = wait_iter->data.serialize();
                    _frames_out.push(eth_frame);
                    wait_queue.erase(wait_iter);
                    break;
                }
            }
            if(arp_receive.opcode==ARPMessage::OPCODE_REQUEST && arp_receive.target_ip_address==_ip_address.ipv4_numeric()){
                ARPMessage arp_reply;
                arp_reply.opcode = ARPMessage::OPCODE_REPLY;
                arp_reply.sender_ethernet_address = _ethernet_address;
                arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
                arp_reply.target_ethernet_address = arp_receive.sender_ethernet_address;
                arp_reply.target_ip_address = arp_receive.sender_ip_address;

                EthernetFrame eth_frame;
                eth_frame.header().type = EthernetHeader::TYPE_ARP;
                eth_frame.header().src = _ethernet_address;
                eth_frame.header().dst = arp_receive.sender_ethernet_address;
                eth_frame.payload() = arp_reply.serialize();
                _frames_out.push(eth_frame);
            }
        }
        else {
            return nullopt;
        }
    }
    else if(frame.header().type==EthernetHeader::TYPE_IPv4){
        InternetDatagram dgram;
        if(dgram.parse(frame.payload())==ParseResult::NoError){
            return dgram;
        }
        else {
            return nullopt;
        }
    }
    else{
        return nullopt;
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    time+=ms_since_last_tick;
    for(arp_iter=arp_table.begin();arp_iter!=arp_table.end();){     //检查arp表中mac和ip对应关系是否过期
        if(arp_iter->ttl+30*1000>=time){    //未过期
            arp_iter++;
        }
        else {                              //! erase()后迭代器自动向后移动一位
            arp_table.erase(arp_iter);
        }
    }
    for(wait_iter=wait_queue.begin();wait_iter!=wait_queue.end();wait_iter++){
        if(wait_iter->ttl+5*1000>=time){        //未过期

        }
        else {                                  //过期重发
            wait_iter->ttl=time;
            ARPMessage arp_request;
            EthernetFrame eth_frame;

            eth_frame.header().src=_ethernet_address;
            //组装广播以太帧
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = std::array<uint8_t, 6>{00, 00, 00, 00, 00, 00};
            arp_request.target_ip_address = wait_iter->ipv4;
            
            eth_frame.header().type=EthernetHeader::TYPE_ARP;
            eth_frame.header().dst = ETHERNET_BROADCAST;
            eth_frame.payload() = arp_request.serialize();
            _frames_out.push(eth_frame);
        }
    }
}
