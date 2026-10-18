#pragma once
using esp_timer_handle_t=void*;
constexpr int ESP_OK=0,ESP_TIMER_TASK=0;
struct esp_timer_create_args_t{void(*callback)(void*)=nullptr;int dispatch_method=0;const char*name=nullptr;bool skip_unhandled_events=false;};
inline void(*sampleCallback)(void*)=nullptr;
inline int esp_timer_create(const esp_timer_create_args_t*a,esp_timer_handle_t*t){sampleCallback=a->callback;*t=(void*)1;return 0;}
inline int esp_timer_start_periodic(esp_timer_handle_t,unsigned){return 0;}
inline int esp_timer_delete(esp_timer_handle_t){return 0;}

inline int64_t fakeUs=0;
inline int64_t esp_timer_get_time(){return fakeUs;}
