 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Screen drawing functions
  *
  * Copyright 1995-2000 Bernd Schmidt
  * Copyright 1995 Alessandro Bissacco
  */

/* There are a couple of concepts of "coordinates" in this file.
   - DIW coordinates
   - DDF coordinates (essentially cycles, resolution lower than lores by a factor of 2)
   - Pixel coordinates
     * in the Amiga's resolution as determined by BPLCON0 ("Amiga coordinates")
     * in the window resolution as determined by the preferences ("window coordinates").
     * in the window resolution, and with the origin being the topmost left corner of
       the window ("native coordinates")
   One note about window coordinates.  The visible area depends on the width of the
   window, and the centering code.  The first visible horizontal window coordinate is
   often _not_ 0, but the value of VISIBLE_LEFT_BORDER instead.

   One important thing to remember: DIW coordinates are in the lowest possible
   resolution.

   To prevent extremely bad things (think pixels cut in half by window borders) from
   happening, all ports should restrict window widths to be multiples of 16 pixels.  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <assert.h>

#include "config.h"
#include "options.h"
#include "threaddep/penguin.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "xwin.h"
#include "autoconf.h"
#include "gui.h"
#include "picasso96.h"
#include "drawing.h"

int lores_factor, lores_shift;

/* The shift factor to apply when converting between Amiga coordinates and window
   coordinates.  Zero if the resolution is the same, positive if window coordinates
   have a higher resolution (i.e. we're stretching the image), negative if window
   coordinates have a lower resolution (i.e. we're shrinking the image).  */
static int res_shift;

static int interlace_seen = 0;

/* Lookup tables for dual playfields.  The dblpf_*1 versions are for the case
   that playfield 1 has the priority, dbplpf_*2 are used if playfield 2 has
   priority.  If we need an array for non-dual playfield mode, it has no number.  */
/* The dbplpf_ms? arrays contain a shift value.  plf_spritemask is initialized
   to contain two 16 bit words, with the appropriate mask if pf1 is in the
   foreground being at bit offset 0, the one used if pf2 is in front being at
   offset 16.  */

static int dblpf_ms1[256], dblpf_ms2[256], dblpf_ms[256];
static int dblpf_ind1[256], dblpf_ind2[256];

static int dblpf_2nd1[256], dblpf_2nd2[256];
static int dblpf_ind1_aga[256], dblpf_ind2_aga[256];


static int dblpfofs[] = { 0, 2, 4, 8, 16, 32, 64, 128 };

static int sprite_offs[256];

static uae_u32 clxtab[256];

/* Video buffer description structure. Filled in by the graphics system
 * dependent code. */

struct vidbuf_description gfxvidinfo;

/* OCS/ECS color lookup table. */
xcolnr xcolors[4096];
/* AGA mode color lookup tables */
unsigned int xredcolors[256], xgreencolors[256], xbluecolors[256];

struct color_entry colors_for_drawing;

/* The size of these arrays is pretty arbitrary; it was chosen to be "more
   than enough".  The coordinates used for indexing into these arrays are
   almost, but not quite, Amiga coordinates (there's a constant offset).  */
union {
    /* Let's try to align this thing. */
    double uupzuq;
    long int cruxmedo;
    uae_u8 apixels[MAX_PIXELS_PER_LINE*2];
    uae_u16 apixels_w[MAX_PIXELS_PER_LINE*2/2];
    uae_u32 apixels_l[MAX_PIXELS_PER_LINE*2/4];
} pixdata;

uae_u16 spixels[2 * MAX_SPR_PIXELS];
/* Eight bits for every pixel.  */
union sps_union spixstate;

static uae_u32 ham_linebuf[MAX_PIXELS_PER_LINE];
static uae_u8 spriteagadpfpixels[MAX_PIXELS_PER_LINE]; /* AGA dualplayfield sprite */

char *xlinebuffer;

static int *amiga2aspect_line_map, *native2amiga_line_map;
static char *row_map[2049];
static int max_drawn_amiga_line;

/* line_draw_funcs: pfield_do_linetoscr, pfield_do_fill_line, decode_ham */
typedef void (*line_draw_func)(int, int);

#define LINE_UNDECIDED 1
#define LINE_DECIDED 2
#define LINE_DECIDED_DOUBLE 3
#define LINE_AS_PREVIOUS 4
#define LINE_BLACK 5
#define LINE_REMEMBERED_AS_BLACK 6
#define LINE_DONE 7
#define LINE_DONE_AS_PREVIOUS 8
#define LINE_REMEMBERED_AS_PREVIOUS 9

static char *line_drawn;
static char linestate[(MAXVPOS + 1)*2 + 1];

uae_u8 line_data[(MAXVPOS + 1) * 2][MAX_PLANES * MAX_WORDS_PER_LINE * 2];

/* Centering variables.  */
static int min_diwstart, max_diwstop, prev_x_adjust;
/* The visible window: VISIBLE_LEFT_BORDER contains the left border of the visible
   area, VISIBLE_RIGHT_BORDER the right border.  These are in window coordinates.  */
static int visible_left_border, visible_right_border;
static int linetoscr_x_adjust_bytes;
static int thisframe_y_adjust, prev_y_adjust;
static int thisframe_y_adjust_real, max_ypos_thisframe, min_ypos_for_screen;
static int extra_y_adjust;
int thisframe_first_drawn_line, thisframe_last_drawn_line;

/* A frame counter that forces a redraw after at least one skipped frame in
   interlace mode.  */
static int last_redraw_point;

static int first_drawn_line, last_drawn_line;
static int first_block_line, last_block_line;

/* These are generated by the drawing code from the line_decisions array for
   each line that needs to be drawn.  These are basically extracted out of
   bit fields in the hardware registers.  */
static int bplehb, bplham, bpldualpf, bpldualpfpri, bpldualpf2of, bplplanecnt, bplres, sprres;
static uae_u32 plf_sprite_mask;
static int sbasecol[2];

int picasso_requested_on;
int picasso_on;

uae_sem_t gui_sem;
int inhibit_frame;

int framecnt = 0;
static int frame_redraw_necessary;
static int picasso_redraw_necessary;

STATIC_INLINE void count_frame (void)
{
    framecnt++;
    if (framecnt >= currprefs.gfx_framerate)
	framecnt = 0;
}

int coord_native_to_amiga_x (int x)
{
    x += visible_left_border;
    x <<= (1 - lores_shift);
    return x + 2*DISPLAY_LEFT_SHIFT - 2*DIW_DDF_OFFSET;
}

int coord_native_to_amiga_y (int y)
{
    return native2amiga_line_map[y] + thisframe_y_adjust - minfirstline;
}

STATIC_INLINE int res_shift_from_window (int x)
{
    if (res_shift >= 0)
	return x >> res_shift;
    return x << -res_shift;
}

STATIC_INLINE int res_shift_from_amiga (int x)
{
    if (res_shift >= 0)
	return x >> res_shift;
    return x << -res_shift;
}

void notice_screen_contents_lost (void)
{
    picasso_redraw_necessary = 1;
    frame_redraw_necessary = 2;
}

static struct decision *dp_for_drawing;
static struct draw_info *dip_for_drawing;

/* Record DIW of the current line for use by centering code.  */
void record_diw_line (int first, int last)
{
    if (last > max_diwstop)
	max_diwstop = last;
    if (first < min_diwstart)
	min_diwstart = first;
}

/*
 * Screen update macros/functions
 */

/* The important positions in the line: where do we start drawing the left border,
   where do we start drawing the playfield, where do we start drawing the right border.
   All of these are forced into the visible window (VISIBLE_LEFT_BORDER .. VISIBLE_RIGHT_BORDER).
   PLAYFIELD_START and PLAYFIELD_END are in window coordinates.  */
