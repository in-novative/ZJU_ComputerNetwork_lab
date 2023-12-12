#include "tcp_receiver.hh"
#include "tcp_state.hh"
#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    static uint64_t abs_seq = 0;
    if(seg.header().fin){
        fin_flag = true;
    }
    if(state==TCP_LISTEN){
        if(seg.header().syn){
                isn = seg.header().seqno.raw_value();
                abs_seq = 1;
                state = TCP_SYN_RECV;
                size = 1;
        }
        else {      //LISTEN & SYN，数据传输未开始
            return ;
        }
    }
    if(!seg.header().syn){
        abs_seq = unwrap(WrappingInt32(seg.header().seqno.raw_value()), WrappingInt32(isn), abs_seq);
    }
    _reassembler.push_substring(seg.payload().copy(), abs_seq - 1, seg.header().fin);
    size = _reassembler.first_unaccepted_idx() + 1;
    if(_reassembler.stream_out().input_ended()){
        size++;
    }
    if(state==TCP_SYN_RECV && fin_flag && _reassembler.stream_out().input_ended()) {
        state = TCP_FIN_RECV;
    }
}

std::optional <WrappingInt32> TCPReceiver::ackno() const {
    if(state==TCP_LISTEN){
        return nullopt;
    }
    else {
        //std::cout << "size=" << size << std::endl;
        return WrappingInt32(wrap(size, WrappingInt32(isn)));
    }
}

size_t TCPReceiver::window_size() const { return _capacity-_reassembler.stream_out().buffer_size();}
