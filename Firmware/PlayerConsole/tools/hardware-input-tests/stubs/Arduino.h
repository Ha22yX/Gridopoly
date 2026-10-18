#pragma once
#include <cstdint>
#include <climits>
inline uint32_t fakeMs=0, fakeGpio=0;
using gpio_num_t=int;
constexpr int GPIO_NUM_6=6,GPIO_NUM_5=5,GPIO_NUM_0=0,INPUT_PULLUP=1;
inline uint32_t millis(){return fakeMs;}
inline void pinMode(int,int){}
