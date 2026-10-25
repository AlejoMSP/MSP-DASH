/*******************************************************************************
 * Size: 20 px
 * Bpp: 4
 * Opts: --font /tmp/mdi/mdi-font-7.4.47/fonts/materialdesignicons-webfont.ttf -r 0xF01FA=>0xE001 --size 20 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h -o /home/claude/MSP-DASH/tools/icons/../../esp32s3-amoled-bsp-base/font_icons_20.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef FONT_ICONS_20
#define FONT_ICONS_20 1
#endif

#if FONT_ICONS_20

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+E001 "" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x1, 0xff, 0xff, 0xff,
    0x90, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xaa,
    0xdf, 0xaa, 0x50, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x9f, 0x10, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x8, 0xff, 0xff, 0xff, 0xff, 0xf1,
    0x2, 0x20, 0x4, 0x20, 0x7f, 0xff, 0xff, 0xff,
    0xff, 0xf1, 0x1f, 0xf6, 0x1f, 0x90, 0xbf, 0xff,
    0xff, 0xff, 0xff, 0xf1, 0x1f, 0xf6, 0x1f, 0x90,
    0xbf, 0xff, 0xff, 0xff, 0xff, 0xfa, 0xaf, 0xf6,
    0x1f, 0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xf6, 0x1f, 0xfd, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xf6, 0x1f, 0x90, 0xbf, 0xff,
    0xff, 0xff, 0xff, 0xfb, 0xbf, 0xf6, 0x1f, 0x90,
    0xbf, 0xff, 0xff, 0xff, 0xff, 0xf1, 0x1f, 0xf6,
    0x5, 0x30, 0x45, 0x6e, 0xff, 0xff, 0xff, 0xf1,
    0x1f, 0xf6, 0x0, 0x0, 0x0, 0x3, 0xef, 0xff,
    0xff, 0xf1, 0x2, 0x21, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 320, .box_w = 20, .box_h = 15, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 57345, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LV_VERSION_CHECK(8, 0, 0)
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LV_VERSION_CHECK(8, 0, 0)
    .cache = &cache
#endif
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LV_VERSION_CHECK(8, 0, 0)
const lv_font_t font_icons_20 = {
#else
lv_font_t font_icons_20 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc           /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
};



#endif /*#if FONT_ICONS_20*/

