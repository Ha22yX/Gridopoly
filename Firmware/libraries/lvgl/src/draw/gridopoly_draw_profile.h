#ifndef GRIDOPOLY_DRAW_PROFILE_H
#define GRIDOPOLY_DRAW_PROFILE_H
#include "../misc/lv_area.h"
#if defined(GRIDOPOLY_SELF_TEST) && GRIDOPOLY_SELF_TEST == 1
#ifdef __cplusplus
extern "C" {
#endif
/* Diagnostic aggregates span completed last-flush boundaries. Mask costs are
 * nested within drawing stages and must not be added to them a second time. */
typedef struct {
    uint32_t bgUs, bgImageUs, borderUs, outlineUs, shadowUs;
    uint32_t maskInitUs, maskCalcUs, maskHits, maskMisses, rectCalls;
    uint32_t peakRectUs;
    lv_area_t peakArea, peakClip;
    int16_t peakRadius, peakBorder, peakShadow;
    uint8_t peakOpacity;
    uint8_t flushCount;
    uint16_t flushDropped;
    lv_area_t flushAreas[6];
} GridopolyDrawProfile;
extern GridopolyDrawProfile gridopoly_draw_profile;
extern bool gridopoly_profile_outer_clip_enabled;
uint32_t gridopoly_profile_now_us(void);
#ifdef __cplusplus
}
#endif
#define GRIDOPOLY_PROFILE_STAGE(field, call) do { \
    uint32_t gp_started = gridopoly_profile_now_us(); \
    call; \
    gridopoly_draw_profile.field += gridopoly_profile_now_us() - gp_started; \
} while(0)
#else
#define GRIDOPOLY_PROFILE_STAGE(field, call) do { call; } while(0)
#endif
#endif