static int playfield_start, playfield_end;

static int pixels_offset;
static int src_pixel;
/* How many pixels in window coordinates which are to the left of the left border.  */
static int unpainted;

/* Initialize the variables necessary for drawing a line.
 * This involves setting up start/stop positions and display window
 * borders.  */
static void pfield_init_linetoscr (void)
{
    /* First, get data fetch start/stop in DIW coordinates.  */
    int ddf_left = dp_for_drawing->plfleft * 2 + DIW_DDF_OFFSET;
    int ddf_right = dp_for_drawing->plfright * 2 + DIW_DDF_OFFSET;
    /* Compute datafetch start/stop in pixels; native display coordinates.  */
    int native_ddf_left = coord_hw_to_window_x (ddf_left);
    int native_ddf_right = coord_hw_to_window_x (ddf_right);

    int linetoscr_diw_start = dp_for_drawing->diwfirstword;
    int linetoscr_diw_end = dp_for_drawing->diwlastword;

    if (dip_for_drawing->nr_sprites == 0) {
	if (linetoscr_diw_start < native_ddf_left)
	    linetoscr_diw_start = native_ddf_left;
	if (linetoscr_diw_end > native_ddf_right)
	    linetoscr_diw_end = native_ddf_right;
    }

    /* Perverse cases happen. */
    if (linetoscr_diw_end < linetoscr_diw_start)
	linetoscr_diw_end = linetoscr_diw_start;

    playfield_start = linetoscr_diw_start;
    playfield_end = linetoscr_diw_end;

    if (playfield_start < visible_left_border)
	playfield_start = visible_left_border;
    if (playfield_start > visible_right_border)
	playfield_start = visible_right_border;
    if (playfield_end < visible_left_border)
	playfield_end = visible_left_border;
    if (playfield_end > visible_right_border)
	playfield_end = visible_right_border;

    /* Now, compute some offsets.  */

    res_shift = lores_shift - bplres;
    ddf_left -= DISPLAY_LEFT_SHIFT;
    ddf_left <<= bplres;
    pixels_offset = MAX_PIXELS_PER_LINE - ddf_left;

    unpainted = visible_left_border < playfield_start ? 0 : visible_left_border - playfield_start;
    src_pixel = MAX_PIXELS_PER_LINE + res_shift_from_window (playfield_start - native_ddf_left + unpainted);

    if (dip_for_drawing->nr_sprites == 0)
	return;
    /* Must clear parts of apixels.  */
    if (linetoscr_diw_start < native_ddf_left) {
	int size = res_shift_from_window (native_ddf_left - linetoscr_diw_start);
	linetoscr_diw_start = native_ddf_left;
	memset (pixdata.apixels + MAX_PIXELS_PER_LINE - size, 0, size);
    }
    if (linetoscr_diw_end > native_ddf_right) {
	int pos = res_shift_from_window (native_ddf_right - native_ddf_left);
	int size = res_shift_from_window (linetoscr_diw_end - native_ddf_right);
	linetoscr_diw_start = native_ddf_left;
	memset (pixdata.apixels + MAX_PIXELS_PER_LINE + pos, 0, size);
    }
}

/* If C++ compilers didn't suck, we'd use templates.  */

#define TYPE uae_u8
#define LNAME linetoscr_8
#define SRC_INC 1
#define HDOUBLE 0
#define AGA 0
#include "linetoscr.c"
#define LNAME linetoscr_8_stretch1
#define SRC_INC 1
#define HDOUBLE 1
#define AGA 0
#include "linetoscr.c"
#define LNAME linetoscr_8_shrink1
#define SRC_INC 2
#define HDOUBLE 0
#define AGA 0
#include "linetoscr.c"

#define LNAME linetoscr_8_aga
#define SRC_INC 1
#define HDOUBLE 0
#define AGA 1
#include "linetoscr.c"
#define LNAME linetoscr_8_stretch1_aga
#define SRC_INC 1
#define HDOUBLE 1
#define AGA 1
#include "linetoscr.c"
#define LNAME linetoscr_8_shrink1_aga
#define SRC_INC 2
#define HDOUBLE 0
#define AGA 1
#include "linetoscr.c"

#undef TYPE

#define TYPE uae_u16
#define LNAME linetoscr_16
#define SRC_INC 1
#define HDOUBLE 0
#define AGA 0
#include "linetoscr.c"
#define LNAME linetoscr_16_stretch1
#define SRC_INC 1
#define HDOUBLE 1
#define AGA 0
#include "linetoscr.c"
#define LNAME linetoscr_16_shrink1
#define SRC_INC 2
#define HDOUBLE 0
#define AGA 0
#include "linetoscr.c"

#define LNAME linetoscr_16_aga
#define SRC_INC 1
#define HDOUBLE 0
#define AGA 1
#include "linetoscr.c"
#define LNAME linetoscr_16_stretch1_aga
#define SRC_INC 1
#define HDOUBLE 1
#define AGA 1
#include "linetoscr.c"
#define LNAME linetoscr_16_shrink1_aga
#define SRC_INC 2
#define HDOUBLE 0
#define AGA 1
#include "linetoscr.c"

#undef TYPE

#define TYPE uae_u32
#define LNAME linetoscr_32
#define SRC_INC 1
#define HDOUBLE 0
#define AGA 0
#include "linetoscr.c"
#define LNAME linetoscr_32_stretch1
#define SRC_INC 1
#define HDOUBLE 1
#define AGA 0
#include "linetoscr.c"
#define LNAME linetoscr_32_shrink1
#define SRC_INC 2
#define HDOUBLE 0
#define AGA 0
#include "linetoscr.c"

#define LNAME linetoscr_32_aga
#define SRC_INC 1
#define HDOUBLE 0
#define AGA 1
#include "linetoscr.c"
#define LNAME linetoscr_32_stretch1_aga
#define SRC_INC 1
#define HDOUBLE 1
#define AGA 1
#include "linetoscr.c"
#define LNAME linetoscr_32_shrink1_aga
#define SRC_INC 2
#define HDOUBLE 0
#define AGA 1
#include "linetoscr.c"

#undef TYPE

static void fill_line_8 (char *buf, int start, int stop)
{
    uae_u8 *b = (uae_u8 *)buf;
    int i;
    xcolnr col = colors_for_drawing.acolors[0];
    for (i = start; i < stop; i++)
	b[i] = col;
}
static void fill_line_16 (char *buf, int start, int stop)
{
    uae_u16 *b = (uae_u16 *)buf;
    int i;
    xcolnr col = colors_for_drawing.acolors[0];
    for (i = start; i < stop; i++)
	b[i] = col;
}
static void fill_line_32 (char *buf, int start, int stop)
{
    uae_u32 *b = (uae_u32 *)buf;
    int i;
    xcolnr col = colors_for_drawing.acolors[0];
    for (i = start; i < stop; i++)
	b[i] = col;
}

STATIC_INLINE void fill_line (void)
{
    int shift;
    int nints, nrem;
    int *start;
    xcolnr val;

    shift = 0;
    if (gfxvidinfo.pixbytes == 2)
	shift = 1;
    if (gfxvidinfo.pixbytes == 4)
	shift = 2;

    nints = gfxvidinfo.width >> (2-shift);
    nrem = nints & 7;
    nints &= ~7;
    start = (int *)(((char *)xlinebuffer) + (visible_left_border << shift));
    val = colors_for_drawing.acolors[0];
    if (gfxvidinfo.pixbytes == 2)
        val |= val << 16;
    for (; nints > 0; nints -= 8, start += 8) {
	*start = val;
	*(start+1) = val;
	*(start+2) = val;
	*(start+3) = val;
	*(start+4) = val;
	*(start+5) = val;
	*(start+6) = val;
	*(start+7) = val;
    }

    switch (nrem) {
     case 7:
	*start++ = val;
     case 6:
	*start++ = val;
     case 5:
	*start++ = val;
     case 4:
	*start++ = val;
     case 3:
	*start++ = val;
     case 2:
	*start++ = val;
     case 1:
	*start = val;
    }
}

