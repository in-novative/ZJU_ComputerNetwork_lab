#include "byte_stream.hh"
#include <string.h>
#include <iostream>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

//template <typename... Targs>
//void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t c){
    this->capacity = c;
}

//如果stream的剩余容量要小于读入的data应该如何解决？
//只保留容量范围内的输入
size_t ByteStream::write(const string &data) {
    //std::cout << "ByteStream write " << data << std::endl;
    size_t i;
    for(i=0;i<data.length();i++){
        if(size==capacity){
            break;
        }
        stream.push_back(data[i]);
        size++;
    }
    writtens+=i;
    return i;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string data{};
    for(size_t i=0;i<len&&i<size;i++){
        data+=*(stream.begin()+i);
    }
    return data;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t i;
    for(i=0;i<len;i++){
        if(size==0){
            //set_error();
            break;
        }
        stream.pop_front();
        size--;
    }
    reads+=i;
    //stream_begin = stream.begin();
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    /*for(auto iter=stream.begin();iter!=stream.end();iter++){
        std::cout << *iter;
    }
    std::cout << "\n";*/
    //std::cerr << "check 1" << std::endl;
    string data = peek_output(len);
    //std::cerr << "check 2" << std::endl;
    pop_output(len);
    //std::cerr << "check 3" << std::endl;
    return data;
}

void ByteStream::end_input() {
    _end = true;
}

bool ByteStream::input_ended() const {
    return _end;
}

size_t ByteStream::buffer_size() const {
    return size;
}

bool ByteStream::buffer_empty() const {
    return (size==0);
}

bool ByteStream::eof() const {
    return (writtens==reads)&&(_end);
}

size_t ByteStream::bytes_written() const {
    return writtens;
}

size_t ByteStream::bytes_read() const {
    return reads;
}

size_t ByteStream::remaining_capacity() const {
    return (capacity-size);
}