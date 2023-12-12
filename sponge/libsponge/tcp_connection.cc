#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return ms_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if(TCPState::state_summary(_sender)==TCPSenderStateSummary::CLOSED && seg.header().ack){
        return ;
    }
    bool need_send_segment;
    need_send_segment = (seg.length_in_sequence_space()!=0) || (_receiver.ackno().has_value() && (seg.length_in_sequence_space()==0) && (seg.header().seqno.raw_value()<_receiver.ackno().value().raw_value()));
    ms_since_last_segment_received = 0;
    // you code here.
    //! _receiver
    //1. 如果设置了RST标志，将入站流和出站流都设置为错误状态，并永久终止连接。
    if(seg.header().rst){
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _is_active = false;
        return ;
    }
    //2. 把这个段交给TCPReceiver，这样它就可以在传入的段上检查它关心的字段：seqno、SYN、负载以及FIN。
    _receiver.segment_received(seg);
    //3. 如果设置了ACK标志，则告诉TCPSender它关心的传入段的字段：ackno和window_size。
    if(seg.header().ack){
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
    //你需要考虑到ACK包、RST包等多种情况
    
    //状态变化(按照个人的情况可进行修改)
    // 如果是 LISEN 到了 SYN
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
        // 此时肯定是第一次调用 fill_window，因此会发送 SYN + ACK
        connect();
        return;
    }

    // 判断 TCP 断开连接时是否时需要等待
    // CLOSE_WAIT
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
        _linger_after_streams_finish = false;

    // 如果到了准备断开连接的时候。服务器端先断
    // CLOSED
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
        _is_active = false;
        return;
    }

    if(_sender.segments_out().empty() && need_send_segment){
        _sender.send_empty_segment();
    }
    fill_window();
}

bool TCPConnection::active() const { return _is_active; }

size_t TCPConnection::write(const string &data) {
    if(!_is_active){
        return 0;
    }
    size_t write_length;
    write_length = _sender.stream_in().write(data);
    _sender.fill_window();
    fill_window();
    return write_length;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) 
{
    if(!_is_active){
        return ;
    }
    ms_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    fill_window();
    if(_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS){
        send_rst_segment();
        return ;
    }
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && _linger_after_streams_finish) {
        if(ms_since_last_segment_received >= 10*_cfg.rt_timeout){
            _is_active = false;
        }
    }
}

void TCPConnection::end_input_stream() 
{
    _sender.stream_in().end_input();
    _sender.fill_window();
    fill_window();
}

void TCPConnection::connect() 
{
    //发送SYN + ACK
    _sender.fill_window();      //? 如何判断是否要发送ack, fill_window()函数暂未实现对ack的发送和ackno的设置
    fill_window();              //由connect调用fill_window实现
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            send_rst_segment();
            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::fill_window(){
    if(!_is_active){
        return ;
    }
    //修改_sender中seg的ackno和ack信号，并存到connect中
    while(!_sender.segments_out().empty()){
        TCPSegment seg_to_send = _sender.segments_out().front();
        _sender.segments_out().pop();
        if(_receiver.ackno().has_value()){
            seg_to_send.header().ackno = _receiver.ackno().value();
            seg_to_send.header().ack = true;
            seg_to_send.header().win = _receiver.window_size();
        }
        _segments_out.push(seg_to_send);
    }
}

void TCPConnection::send_rst_segment(){
    while(!_segments_out.empty()){          //? 虽然不知道为什么要这样
        _segments_out.pop();
    }
    _sender.send_empty_segment();
    TCPSegment seg_to_send = _sender.segments_out().front();
    _sender.segments_out().pop();
    seg_to_send.header().rst = true;
    _segments_out.push(seg_to_send);
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _is_active = false;
}