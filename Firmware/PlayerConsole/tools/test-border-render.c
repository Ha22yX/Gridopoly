#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void baseline_lv_draw_sw_rect(lv_draw_ctx_t *, const lv_draw_rect_dsc_t *, const lv_area_t *);
static lv_color_t original[480 * 480], optimized[480 * 480];
static lv_area_t screen = {0, 0, 479, 479};
static lv_disp_t *display;
static unsigned cases;
static int background_opa = -1, blend_mode;
static unsigned background_color = 0x125438;
static double old_ticks, new_ticks;
static unsigned rng = 0x934127ab;
static unsigned next_random(void) { rng = rng * 1664525u + 1013904223u; return rng; }

static void compare(lv_area_t box, lv_area_t clip, int radius, int width,
                    int sides, int opacity, int antialias, int extras)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = background_opa >= 0 ? background_opa : (extras ? 128 : 0);
    dsc.bg_color = lv_color_hex(background_color);
    dsc.blend_mode = blend_mode;
    if(extras & 8) {
        dsc.bg_grad.dir = (extras & 16) ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER;
        dsc.bg_grad.stops[0].color = lv_color_hex(0x163247);
        dsc.bg_grad.stops[1].color = lv_color_hex(0xdeb643);
    }
    dsc.radius = radius;
    dsc.border_width = width;
    dsc.border_side = sides;
    dsc.border_opa = opacity;
    dsc.border_color = lv_color_hex(0x52dcb7);
    dsc.border_post = (extras & 4) != 0;
    if(extras) {
        dsc.outline_width = 3; dsc.outline_pad = 2;
        dsc.outline_color = lv_color_hex(0xfac325); dsc.outline_opa = 170;
        dsc.shadow_width = 8; dsc.shadow_ofs_x = 2; dsc.shadow_ofs_y = -2;
        dsc.shadow_opa = 180;
    }
    memset(original, 0x5a, sizeof original);
    memset(optimized, 0x5a, sizeof optimized);
    display->driver->antialiasing = antialias;
    lv_draw_ctx_t *ctx = display->driver->draw_ctx;
    ctx->buf_area = &screen; ctx->clip_area = &clip;
    lv_draw_mask_line_param_t mask;
    int16_t mask_id = LV_MASK_ID_INV;
    if(extras & 2) {
        lv_draw_mask_line_points_init(&mask, 80, 70, 400, 390, LV_DRAW_MASK_LINE_SIDE_LEFT);
        mask_id = lv_draw_mask_add(&mask, NULL);
    }
    ctx->buf = original;
    clock_t start = clock();
    baseline_lv_draw_sw_rect(ctx, &dsc, &box);
    old_ticks += clock() - start;
    _lv_draw_mask_cleanup();
    ctx->buf = optimized;
    start = clock();
    lv_draw_sw_rect(ctx, &dsc, &box);
    new_ticks += clock() - start;
    _lv_draw_mask_cleanup();
    if(mask_id != LV_MASK_ID_INV) lv_draw_mask_remove_id(mask_id);
    ++cases;
    if(memcmp(original, optimized, sizeof original)) {
        for(int i = 0; i < 480 * 480; ++i) if(original[i].full != optimized[i].full) {
            fprintf(stderr, "FAIL case=%u r=%d w=%d sides=%d opa=%d aa=%d extras=%d clip=%d,%d,%d,%d pixel=%d,%d old=%04x new=%04x\n",
                cases, radius, width, sides, opacity, antialias, extras,
                clip.x1, clip.y1, clip.x2, clip.y2, i % 480, i / 480,
                original[i].full, optimized[i].full);
            exit(1);
        }
    }
}