static int linetoscr_double_offset;

static void pfield_do_linetoscr (int start, int stop)
{
    if (currprefs.chipset_mask & CSMASK_AGA) {
	if (res_shift == 0)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_aga (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_aga (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_aga (src_pixel, start, stop); break;
	    }
	else if (res_shift == 1)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_stretch1_aga (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_stretch1_aga (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_stretch1_aga (src_pixel, start, stop); break;
	    }
	else if (res_shift == -1)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_shrink1_aga (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_shrink1_aga (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_shrink1_aga (src_pixel, start, stop); break;
	    }
        else
	    abort ();

    } else {

        if (res_shift == 0)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8 (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16 (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32 (src_pixel, start, stop); break;
	    }
	else if (res_shift == 1)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_stretch1 (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_stretch1 (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_stretch1 (src_pixel, start, stop); break;
	    }
	else if (res_shift == -1)
	    switch (gfxvidinfo.pixbytes) {
	    case 1: src_pixel = linetoscr_8_shrink1 (src_pixel, start, stop); break;
	    case 2: src_pixel = linetoscr_16_shrink1 (src_pixel, start, stop); break;
	    case 4: src_pixel = linetoscr_32_shrink1 (src_pixel, start, stop); break;
	    }
	else
	    abort ();
    }
}

static void pfield_do_fill_line (int start, int stop)
{
    switch (gfxvidinfo.pixbytes) {
    case 1: fill_line_8 (xlinebuffer, start, stop); break;
    case 2: fill_line_16 (xlinebuffer, start, stop); break;
    case 4: fill_line_32 (xlinebuffer, start, stop); break;
    }
}

static void pfield_do_linetoscr_full (int double_line)
{
    char *oldxlb = (char *)xlinebuffer;
    int old_src_pixel = src_pixel;

    pfield_do_linetoscr (playfield_start, playfield_end);
    xlinebuffer = oldxlb + linetoscr_double_offset;
    src_pixel = old_src_pixel;
    pfield_do_linetoscr (playfield_start, playfield_end);
}

static void dummy_worker (int start, int stop)
{
}

static unsigned int ham_lastcolor;

static int ham_decode_pixel;

/* Decode HAM in the invisible portion of the display (left of VISIBLE_LEFT_BORDER),
   but don't draw anything in.  This is done to prepare HAM_LASTCOLOR for later,
   when decode_ham runs.  */
static void init_ham_decoding (void)
{
    int unpainted_amiga = res_shift_from_window (unpainted);
    ham_decode_pixel = src_pixel;
    ham_lastcolor = color_reg_get (&colors_for_drawing, 0);

    if (currprefs.chipset_mask & CSMASK_AGA) {
	if (bplplanecnt == 8) { /* AGA mode HAM8 */
	    while (unpainted_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel++];
		switch (pv & 0x3) {
		case 0x0: ham_lastcolor = colors_for_drawing.color_regs_aga[pv >> 2]; break;
		case 0x1: ham_lastcolor &= 0xFFFF03; ham_lastcolor |= (pv & 0xFC); break;
		case 0x2: ham_lastcolor &= 0x03FFFF; ham_lastcolor |= (pv & 0xFC) << 16; break;
		case 0x3: ham_lastcolor &= 0xFF03FF; ham_lastcolor |= (pv & 0xFC) << 8; break;
		}
	    }
	} else if(bplplanecnt == 6) { /* AGA mode HAM6 */
	    while (unpainted_amiga-- > 0) {
    		int pv = pixdata.apixels[ham_decode_pixel++];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_aga[pv]; break;
		case 0x10: ham_lastcolor &= 0xFFFF00; ham_lastcolor |= (pv & 0xF) << 4; break;
		case 0x20: ham_lastcolor &= 0x00FFFF; ham_lastcolor |= (pv & 0xF) << 20; break;
		case 0x30: ham_lastcolor &= 0xFF00FF; ham_lastcolor |= (pv & 0xF) << 12; break;
		}
	    }
	}
    } else {
        if (bplplanecnt == 6) { /* OCS/ECS mode HAM6 */
	    while (unpainted_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel++];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_ecs[pv]; break;
		case 0x10: ham_lastcolor &= 0xFF0; ham_lastcolor |= (pv & 0xF); break;
		case 0x20: ham_lastcolor &= 0x0FF; ham_lastcolor |= (pv & 0xF) << 8; break;
		case 0x30: ham_lastcolor &= 0xF0F; ham_lastcolor |= (pv & 0xF) << 4; break;
		}
	    }
	}
    }
}

static void decode_ham (int pix, int stoppos)
{
    int todraw_amiga = res_shift_from_window (stoppos - pix);
    if (!bplham || (bplplanecnt != 6 && bplplanecnt != 8))
	abort ();

    if (currprefs.chipset_mask & CSMASK_AGA) {
	if (bplplanecnt == 8) { /* AGA mode HAM8 */
	    while (todraw_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel];
		switch (pv & 0x3) {
		case 0x0: ham_lastcolor = colors_for_drawing.color_regs_aga[pv >> 2]; break;
		case 0x1: ham_lastcolor &= 0xFFFF03; ham_lastcolor |= (pv & 0xFC); break;
		case 0x2: ham_lastcolor &= 0x03FFFF; ham_lastcolor |= (pv & 0xFC) << 16; break;
		case 0x3: ham_lastcolor &= 0xFF03FF; ham_lastcolor |= (pv & 0xFC) << 8; break;
		}
		ham_linebuf[ham_decode_pixel++] = ham_lastcolor;
	    }
	} else if(bplplanecnt == 6) { /* AGA mode HAM6 */
	    while (todraw_amiga-- > 0) {
		int pv = pixdata.apixels[ham_decode_pixel];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_aga[pv]; break;
		case 0x10: ham_lastcolor &= 0xFFFF00; ham_lastcolor |= (pv & 0xF) << 4; break;
		case 0x20: ham_lastcolor &= 0x00FFFF; ham_lastcolor |= (pv & 0xF) << 20; break;
		case 0x30: ham_lastcolor &= 0xFF00FF; ham_lastcolor |= (pv & 0xF) << 12; break;
		}
		ham_linebuf[ham_decode_pixel++] = ham_lastcolor;
	    }
	}
    } else {
        if (bplplanecnt == 6) { /* OCS/ECS mode HAM6 */
	    while (todraw_amiga-- > 0) {
    		int pv = pixdata.apixels[ham_decode_pixel];
		switch (pv & 0x30) {
		case 0x00: ham_lastcolor = colors_for_drawing.color_regs_ecs[pv]; break;
		case 0x10: ham_lastcolor &= 0xFF0; ham_lastcolor |= (pv & 0xF); break;
		case 0x20: ham_lastcolor &= 0x0FF; ham_lastcolor |= (pv & 0xF) << 8; break;
		case 0x30: ham_lastcolor &= 0xF0F; ham_lastcolor |= (pv & 0xF) << 4; break;
		}
		ham_linebuf[ham_decode_pixel++] = ham_lastcolor;
	    }
	}
    }
}

