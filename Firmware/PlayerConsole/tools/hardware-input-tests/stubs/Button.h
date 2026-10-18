#pragma once
class Button{public:Button(int,bool){}void attachPressDownEventCb(void(*)(void*,void*),void*){}void attachPressUpEventCb(void(*)(void*,void*),void*){}};