int main(void)
{
    lv_init();
    static lv_disp_draw_buf_t draw_buffer;
    lv_disp_draw_buf_init(&draw_buffer, original, NULL, 480 * 480);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = 480; driver.ver_res = 480; driver.draw_buf = &draw_buffer;
    display = lv_disp_drv_register(&driver);
    if(!display) return 2;
    _lv_refr_set_disp_refreshing(display);
    const int radii[] = {0, 1, 2, 6, 12, 196, 209, LV_RADIUS_CIRCLE};
    const int widths[] = {0, 1, 2, 5, 12, 64, 220};
    const int opacities[] = {0, 1, 2, 50, 128, 253, 255};
    const lv_area_t box = {31, 31, 448, 448};
    const lv_area_t clips[] = {
        {104,137,375,302}, {48,300,431,373}, {200,200,280,280},
        {31,31,448,448}, {0,0,479,479}, {31,31,32,32},
        {238,31,242,38}, {238,441,242,448}, {31,238,38,242},
        {441,238,448,242}, {36,36,443,443}, {240,35,240,36}
    };
    for(unsigned r=0; r<sizeof radii/sizeof *radii; ++r)
    for(unsigned w=0; w<sizeof widths/sizeof *widths; ++w)
    for(int side=0; side<16; ++side)
    for(unsigned c=0; c<sizeof clips/sizeof *clips; ++c) {
        compare(box, clips[c], radii[r], widths[w], side,
                opacities[(r+w+side+c)%7], (r+w+side+c)%2, 0);
    }
    for(int i=0; i<6000; ++i) {
        int x=next_random()%480, y=next_random()%480;
        lv_area_t clip={x,y,LV_MIN(x+(next_random()%180),479),LV_MIN(y+(next_random()%180),479)};
        compare(box,clip,radii[next_random()%8],widths[next_random()%7],
                next_random()%16,opacities[next_random()%7],next_random()%2,i%8);
    }
    /* Also exercise non-square and tiny boxes, including off-screen origins. */
    for(int i=0; i<3000; ++i) {
        int x=(int)(next_random()%220)-30, y=(int)(next_random()%220)-30;
        lv_area_t varied={x,y,x+(next_random()%290),y+(next_random()%290)};
        int cx=next_random()%480, cy=next_random()%480;
        lv_area_t clip={cx,cy,LV_MIN(cx+(next_random()%280),479),LV_MIN(cy+(next_random()%280),479)};
        compare(varied,clip,radii[next_random()%8],widths[next_random()%7],
                next_random()%16,opacities[next_random()%7],next_random()%2,i%8);
    }
    /* Tiny clips straddling the curved AA edge at every row. */
    for(int y=31; y<=448; ++y) for(int x=31; x<=448; x+=3) {
        int dx=x-240, dy=y-240;
        int dist=dx*dx+dy*dy;
        if(dist<190*190 || dist>211*211) continue;
        lv_area_t clip={x,y,x+1,y+1};
        compare(box,clip,209,5,LV_BORDER_SIDE_FULL,255,1,0);
    }
    /* Background coverage: interior, AA edge, border shrink, alpha rounding,
     * blend modes, external masks and gradient fallback. */
    const int bg_opacities[] = {0,1,2,3,127,128,251,252,253,254,255};
    const unsigned colors[] = {0,0xffffff,0x125438,0xf537a9};
    for(unsigned o=0; o<sizeof bg_opacities/sizeof *bg_opacities; ++o)
    for(unsigned color=0; color<sizeof colors/sizeof *colors; ++color)
    for(int mode=LV_BLEND_MODE_NORMAL; mode<=LV_BLEND_MODE_MULTIPLY; ++mode) {
        background_opa=bg_opacities[o]; background_color=colors[color]; blend_mode=mode;
        for(unsigned c=0; c<sizeof clips/sizeof *clips; ++c)
        for(unsigned r=0; r<sizeof radii/sizeof *radii; ++r)
            compare(box,clips[c],radii[r],widths[(o+c+r)%7],(o+c+r)%16,
                    opacities[(o+c+r)%7],(c+r)%2,(c+r)%32);
    }
    blend_mode=LV_BLEND_MODE_NORMAL;
    for(int y=31; y<=448; ++y) for(int x=31; x<=448; x+=3) {
        int dx=x-240,dy=y-240,dist=dx*dx+dy*dy;
        if(dist<190*190 || dist>211*211) continue;
        lv_area_t clip={x,y,x+1,y+1};
        background_opa=bg_opacities[(x+y)%11];
        compare(box,clip,209,5,LV_BORDER_SIDE_FULL,255,1,0);
    }
    printf("PASS %u pixel-identical render cases; old_cpu_ms=%.1f new_cpu_ms=%.1f (host timing only)\n",
           cases,old_ticks*1000/CLOCKS_PER_SEC,new_ticks*1000/CLOCKS_PER_SEC);
    return 0;
}