static void gen_pfield_tables (void)
{
    int i;

    /* For now, the AGA stuff is broken in the dual playfield case. We encode
     * sprites in dpf mode by ORing the pixel value with 0x80. To make dual
     * playfield rendering easy, the lookup tables contain are made linear for
     * values >= 128. That only works for OCS/ECS, though. */

    for (i = 0; i < 256; i++) {
	int plane1 = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4) | ((i >> 3) & 8);
	int plane2 = ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4) | ((i >> 4) & 8);

	dblpf_2nd1[i] = plane1 == 0 ? (plane2 == 0 ? 0 : 2) : 1;
	dblpf_2nd2[i] = plane2 == 0 ? (plane1 == 0 ? 0 : 1) : 2;
	dblpf_ind1_aga[i] = plane1 == 0 ? plane2 : plane1;
	dblpf_ind2_aga[i] = plane2 == 0 ? plane1 : plane2;

	dblpf_ms1[i] = plane1 == 0 ? (plane2 == 0 ? 16 : 8) : 0;
	dblpf_ms2[i] = plane2 == 0 ? (plane1 == 0 ? 16 : 0) : 8;
	dblpf_ms[i] = i == 0 ? 16 : 8;
	if (plane2 > 0)
	    plane2 += 8;
	dblpf_ind1[i] = i >= 128 ? i & 0x7F : (plane1 == 0 ? plane2 : plane1);
	dblpf_ind2[i] = i >= 128 ? i & 0x7F : (plane2 == 0 ? plane1 : plane2);

	sprite_offs[i] = (i & 15) ? 0 : 2;

	clxtab[i] = ((((i & 3) && (i & 12)) << 9)
		     | (((i & 3) && (i & 48)) << 10)
		     | (((i & 3) && (i & 192)) << 11)
		     | (((i & 12) && (i & 48)) << 12)
		     | (((i & 12) && (i & 192)) << 13)
		     | (((i & 48) && (i & 192)) << 14));
    }
}

/* When looking at this function and the ones that inline it, bear in mind
   what an optimizing compiler will do with this code.  All callers of this
   function only pass in constant arguments (except for E).  This means
   that many of the if statements will go away completely after inlining.  */
STATIC_INLINE void draw_sprites_1 (struct sprite_entry *e, int ham, int dualpf, int shift, int has_attach)
{
    int *shift_lookup = dualpf ? (bpldualpfpri ? dblpf_ms2 : dblpf_ms1) : dblpf_ms;
    uae_u16 *buf = spixels + e->first_pixel;
    uae_u8 *stbuf = spixstate.bytes + e->first_pixel;
    int pos, last;

    buf -= e->pos;
    stbuf -= e->pos;

    for (pos = e->pos; pos < e->max; pos++) {
	int maskshift, plfmask;
	unsigned int v = buf[pos];

	/* The value in the shift lookup table is _half_ the shift count we
	   need.  This is because we can't shift 32 bits at once (undefined
	   behaviour in C).  */
	maskshift = shift_lookup[pixdata.apixels[(pos << shift) + pixels_offset]];
	plfmask = (plf_sprite_mask >> maskshift) >> maskshift;
	v &= ~plfmask;
	if (v != 0) {
	    unsigned int vlo, vhi, col, col_off;
	    unsigned int v1 = v & 255;
	    /* OFFS determines the sprite pair with the highest priority that has
	       any bits set.  E.g. if we have 0xFF00 in the buffer, we have sprite
	       pairs 01 and 23 cleared, and pairs 45 and 67 set, so OFFS will
	       have a value of 4.
	       2 * OFFS is the bit number in V of the sprite pair, and it also
	       happens to be the color offset for that pair.  */
	    int offs;
	    if (v1 == 0)
		offs = 4 + sprite_offs[v >> 8];
	    else
		offs = sprite_offs[v1];

	    /* Shift highest priority sprite pair down to bit zero.  */
	    v >>= offs * 2;
	    v &= 15;
 
	    if (has_attach && (stbuf[pos] & (1 << offs))) {
		col = v;
	    } else {
		/* This sequence computes the correct color value.  We have to select
		   either the lower-numbered or the higher-numbered sprite in the pair.
		   We have to select the high one if the low one has all bits zero.
		   If the lower-numbered sprite has any bits nonzero, (VLO - 1) is in
		   the range of 0..2, and with the mask and shift, VHI will be zero.
		   If the lower-numbered sprite is zero, (VLO - 1) is a mask of
		   0xFFFFFFFF, and we select the bits of the higher numbered sprite
		   in VHI.
		   This is _probably_ more efficient than doing it with branches.  */
		vlo = v & 3;
		vhi = (v & (vlo - 1)) >> 2;
		col = vlo | vhi;
		col += (offs * 2);
	    }
	    col += dualpf ? 128 + 16 : 16;
	    if (ham) {
		col = color_reg_get (&colors_for_drawing, col);
		ham_linebuf[(pos << shift) + pixels_offset] = col;
		if (shift == 1) {
		    ham_linebuf[(pos << shift) + pixels_offset + 1] = col;
		}
	    } else {
		if (shift == 1)
		    pixdata.apixels_w[pos + (pixels_offset >> 1)] = col | (col << 8);
		else
		    pixdata.apixels[(pos << shift) + pixels_offset] = col;
	    }
	}
    }
}
  
/* See comments above.  Do not touch if you don't know what's going on.
 * (We do _not_ want the following to be inlined themselves).  */
static void draw_sprites_normal_sp_lo_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 0, 0); }
static void draw_sprites_normal_dp_lo_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 0, 0); }
static void draw_sprites_ham_sp_lo_nat (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 0, 0); }
static void draw_sprites_normal_sp_hi_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 1, 0); }
static void draw_sprites_normal_dp_hi_nat (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 1, 0); }
static void draw_sprites_ham_sp_hi_nat (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 1, 0); }
static void draw_sprites_normal_sp_lo_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 0, 1); }
static void draw_sprites_normal_dp_lo_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 0, 1); }
static void draw_sprites_ham_sp_lo_at (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 0, 1); }
static void draw_sprites_normal_sp_hi_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 0, 1, 1); }
static void draw_sprites_normal_dp_hi_at (struct sprite_entry *e) { draw_sprites_1 (e, 0, 1, 1, 1); }
static void draw_sprites_ham_sp_hi_at (struct sprite_entry *e) { draw_sprites_1 (e, 1, 0, 1, 1); }

