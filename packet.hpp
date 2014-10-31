#pragma once

#include <iostream>
#include "common.hpp"

static void packet_decoder(boost::coroutines::asymmetric_coroutine<uint8_t>::pull_type & source)
{
    // 一次拿到一个字节
    while(source)
    {
        std::cout << "got one byte: " << source.get() << std::endl;
        source();
    }
}

// 数据打包编码
static void datapacker(boost::coroutines::asymmetric_coroutine<uint8_t>::push_type & sink)
{
    // 数据打包，就将数据按照一定的字节封装起来
    // 每个封装后的大小为 62 字节，占用传输时间 0.5s

    if(packet_send_buffer.empty())
        return;

    // 拿到一个包
    auto packet = packet_send_buffer.pull();

    // 按照一定的次序送出
    for(auto b : packet)
        sink(b);
}
