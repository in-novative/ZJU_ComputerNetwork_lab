#include "wrapping_integers.hh"
#include <iostream>

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t ret;
    ret = static_cast<uint32_t>(n) + isn.raw_value();
    return WrappingInt32(ret);
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint64_t offset = n.raw_value() - isn.raw_value();
    uint64_t choice_1, choice_2, choice_3, distance_1, distance_2, distance_3;
    choice_2 = (checkpoint&0xffffffff00000000) + offset;
    choice_1 = choice_2 - (1UL<<32);
    choice_3 = choice_2 + (1UL<<32);
    distance_1 = checkpoint - choice_1;
    distance_2 = (checkpoint>choice_2) ? checkpoint-choice_2 : choice_2-checkpoint;
    distance_3 = choice_3 - checkpoint;
    //std::cout << "c1=" << choice_1 << " c2=" << choice_2 << " c3=" << choice_3 << std::endl;
    if(choice_1 > choice_2){                    //向下溢出
        return (distance_2<distance_3) ? choice_2 : choice_3;
    }
    else if(choice_3 < choice_2){               //向上溢出
        return (distance_2<distance_1) ? choice_2 : choice_1;
    }
    else {                                      //无溢出
        return (distance_1<distance_2) ?
                    (distance_1<distance_3) ? choice_1 : choice_3 : 
                    (distance_2<distance_3) ? choice_2 : choice_3;
    }
}