STATIC_INLINE void draw_sprites (struct sprite_entry *e)
{
    if (e->has_attached)
	if (bplres == 1)
	    if (bplham)
		draw_sprites_ham_sp_hi_at (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_hi_at (e);
		else
		    draw_sprites_normal_sp_hi_at (e);
	else
	    if (bplham)
		draw_sprites_ham_sp_lo_at (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_lo_at (e);
		else
		    draw_sprites_normal_sp_lo_at (e);
    else
	if (bplres == 1)
	    if (bplham)
		draw_sprites_ham_sp_hi_nat (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_hi_nat (e);
		else
		    draw_sprites_normal_sp_hi_nat (e);
	else
	    if (bplham)
		draw_sprites_ham_sp_lo_nat (e);
	    else
		if (bpldualpf)
		    draw_sprites_normal_dp_lo_nat (e);
		else
		    draw_sprites_normal_sp_lo_nat (e);
}

#define MERGE(a,b,mask,shift) do {\
    uae_u32 tmp = mask & (a ^ (b >> shift)); \
    a ^= tmp; \
    b ^= (tmp << shift); \
} while (0)

#define GETLONG(P) (*(uae_u32 *)P)

/* We use the compiler's inlining ability to ensure that PLANES is in effect a compile time
   constant.  That will cause some unnecessary code to be optimized away.
   Don't touch this if you don't know what you are doing.  */
STATIC_INLINE void pfield_doline_1 (uae_u32 *pixels, int wordcount, int planes)
{
    while (wordcount-- > 0) {
	uae_u32 b0, b1, b2, b3, b4, b5, b6, b7;

	b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, b7 = 0;
	switch (planes) {
	case 8: b0 = GETLONG ((uae_u32 *)real_bplpt[7]); real_bplpt[7] += 4;
	case 7: b1 = GETLONG ((uae_u32 *)real_bplpt[6]); real_bplpt[6] += 4;
	case 6: b2 = GETLONG ((uae_u32 *)real_bplpt[5]); real_bplpt[5] += 4;
	case 5: b3 = GETLONG ((uae_u32 *)real_bplpt[4]); real_bplpt[4] += 4;
	case 4: b4 = GETLONG ((uae_u32 *)real_bplpt[3]); real_bplpt[3] += 4;
	case 3: b5 = GETLONG ((uae_u32 *)real_bplpt[2]); real_bplpt[2] += 4;
	case 2: b6 = GETLONG ((uae_u32 *)real_bplpt[1]); real_bplpt[1] += 4;
	case 1: b7 = GETLONG ((uae_u32 *)real_bplpt[0]); real_bplpt[0] += 4;
	}

	MERGE (b0, b1, 0x55555555, 1);
	MERGE (b2, b3, 0x55555555, 1);
	MERGE (b4, b5, 0x55555555, 1);
	MERGE (b6, b7, 0x55555555, 1);

	MERGE (b0, b2, 0x33333333, 2);
	MERGE (b1, b3, 0x33333333, 2);
	MERGE (b4, b6, 0x33333333, 2);
	MERGE (b5, b7, 0x33333333, 2);

	MERGE (b0, b4, 0x0f0f0f0f, 4);
	MERGE (b1, b5, 0x0f0f0f0f, 4);
	MERGE (b2, b6, 0x0f0f0f0f, 4);
	MERGE (b3, b7, 0x0f0f0f0f, 4);

	MERGE (b0, b1, 0x00ff00ff, 8);
	MERGE (b2, b3, 0x00ff00ff, 8);
	MERGE (b4, b5, 0x00ff00ff, 8);
	MERGE (b6, b7, 0x00ff00ff, 8);

	MERGE (b0, b2, 0x0000ffff, 16);
	do_put_mem_long (pixels, b0);
	do_put_mem_long (pixels + 4, b2);
	MERGE (b1, b3, 0x0000ffff, 16);
	do_put_mem_long (pixels + 2, b1);
	do_put_mem_long (pixels + 6, b3);
	MERGE (b4, b6, 0x0000ffff, 16);
	do_put_mem_long (pixels + 1, b4);
	do_put_mem_long (pixels + 5, b6);
	MERGE (b5, b7, 0x0000ffff, 16);
	do_put_mem_long (pixels + 3, b5);
	do_put_mem_long (pixels + 7, b7);
	pixels += 8;
    }
}

/* See above for comments on inlining.  These functions should _not_
   be inlined themselves.  */
static void pfield_doline_n1 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 1); }
static void pfield_doline_n2 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 2); }
static void pfield_doline_n3 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 3); }
static void pfield_doline_n4 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 4); }
static void pfield_doline_n5 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 5); }
static void pfield_doline_n6 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 6); }
static void pfield_doline_n7 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 7); }
static void pfield_doline_n8 (uae_u32 *data, int count) { pfield_doline_1 (data, count, 8); }

static void pfield_doline (int lineno)
{
    int wordcount = dp_for_drawing->plflinelen;
    uae_u32 *data = pixdata.apixels_l + MAX_PIXELS_PER_LINE/4;

#ifdef SMART_UPDATE
#define DATA_POINTER(n) (line_data[lineno] + (n)*MAX_WORDS_PER_LINE*2)
    real_bplpt[0] = DATA_POINTER (0);
    real_bplpt[1] = DATA_POINTER (1);
    real_bplpt[2] = DATA_POINTER (2);
    real_bplpt[3] = DATA_POINTER (3);
    real_bplpt[4] = DATA_POINTER (4);
    real_bplpt[5] = DATA_POINTER (5);
    real_bplpt[6] = DATA_POINTER (6);
    real_bplpt[7] = DATA_POINTER (7);
#endif

    switch (bplplanecnt) {
    default: break;
    case 0: memset (data, 0, wordcount * 32); break;
    case 1: pfield_doline_n1 (data, wordcount); break;
    case 2: pfield_doline_n2 (data, wordcount); break;
    case 3: pfield_doline_n3 (data, wordcount); break;
    case 4: pfield_doline_n4 (data, wordcount); break;
    case 5: pfield_doline_n5 (data, wordcount); break;
    case 6: pfield_doline_n6 (data, wordcount); break;
    case 7: pfield_doline_n7 (data, wordcount); break;
    case 8: pfield_doline_n8 (data, wordcount); break;
    }
}

void init_row_map (void)
{
    int i;
    if (gfxvidinfo.height > 2048) {
	write_log ("Resolution too high, aborting\n");
	abort ();
    }
    for (i = 0; i < gfxvidinfo.height + 1; i++)
	row_map[i] = gfxvidinfo.bufmem + gfxvidinfo.rowbytes * i;
}

static void init_aspect_maps (void)
{
    int i, maxl;
    double native_lines_per_amiga_line;

    if (native2amiga_line_map)
	free (native2amiga_line_map);
    if (amiga2aspect_line_map)
	free (amiga2aspect_line_map);

    /* At least for this array the +1 is necessary. */
    amiga2aspect_line_map = (int *)malloc (sizeof (int) * (MAXVPOS + 1)*2 + 1);
    native2amiga_line_map = (int *)malloc (sizeof (int) * gfxvidinfo.height);

    if (currprefs.gfx_correct_aspect)
	native_lines_per_amiga_line = ((double)gfxvidinfo.height
				       * (currprefs.gfx_lores ? 320 : 640)
				       / (currprefs.gfx_linedbl ? 512 : 256)
				       / gfxvidinfo.width);
    else
	native_lines_per_amiga_line = 1;

    maxl = (MAXVPOS + 1) * (currprefs.gfx_linedbl ? 2 : 1);
    min_ypos_for_screen = minfirstline << (currprefs.gfx_linedbl ? 1 : 0);
    max_drawn_amiga_line = -1;
    for (i = 0; i < maxl; i++) {
	int v = (int) ((i - min_ypos_for_screen) * native_lines_per_amiga_line);
	if (v >= gfxvidinfo.height && max_drawn_amiga_line == -1)
	    max_drawn_amiga_line = i - min_ypos_for_screen;
	if (i < min_ypos_for_screen || v >= gfxvidinfo.height)
	    v = -1;
	amiga2aspect_line_map[i] = v;
    }
    if (currprefs.gfx_linedbl)
	max_drawn_amiga_line >>= 1;

    if (currprefs.gfx_ycenter && !(currprefs.gfx_correct_aspect)) {
	/* @@@ verify maxvpos vs. MAXVPOS */
	extra_y_adjust = (gfxvidinfo.height - (maxvpos << (currprefs.gfx_linedbl ? 1 : 0))) >> 1;
	if (extra_y_adjust < 0)
	    extra_y_adjust = 0;
    }

    for (i = 0; i < gfxvidinfo.height; i++)
	native2amiga_line_map[i] = -1;

    if (native_lines_per_amiga_line < 1) {
	/* Must omit drawing some lines. */
	for (i = maxl - 1; i > min_ypos_for_screen; i--) {
	    if (amiga2aspect_line_map[i] == amiga2aspect_line_map[i-1]) {
		if (currprefs.gfx_linedbl && (i & 1) == 0 && amiga2aspect_line_map[i+1] != -1) {
		    /* If only the first line of a line pair would be omitted,
		     * omit the second one instead to avoid problems with line
		     * doubling. */
		    amiga2aspect_line_map[i] = amiga2aspect_line_map[i+1];
		    amiga2aspect_line_map[i+1] = -1;
		} else
		    amiga2aspect_line_map[i] = -1;
	    }
	}
    }

    for (i = maxl-1; i >= min_ypos_for_screen; i--) {
	int j;
	if (amiga2aspect_line_map[i] == -1)
	    continue;
	for (j = amiga2aspect_line_map[i]; j < gfxvidinfo.height && native2amiga_line_map[j] == -1; j++)
	    native2amiga_line_map[j] = i >> (currprefs.gfx_linedbl ? 1 : 0);
    }
}

