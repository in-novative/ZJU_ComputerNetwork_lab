#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , RTO{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    //std::cout << "sender fill_window called\n";
    //判断是否要设置SYN
    if(!SYN_send){                          //? 单独发送syn帧，开始时win_size为1
        //std::cout << "syn set\n";
        TCPSegment seg_to_send{};
        seg_to_send.header().syn = true;
        SYN_send = true;
        seg_to_send.header().seqno = wrap(_next_seqno, _isn);
        _next_seqno += seg_to_send.length_in_sequence_space();
        _bytes_in_flight += seg_to_send.length_in_sequence_space();
        _segments_out.push(seg_to_send);
        _segments_wait_list.push_front(seg_to_send);
        timer_valid = true;
        return ;
    }

    size_t _capacity;
    _capacity = _window_size>0 ? _window_size : 1;          //可发送字节数
    _capacity -= _bytes_in_flight;
    while(_capacity>0 && !FIN_send){      //? OR only fin
        TCPSegment seg_to_send{};
        seg_to_send.header().seqno = wrap(_next_seqno, _isn);
        uint16_t bytes_to_send = min(_capacity, TCPConfig::MAX_PAYLOAD_SIZE);

        string _payload = _stream.read(bytes_to_send);
        seg_to_send.payload() = Buffer(std::move(_payload));
        
        bytes_to_send = seg_to_send.payload().size();
        if(_stream.eof() && !FIN_send){
            if(_capacity > seg_to_send.length_in_sequence_space()){
                seg_to_send.header().fin = true;
                FIN_send = true;
            }
        }

        _next_seqno += seg_to_send.length_in_sequence_space();
        _bytes_in_flight += seg_to_send.length_in_sequence_space();
        _capacity -= seg_to_send.length_in_sequence_space();

        if(!seg_to_send.length_in_sequence_space()){
            break;
        }
        
        if(_segments_wait_list.empty()){
            timer = 0;
        }
        _segments_out.push(seg_to_send);
        _segments_wait_list.push_front(seg_to_send);
        timer_valid = true;
    }
    if(_segments_wait_list.empty()){
        timer_valid = false;
        timer = 0;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size){
    if(ackno.raw_value()>wrap(_next_seqno, _isn).raw_value()){
        return ;
    }
    if(ackno.raw_value()<=_last_ack_no && _last_ack_no_valid){
        _window_size = window_size;
        return ;
    }
    TCPSegment seg_to_recv;
    while(!_segments_wait_list.empty()){
        seg_to_recv=_segments_wait_list.back();
        if(seg_to_recv.header().seqno.raw_value() < ackno.raw_value()){
            timer = 0;
            RTO = _initial_retransmission_timeout;
            if(_last_ack_no<=seg_to_recv.header().seqno.raw_value() || !_last_ack_no_valid){      //上一次确认在下一数据包之前，即当前数据包未被确认过
                if(seg_to_recv.length_in_sequence_space() > ackno.raw_value()-seg_to_recv.header().seqno.raw_value()){
                    _bytes_in_flight -= (ackno.raw_value()-seg_to_recv.header().seqno.raw_value());
                    _last_ack_no = ackno.raw_value();
                    break;
                }
                else {
                    _bytes_in_flight -= seg_to_recv.length_in_sequence_space();
                    _last_ack_no = seg_to_recv.header().seqno.raw_value()+seg_to_recv.length_in_sequence_space();
                    _segments_wait_list.pop_back();
                }
            }
            else {                                                           //当前数据包被部分确认过
                if(seg_to_recv.length_in_sequence_space()-(_last_ack_no-seg_to_recv.header().seqno.raw_value()) > ackno.raw_value()-_last_ack_no){
                    _bytes_in_flight -= ackno.raw_value()-_last_ack_no;
                    _last_ack_no = ackno.raw_value();
                    break;
                }
                else {
                    _bytes_in_flight -= seg_to_recv.length_in_sequence_space()-(_last_ack_no-seg_to_recv.header().seqno.raw_value());
                    _last_ack_no = seg_to_recv.header().seqno.raw_value()+seg_to_recv.length_in_sequence_space();
                    _segments_wait_list.pop_back();
                }
            }
            _last_ack_no_valid = true;
        }
        else {
            break;
        }
    }
    if(_segments_wait_list.empty()){        //所有数据都被确认，停止重传计时器
        timer_valid = false;
    }
    _consecutive_retransmissions = 0;
    _window_size = window_size;
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    if(timer_valid){
        timer += ms_since_last_tick;
        if(!_segments_wait_list.empty() && timer>=RTO){
            timer = 0;
            _segments_out.push(_segments_wait_list.back());
            if(_window_size!=0){       //网络拥堵
                RTO *= 2;
                _consecutive_retransmissions++;
            }
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment seg_to_send;
    seg_to_send.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(seg_to_send);
}