/*
 * A raster line has been built in the graphics buffer. Tell the graphics code
 * to do anything necessary to display it.
 */
static void do_flush_line_1 (int lineno)
{
    if (lineno < first_drawn_line)
	first_drawn_line = lineno;
    if (lineno > last_drawn_line)
	last_drawn_line = lineno;

    if (gfxvidinfo.maxblocklines == 0)
	flush_line (lineno);
    else {
	if ((last_block_line+1) != lineno) {
	    if (first_block_line != -2)
		flush_block (first_block_line, last_block_line);
	    first_block_line = lineno;
	}
	last_block_line = lineno;
	if (last_block_line - first_block_line >= gfxvidinfo.maxblocklines) {
	    flush_block (first_block_line, last_block_line);
	    first_block_line = last_block_line = -2;
	}
    }
}

STATIC_INLINE void do_flush_line (int lineno)
{
    do_flush_line_1 (lineno);
}

/*
 * One drawing frame has been finished. Tell the graphics code about it.
 * Note that the actual flush_screen() call is a no-op for all reasonable
 * systems.
 */

STATIC_INLINE void do_flush_screen (int start, int stop)
{
    /* TODO: this flush operation is executed outside locked state!
       Should be corrected.
       (sjo 26.9.99) */

    if (gfxvidinfo.maxblocklines != 0 && first_block_line != -2) {
	flush_block (first_block_line, last_block_line);
    }
    if (start <= stop)
	flush_screen (start, stop);
}

static int drawing_color_matches;
static enum { color_match_acolors, color_match_full } color_match_type;

/* Set up colors_for_drawing to the state at the beginning of the currently drawn
   line.  Try to avoid copying color tables around whenever possible.  */
static void adjust_drawing_colors (int ctable, int bplham)
{
    if (drawing_color_matches != ctable) {
	if (bplham) {
	    color_reg_cpy (&colors_for_drawing, curr_color_tables + ctable);
	    color_match_type = color_match_full;
	} else {
	    memcpy (colors_for_drawing.acolors, curr_color_tables[ctable].acolors,
	        sizeof colors_for_drawing.acolors);
	    color_match_type = color_match_acolors;
	}
	drawing_color_matches = ctable;
    } else if (bplham && color_match_type != color_match_full) {
	color_reg_cpy (&colors_for_drawing, &curr_color_tables[ctable]);
	color_match_type = color_match_full;
    }
}

STATIC_INLINE void do_color_changes (line_draw_func worker_border, line_draw_func worker_pfield)
{
    int i;
    int lastpos = visible_left_border;

    for (i = dip_for_drawing->first_color_change; i <= dip_for_drawing->last_color_change; i++) {
	int regno = curr_color_changes[i].regno;
	unsigned int value = curr_color_changes[i].value;
	int nextpos, nextpos_in_range;
	if (i == dip_for_drawing->last_color_change)
	    nextpos = max_diwlastword;
	else
	    nextpos = coord_hw_to_window_x (curr_color_changes[i].linepos * 2);

	nextpos_in_range = nextpos;
	if (nextpos > visible_right_border)
	    nextpos_in_range = visible_right_border;

	if (nextpos_in_range > lastpos) {
	    if (lastpos < playfield_start) {
		int t = nextpos_in_range <= playfield_start ? nextpos_in_range : playfield_start;
		(*worker_border) (lastpos, t);
		lastpos = t;
	    }
	}
	if (nextpos_in_range > lastpos) {
	    if (lastpos >= playfield_start && lastpos < playfield_end) {
		int t = nextpos_in_range <= playfield_end ? nextpos_in_range : playfield_end;
		(*worker_pfield) (lastpos, t);
		lastpos = t;
	    }
	}
	if (nextpos_in_range > lastpos) {
	    if (lastpos >= playfield_end)
		(*worker_border) (lastpos, nextpos_in_range);
	    lastpos = nextpos_in_range;
	}
	if (i != dip_for_drawing->last_color_change) {
	    color_reg_set (&colors_for_drawing, regno, value);
	    colors_for_drawing.acolors[regno] = getxcolor (value);
	}
	if (lastpos >= visible_right_border)
	    break;
    }
}

/* We only save hardware registers during the hardware frame. Now, when
 * drawing the frame, we expand the data into a slightly more useful
 * form. */
static void pfield_expand_dp_bplcon (void)
{
    int plf1pri, plf2pri;
    bplres = dp_for_drawing->bplres;
    bplplanecnt = dp_for_drawing->nr_planes;
    bplham = (dp_for_drawing->bplcon0 & 0x800) == 0x800;
    if (currprefs.chipset_mask & CSMASK_AGA) {
	/* The KILLEHB bit exists in ECS, but is apparently meant for Genlock
	 * stuff, and it's set by some demos (e.g. Andromeda Seven Seas) */
	bplehb = ((dp_for_drawing->bplcon0 & 0xFCC0) == 0x6000 && !(dp_for_drawing->bplcon2 & 0x200));
    } else {
	bplehb = (dp_for_drawing->bplcon0 & 0xFC00) == 0x6000;
    }
    plf1pri = dp_for_drawing->bplcon2 & 7;
    plf2pri = (dp_for_drawing->bplcon2 >> 3) & 7;
    plf_sprite_mask = 0xFFFF0000 << (4 * plf2pri);
    plf_sprite_mask |= (0xFFFF << (4 * plf1pri)) & 0xFFFF;
    bpldualpf = (dp_for_drawing->bplcon0 & 0x400) == 0x400;
    bpldualpfpri = (dp_for_drawing->bplcon2 & 0x40) == 0x40;
    bpldualpf2of = (dp_for_drawing->bplcon3 >> 10) & 7;
    sbasecol[0] = ((dp_for_drawing->bplcon4 >> 4) & 15) << 4;
    sbasecol[1] = ((dp_for_drawing->bplcon4 >> 0) & 15) << 4;
}

enum double_how {
    dh_buf,
    dh_line,
    dh_emerg
};

STATIC_INLINE void pfield_draw_line (int lineno, int gfx_ypos, int follow_ypos)
{
    static int warned = 0;
    int border = 0;
    int do_double = 0;
    enum double_how dh;

    dp_for_drawing = line_decisions + lineno;
    dip_for_drawing = curr_drawinfo + lineno;
    switch (linestate[lineno]) {
    case LINE_REMEMBERED_AS_PREVIOUS:
	if (!warned)
	    write_log ("Shouldn't get here... this is a bug.\n"), warned++;
	return;

    case LINE_BLACK:
	linestate[lineno] = LINE_REMEMBERED_AS_BLACK;
	border = 2;
	break;

    case LINE_REMEMBERED_AS_BLACK:
	return;

    case LINE_AS_PREVIOUS:
	dp_for_drawing--;
	dip_for_drawing--;
	if (dp_for_drawing->plfleft == -1)
	    border = 1;
	linestate[lineno] = LINE_DONE_AS_PREVIOUS;
	break;

    case LINE_DONE_AS_PREVIOUS:
	/* fall through */
    case LINE_DONE:
	return;

    case LINE_DECIDED_DOUBLE:
	if (follow_ypos != -1) {
	    do_double = 1;
	    linetoscr_double_offset = gfxvidinfo.rowbytes * (follow_ypos - gfx_ypos);
	    linestate[lineno + 1] = LINE_DONE_AS_PREVIOUS;
	}

	/* fall through */
    default:
	if (dp_for_drawing->plfleft == -1)
	    border = 1;
	linestate[lineno] = LINE_DONE;
	break;
    }

    dh = dh_line;
    xlinebuffer = gfxvidinfo.linemem;
    if (xlinebuffer == 0 && do_double
	&& (border == 0 || (border != 1 && dip_for_drawing->nr_color_changes > 0)))
	xlinebuffer = gfxvidinfo.emergmem, dh = dh_emerg;
    if (xlinebuffer == 0)
	xlinebuffer = row_map[gfx_ypos], dh = dh_buf;
    xlinebuffer -= linetoscr_x_adjust_bytes;

    if (border == 0) {
	pfield_expand_dp_bplcon ();

	if (bplres == RES_LORES && !currprefs.gfx_lores)
	    currprefs.gfx_lores = 2;
	pfield_init_linetoscr ();

	pfield_doline (lineno);

	adjust_drawing_colors (dp_for_drawing->ctable, bplham || bplehb);

	/* The problem is that we must call decode_ham() BEFORE we do the
	   sprites. */
	if (! border && bplham) {
	    init_ham_decoding ();
	    if (dip_for_drawing->nr_color_changes == 0) {
		/* The easy case: need to do HAM decoding only once for the
		 * full line. */
		decode_ham (visible_left_border, visible_right_border);
	    } else /* Argh. */ {
		do_color_changes (dummy_worker, decode_ham);
		adjust_drawing_colors (dp_for_drawing->ctable, bplham || bplehb);
	    }
	}

	{
	    int i;
	    for (i = 0; i < dip_for_drawing->nr_sprites; i++)
		draw_sprites (curr_sprite_entries + dip_for_drawing->first_sprite_entry + i);
	}

	do_color_changes (pfield_do_fill_line, pfield_do_linetoscr);

	if (dh == dh_emerg)
	    memcpy (row_map[gfx_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.rowbytes);

	do_flush_line (gfx_ypos);
	if (do_double) {
	    if (dh == dh_emerg)
		memcpy (row_map[follow_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.rowbytes);
	    else if (dh == dh_buf)
		memcpy (row_map[follow_ypos], row_map[gfx_ypos], gfxvidinfo.rowbytes);
	    do_flush_line (follow_ypos);
	}
	if (currprefs.gfx_lores == 2)
	    currprefs.gfx_lores = 0;
    } else if (border == 1) {
	adjust_drawing_colors (dp_for_drawing->ctable, 0);

	if (dip_for_drawing->nr_color_changes == 0) {
	    fill_line ();
	    do_flush_line (gfx_ypos);
#if 0
	    if (dh == dh_emerg)
		abort ();
#endif
	    if (do_double) {
		if (dh == dh_buf) {
		    xlinebuffer = row_map[follow_ypos] - linetoscr_x_adjust_bytes;
		    fill_line ();
		}
		/* If dh == dh_line, do_flush_line will re-use the rendered line
		 * from linemem.  */
		do_flush_line (follow_ypos);
	    }
	    return;
	}

	playfield_start = visible_right_border;
	playfield_end = visible_right_border;

	do_color_changes (pfield_do_fill_line, pfield_do_fill_line);

	if (dh == dh_emerg)
	    memcpy (row_map[gfx_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.rowbytes);

	do_flush_line (gfx_ypos);
	if (do_double) {
	    if (dh == dh_emerg)
		memcpy (row_map[follow_ypos], xlinebuffer + linetoscr_x_adjust_bytes, gfxvidinfo.rowbytes);
	    else if (dh == dh_buf)
		memcpy (row_map[follow_ypos], row_map[gfx_ypos], gfxvidinfo.rowbytes);
	    /* If dh == dh_line, do_flush_line will re-use the rendered line
	     * from linemem.  */
	    do_flush_line (follow_ypos);
	}
    } else {
	xcolnr tmp = colors_for_drawing.acolors[0];
	colors_for_drawing.acolors[0] = getxcolor (0);
	fill_line ();
	do_flush_line (gfx_ypos);
	colors_for_drawing.acolors[0] = tmp;
    }
}

static void center_image (void)
{
    prev_x_adjust = visible_left_border;
    prev_y_adjust = thisframe_y_adjust;

    if (currprefs.gfx_xcenter) {
	if (max_diwstop - min_diwstart < gfxvidinfo.width && currprefs.gfx_xcenter == 2)
	    /* Try to center. */
	    visible_left_border = ((max_diwstop - min_diwstart - gfxvidinfo.width) / 2 + min_diwstart) & ~1;
	else
	    visible_left_border = max_diwstop - gfxvidinfo.width;

	/* Would the old value be good enough? If so, leave it as it is if we want to
	 * be clever. */
	if (currprefs.gfx_xcenter == 2) {
	    if (visible_left_border < prev_x_adjust && prev_x_adjust < min_diwstart)
		visible_left_border = prev_x_adjust;
	}
    } else
	visible_left_border = max_diwlastword - gfxvidinfo.width;
    if (visible_left_border < 0)
	visible_left_border = 0;

    linetoscr_x_adjust_bytes = visible_left_border * gfxvidinfo.pixbytes;

    visible_right_border = visible_left_border + gfxvidinfo.width;
    if (visible_right_border > max_diwlastword)
	visible_right_border = max_diwlastword;

    thisframe_y_adjust = minfirstline;
    if (currprefs.gfx_ycenter && thisframe_first_drawn_line != -1) {
	if (thisframe_last_drawn_line - thisframe_first_drawn_line < max_drawn_amiga_line && currprefs.gfx_ycenter == 2)
	    thisframe_y_adjust = (thisframe_last_drawn_line - thisframe_first_drawn_line - max_drawn_amiga_line) / 2 + thisframe_first_drawn_line;
	else
	    thisframe_y_adjust = thisframe_first_drawn_line;
	/* Would the old value be good enough? If so, leave it as it is if we want to
	 * be clever. */
	if (currprefs.gfx_ycenter == 2) {
	    if (thisframe_y_adjust != prev_y_adjust
		&& prev_y_adjust <= thisframe_first_drawn_line
		&& prev_y_adjust + max_drawn_amiga_line > thisframe_last_drawn_line)
		thisframe_y_adjust = prev_y_adjust;
	}
	/* Make sure the value makes sense */
	if (thisframe_y_adjust + max_drawn_amiga_line > maxvpos)
	    thisframe_y_adjust = maxvpos - max_drawn_amiga_line;
	if (thisframe_y_adjust < minfirstline)
	    thisframe_y_adjust = minfirstline;
    }
    thisframe_y_adjust_real = thisframe_y_adjust << (currprefs.gfx_linedbl ? 1 : 0);
    max_ypos_thisframe = (maxvpos - thisframe_y_adjust) << (currprefs.gfx_linedbl ? 1 : 0);

    /* @@@ interlace_seen used to be (bplcon0 & 4), but this is probably
     * better.  */
    if (prev_x_adjust != visible_left_border || prev_y_adjust != thisframe_y_adjust)
	frame_redraw_necessary |= interlace_seen && currprefs.gfx_linedbl ? 2 : 1;

    max_diwstop = 0;
    min_diwstart = 10000;
}

static void init_drawing_frame (void)
{
    int i, maxline;

    init_hardware_for_drawing_frame ();

    if (thisframe_first_drawn_line == -1)
	thisframe_first_drawn_line = minfirstline;
    if (thisframe_first_drawn_line > thisframe_last_drawn_line)
	thisframe_last_drawn_line = thisframe_first_drawn_line;

    maxline = currprefs.gfx_linedbl ? (maxvpos+1) * 2 + 1 : (maxvpos+1) + 1;
#ifdef SMART_UPDATE
    for (i = 0; i < maxline; i++) {
	switch (linestate[i]) {
	 case LINE_DONE_AS_PREVIOUS:
	    linestate[i] = LINE_REMEMBERED_AS_PREVIOUS;
	    break;
	 case LINE_REMEMBERED_AS_BLACK:
	    break;
	 default:
	    linestate[i] = LINE_UNDECIDED;
	    break;
	}
    }
#else
    memset (linestate, LINE_UNDECIDED, maxline);
#endif
    last_drawn_line = 0;
    first_drawn_line = 32767;

    first_block_line = last_block_line = -2;
    if (currprefs.test_drawing_speed)
	frame_redraw_necessary = 1;
    else if (frame_redraw_necessary)
	frame_redraw_necessary--;

    center_image ();

    thisframe_first_drawn_line = -1;
    thisframe_last_drawn_line = -1;

    drawing_color_matches = -1;
}

void finish_drawing_frame (void)
{
    int i;

#ifndef SMART_UPDATE
    /* @@@ This isn't exactly right yet. FIXME */
    if (!interlace_seen) {
	do_flush_screen (first_drawn_line, last_drawn_line);
	return;
    }
#endif
    if (! lockscr ()) {
	notice_screen_contents_lost ();
	return;
    }
    for (i = 0; i < max_ypos_thisframe; i++) {
	int where;
	int line = i + thisframe_y_adjust_real;

	if (linestate[line] == LINE_UNDECIDED)
	    break;

	where = amiga2aspect_line_map[i+min_ypos_for_screen];
	if (where >= gfxvidinfo.height)
	    break;
	if (where == -1)
	    continue;

	pfield_draw_line (line, where, amiga2aspect_line_map[i+min_ypos_for_screen+1]);
    }
    unlockscr ();
    do_flush_screen (first_drawn_line, last_drawn_line);
}

void hardware_line_completed (int lineno)
{
#ifndef SMART_UPDATE
    {
	int i, where;
	/* l is the line that has been finished for drawing. */
	i = lineno - thisframe_y_adjust_real;
	if (i >= 0 && i < max_ypos_thisframe) {
	    where = amiga2aspect_line_map[i+min_ypos_for_screen];
	    if (where < gfxvidinfo.height && where != -1)
		pfield_draw_line (lineno, where, amiga2aspect_line_map[i+min_ypos_for_screen+1]);
	}
    }
#endif
}

STATIC_INLINE void check_picasso (void)
{
#ifdef PICASSO96
    if (picasso_on && picasso_redraw_necessary)
	picasso_refresh ();
    picasso_redraw_necessary = 0;

    if (picasso_requested_on == picasso_on)
	return;

    picasso_on = picasso_requested_on;

    if (!picasso_on)
	clear_inhibit_frame (IHF_PICASSO);
    else
	set_inhibit_frame (IHF_PICASSO);

    gfx_set_picasso_state (picasso_on);
    picasso_enablescreen (picasso_requested_on);

    notice_screen_contents_lost ();
    notice_new_xcolors ();
#endif
}

void vsync_handle_redraw (int long_frame, int lof_changed)
{
    last_redraw_point++;
    if (lof_changed || ! interlace_seen || last_redraw_point >= 2 || long_frame) {
	last_redraw_point = 0;
	interlace_seen = 0;

	if (framecnt == 0)
	    finish_drawing_frame ();

	/* At this point, we have finished both the hardware and the
	 * drawing frame. Essentially, we are outside of all loops and
	 * can do some things which would cause confusion if they were
	 * done at other times.
	 */

	if (quit_program < 0) {
	    quit_program = -quit_program;
	    set_inhibit_frame (IHF_QUIT_PROGRAM);
	    set_special (SPCFLAG_BRK);
	    filesys_prepare_reset ();
	    return;
	}

	count_frame ();
	check_picasso ();

	check_prefs_changed_audio ();
	check_prefs_changed_custom ();
	check_prefs_changed_cpu ();
	if (check_prefs_changed_gfx ()) {
	    init_row_map ();
	    init_aspect_maps ();
	    notice_screen_contents_lost ();
	    notice_new_xcolors ();
	}

	if (inhibit_frame != 0)
	    framecnt = 1;

	if (framecnt == 0)
	    init_drawing_frame ();
    }
}

void hsync_record_line_state (int lineno, enum nln_how how, int changed)
{
    char *state;
    if (framecnt != 0)
	return;

    state = linestate + lineno;
    changed += frame_redraw_necessary;

    switch (how) {
     case nln_normal:
	*state = changed ? LINE_DECIDED : LINE_DONE;
	break;
     case nln_doubled:
	*state = changed ? LINE_DECIDED_DOUBLE : LINE_DONE;
	changed += state[1] != LINE_REMEMBERED_AS_PREVIOUS;
	state[1] = changed ? LINE_AS_PREVIOUS : LINE_DONE_AS_PREVIOUS;
	break;
     case nln_nblack:
	*state = changed ? LINE_DECIDED : LINE_DONE;
	if (state[1] != LINE_REMEMBERED_AS_BLACK)
	    state[1] = LINE_BLACK;
	break;
     case nln_lower:
	if (state[-1] == LINE_UNDECIDED)
	    state[-1] = LINE_BLACK;
	*state = changed ? LINE_DECIDED : LINE_DONE;
	break;
     case nln_upper:
	*state = changed ? LINE_DECIDED : LINE_DONE;
	if (state[1] == LINE_UNDECIDED
	    || state[1] == LINE_REMEMBERED_AS_PREVIOUS
	    || state[1] == LINE_AS_PREVIOUS)
	    state[1] = LINE_BLACK;
	break;
    }
}

void notice_interlace_seen (void)
{
    interlace_seen = 1;
}

void reset_drawing (void)
{
    int i;

    inhibit_frame = 0;
    uae_sem_init (&gui_sem, 0, 1);

#ifdef PICASSO96
    InitPicasso96 ();
    picasso_on = 0;
    picasso_requested_on = 0;
    gfx_set_picasso_state (0);
#endif
    max_diwstop = 0;

    lores_factor = currprefs.gfx_lores ? 1 : 2;
    lores_shift = currprefs.gfx_lores ? 0 : 1;

    /*memset(blitcount, 0, sizeof(blitcount));  blitter debug */
    for (i = 0; i < sizeof linestate / sizeof *linestate; i++)
	linestate[i] = LINE_UNDECIDED;

    xlinebuffer = gfxvidinfo.bufmem;

    init_aspect_maps ();

    if (line_drawn == 0)
	line_drawn = (char *)malloc (gfxvidinfo.height);

    init_row_map();

    last_redraw_point = 0;

    memset (spixels, 0, sizeof spixels);
    memset (&spixstate, 0, sizeof spixstate);

    init_drawing_frame ();
}

void drawing_init ()
{
    native2amiga_line_map = 0;
    amiga2aspect_line_map = 0;
    line_drawn = 0;

    gen_pfield_tables();
}

