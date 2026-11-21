// karabas: a copy of src/hexen/r_draw.c of chocolate-doom 2.3.0 with faster R_DrawColumn and
// R_DrawSpan (docs/PLAN.md, Hexen Ф0); built instead of the upstream file (src/fw Makefile, HXDRAW=kg).
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 1993-2008 Raven Software
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//


#include "h2def.h"
#include "i_system.h"
#include "i_video.h"
#include "r_local.h"
#include "v_video.h"

/*

All drawing to the view buffer is accomplished in this file.  The other refresh
files only know about ccordinates, not the architecture of the frame buffer.

*/

byte *viewimage;
int viewwidth, scaledviewwidth, viewheight, viewwindowx, viewwindowy;
byte *ylookup[MAXHEIGHT];
int columnofs[MAXWIDTH];
//byte translations[3][256]; // color tables for different players

/*
==================
=
= R_DrawColumn
=
= Source is the top of the column to scale
=
==================
*/

lighttable_t *dc_colormap;
int dc_x;
int dc_yl;
int dc_yh;
fixed_t dc_iscale;
fixed_t dc_texturemid;
byte *dc_source;                // first pixel in a column (possibly virtual)

int dccount;                    // just for profiling

#ifdef KG_HWDRAW
// karabas (Ф1в): the drawer of the FPGA (doom_draw.v, src/fw/mmio.h) takes the plain columns and the
// spans: a command of 4 MMIO writes, drawn while the CPU goes on. Its colormap index is the offset in
// colormaps (COLORMAP / FOGMAP, 34 tables of 256): other colormaps (none known) and sources out of the
// SDRAM stay with the CPU. colormaps is one buffer reloaded per level: its sum is checked once per
// frame (kg_drw_newframe, kg_htic.c) and the copy of the drawer uploaded when it changed (the
// drawer idle first). The TL / translated columns, the sky and the rest stay with the CPU: its SRAM
// accesses wait for the drawer in the FPGA.
#include "mmio.h"
extern int kg_hwdraw, kg_drw_newframe;
#define KG_CMAPS_BYTES (34 * 256)        // summed: COLORMAP / FOGMAP
#define KG_DRW_CMAPS_BYTES (32 * 256)    // in the drawer: tables 0..31 (32, 33: invulnerability, CPU)

static void kg_drw_sync(void)
{
    static uint32_t sum_loaded;
    static int loaded;
    const uint32_t *w = (const uint32_t *) colormaps;
    uint32_t sum = 0;
    int i;

    kg_drw_newframe = 0;
    for (i = 0; i < KG_CMAPS_BYTES / 4; i++)
        sum = (sum << 1 | sum >> 31) + w[i];
    if (loaded && sum == sum_loaded)
        return;
    while (MMIO_DRW_STAT & 1) {}
    MMIO_DRW_CMADR = 0;
    for (i = 0; i < KG_DRW_CMAPS_BYTES / 4; i++)
        MMIO_DRW_CMAP = w[i];
    sum_loaded = sum;
    loaded = 1;
}

// the colormap index of a colormap pointer, -1: not in colormaps
static inline int kg_drw_cmap(const byte *cm)
{
    uint32_t d = (uint32_t) (cm - (const byte *) colormaps);
    return (d < KG_DRW_CMAPS_BYTES && (d & 255) == 0) ? (int) (d >> 8) : -1;
}

static inline int kg_drw_src(const byte *s)
{
    return (uint32_t) s - SDRAM_BASE < 0x02000000u;
}

// a post of R_DrawMaskedColumn (r_things.c) to the drawer, 0: not taken (the CPU draws it)
int kg_drw_cmidx(const byte *colormap)
{
    return kg_hwdraw ? kg_drw_cmap(colormap) : -1;
}
int kg_drw_col(int x, int yl, int yh, fixed_t texturemid, fixed_t iscale, int cm, const byte *source)
{
    if (!kg_drw_src(source))
        return 0;
    if (kg_drw_newframe)
        kg_drw_sync();
    MMIO_DRW0 = (uint32_t) (ylookup[yl] + columnofs[x] - I_VideoBuffer) | (uint32_t) (yh - yl) << 16 | (uint32_t) cm << 25;
    MMIO_DRW_FRAC = texturemid + (yl - centery) * iscale;
    MMIO_DRW_STEP = iscale;
    MMIO_DRW_COL = (uint32_t) source - SDRAM_BASE;
    return 1;
}

// a span of R_MapPlane (r_plane.c kg_planespans) to the drawer, 0: not taken (the CPU draws it)
int kg_drw_span(int y, int x1, int x2, fixed_t xfrac, fixed_t yfrac, fixed_t xstep, fixed_t ystep,
                const byte *colormap, const byte *source)
{
    int cm, count = x2 - x1;
    if (!kg_hwdraw)
        return 0;
    cm = kg_drw_cmap(colormap);
    if (cm < 0 || !kg_drw_src(source) || ((uint32_t) source & 3) != 0 || count < 0)
        return 0;
    if (kg_drw_newframe)
        kg_drw_sync();
    MMIO_DRW0 = (uint32_t) (ylookup[y] + columnofs[x1] - I_VideoBuffer) | (uint32_t) count << 16 | (uint32_t) cm << 25;
    MMIO_DRW_FRAC = ((xfrac << 10) & 0xffff0000) | ((yfrac >> 6) & 0x0000ffff);
    MMIO_DRW_STEP = ((xstep << 10) & 0xffff0000) | ((ystep >> 6) & 0x0000ffff);
    MMIO_DRW_SPAN = (uint32_t) source - SDRAM_BASE;
    return 1;
}
#endif

// karabas: the pointers in locals (the byte store to dest may alias the globals: gcc reloaded
// dc_source / dc_colormap at every pixel); same pixels as upstream. Unrolled by 2 it was slower
// (demo1 4K / 4K: more I$ misses ate the 0.06 M instructions per frame).
void R_DrawColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;
    const byte *source, *colormap;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

#ifdef KG_HWDRAW
    if (kg_hwdraw)
    {
        int cm = kg_drw_cmap(dc_colormap);
        if (cm >= 0 && kg_drw_src(dc_source))
        {
            if (kg_drw_newframe)
                kg_drw_sync();
            MMIO_DRW0 = (uint32_t) (ylookup[dc_yl] + columnofs[dc_x] - I_VideoBuffer)
                      | (uint32_t) count << 16 | (uint32_t) cm << 25;
            MMIO_DRW_FRAC = dc_texturemid + (dc_yl - centery) * dc_iscale;
            MMIO_DRW_STEP = dc_iscale;
            MMIO_DRW_COL = (uint32_t) dc_source - SDRAM_BASE;
            return;
        }
    }
#endif

    dest = ylookup[dc_yl] + columnofs[dc_x];
    source = dc_source;
    colormap = dc_colormap;

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    do
    {
        *dest = colormap[source[(frac >> FRACBITS) & 127]];
        dest += SCREENWIDTH;
        frac += fracstep;
    }
    while (count--);
}

void R_DrawColumnLow(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
//      dccount++;
#endif

    dest = ylookup[dc_yl] + columnofs[dc_x];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    do
    {
        *dest = dc_colormap[dc_source[(frac >> FRACBITS) & 127]];
        dest += SCREENWIDTH;
        frac += fracstep;
    }
    while (count--);
}

void R_DrawTLColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    if (!dc_yl)
        dc_yl = 1;
    if (dc_yh == viewheight - 1)
        dc_yh = viewheight - 2;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error("R_DrawTLColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[dc_x];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    // karabas: the pointers in locals (as R_DrawColumn)
    const byte *source = dc_source, *colormap = dc_colormap;
    const byte *tint = tinttable;
    do
    {
        *dest = tint[*dest +
                          (colormap[source[(frac >> FRACBITS) & 127]] <<
                           8)];
        dest += SCREENWIDTH;
        frac += fracstep;
    }
    while (count--);
}

//============================================================================
//
// R_DrawAltTLColumn
//
//============================================================================

void R_DrawAltTLColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    if (!dc_yl)
        dc_yl = 1;
    if (dc_yh == viewheight - 1)
        dc_yh = viewheight - 2;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error("R_DrawAltTLColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[dc_x];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    // karabas: the pointers in locals (as R_DrawColumn)
    const byte *source = dc_source, *colormap = dc_colormap;
    const byte *tint = tinttable;
    do
    {
        *dest = tint[((*dest) << 8)
                          + colormap[source[(frac >> FRACBITS) & 127]]];
        dest += SCREENWIDTH;
        frac += fracstep;
    }
    while (count--);
}

/*
========================
=
= R_DrawTranslatedColumn
=
========================
*/

byte *dc_translation;
byte *translationtables;

void R_DrawTranslatedColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[dc_x];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    // karabas: the pointers in locals (as R_DrawColumn)
    const byte *source = dc_source, *colormap = dc_colormap;
    const byte *translation = dc_translation;
    do
    {
        *dest = colormap[translation[source[frac >> FRACBITS]]];
        dest += SCREENWIDTH;
        frac += fracstep;
    }
    while (count--);
}

//============================================================================
//
// R_DrawTranslatedTLColumn
//
//============================================================================

void R_DrawTranslatedTLColumn(void)
{
    int count;
    byte *dest;
    fixed_t frac, fracstep;

    count = dc_yh - dc_yl;
    if (count < 0)
        return;

#ifdef RANGECHECK
    if ((unsigned) dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
        I_Error("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    dest = ylookup[dc_yl] + columnofs[dc_x];

    fracstep = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * fracstep;

    // karabas: the pointers in locals (as R_DrawColumn)
    const byte *source = dc_source, *colormap = dc_colormap;
    const byte *tint = tinttable;
    const byte *translation = dc_translation;
    do
    {
        *dest = tint[((*dest) << 8)
                          +
                          colormap[translation
                                      [source[frac >> FRACBITS]]]];
        dest += SCREENWIDTH;
        frac += fracstep;
    }
    while (count--);
}

//============================================================================
//
// R_DrawTranslatedAltTLColumn
//
//============================================================================

/*
void R_DrawTranslatedAltTLColumn (void)
{
	int			count;
	byte		*dest;
	fixed_t		frac, fracstep;	

	count = dc_yh - dc_yl;
	if (count < 0)
		return;
				
#ifdef RANGECHECK
	if ((unsigned)dc_x >= SCREENWIDTH || dc_yl < 0 || dc_yh >= SCREENHEIGHT)
		I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

	dest = ylookup[dc_yl] + columnofs[dc_x];
	
	fracstep = dc_iscale;
	frac = dc_texturemid + (dc_yl-centery)*fracstep;

	// karabas: the pointers in locals (as R_DrawColumn)
	const byte *source = dc_source, *colormap = dc_colormap;
	const byte *tint = tinttable;
	const byte *translation = dc_translation;
	do
	{
		*dest = tint[*dest
			+(colormap[translation[source[frac>>FRACBITS]]]<<8)];
		dest += SCREENWIDTH;
		frac += fracstep;
	} while (count--);
}
*/

//--------------------------------------------------------------------------
//
// PROC R_InitTranslationTables
//
//--------------------------------------------------------------------------

void R_InitTranslationTables(void)
{
    int i;
    byte *transLump;
    int lumpnum;

    V_LoadTintTable();

    // Allocate translation tables
    translationtables = Z_Malloc(256 * 3 * (maxplayers - 1), PU_STATIC, 0);

    for (i = 0; i < 3 * (maxplayers - 1); i++)
    {
        lumpnum = W_GetNumForName("trantbl0") + i;
        transLump = W_CacheLumpNum(lumpnum, PU_STATIC);
        memcpy(translationtables + i * 256, transLump, 256);
        W_ReleaseLumpNum(lumpnum);
    }
}

/*
================
=
= R_DrawSpan
=
================
*/

int ds_y;
int ds_x1;
int ds_x2;
lighttable_t *ds_colormap;
fixed_t ds_xfrac;
fixed_t ds_yfrac;
fixed_t ds_xstep;
fixed_t ds_ystep;
byte *ds_source;                // start of a 64*64 tile image

int dscount;                    // just for profiling

// karabas: the position packed as in doom/r_draw.c of chocolate-doom (x: 6.10 in the high half,
// y: 6.10 in the low half: one add per pixel; 10 instead of 16 bits of fraction), the pointers
// in locals (see R_DrawColumn).
void R_DrawSpan(void)
{
    unsigned int position, step;
    byte *dest;
    int count;
    const byte *source, *colormap;

#ifdef RANGECHECK
    if (ds_x2 < ds_x1 || ds_x1 < 0 || ds_x2 >= SCREENWIDTH
        || (unsigned) ds_y > SCREENHEIGHT)
        I_Error("R_DrawSpan: %i to %i at %i", ds_x1, ds_x2, ds_y);
//      dscount++;
#endif

    position = ((ds_xfrac << 10) & 0xffff0000) | ((ds_yfrac >> 6) & 0x0000ffff);
    step = ((ds_xstep << 10) & 0xffff0000) | ((ds_ystep >> 6) & 0x0000ffff);
    source = ds_source;
    colormap = ds_colormap;

    dest = ylookup[ds_y] + columnofs[ds_x1];
    count = ds_x2 - ds_x1;
#ifdef KG_HWDRAW
    if (kg_hwdraw)
    {
        int cm = kg_drw_cmap(colormap);
        if (cm >= 0 && kg_drw_src(source) && ((uint32_t) source & 3) == 0 && count >= 0)
        {
            if (kg_drw_newframe)
                kg_drw_sync();
            MMIO_DRW0 = (uint32_t) (dest - I_VideoBuffer) | (uint32_t) count << 16 | (uint32_t) cm << 25;
            MMIO_DRW_FRAC = position;
            MMIO_DRW_STEP = step;
            MMIO_DRW_SPAN = (uint32_t) source - SDRAM_BASE;
            return;
        }
    }
#endif
    do
    {
        *dest++ = colormap[source[((position >> 4) & 0x0fc0) | (position >> 26)]];
        position += step;
    }
    while (count--);
}

void R_DrawSpanLow(void)
{
    fixed_t xfrac, yfrac;
    byte *dest;
    int count, spot;

#ifdef RANGECHECK
    if (ds_x2 < ds_x1 || ds_x1 < 0 || ds_x2 >= SCREENWIDTH
        || (unsigned) ds_y > SCREENHEIGHT)
        I_Error("R_DrawSpan: %i to %i at %i", ds_x1, ds_x2, ds_y);
//      dscount++;
#endif

    xfrac = ds_xfrac;
    yfrac = ds_yfrac;

    dest = ylookup[ds_y] + columnofs[ds_x1];
    count = ds_x2 - ds_x1;
    do
    {
        spot = ((yfrac >> (16 - 6)) & (63 * 64)) + ((xfrac >> 16) & 63);
        *dest++ = ds_colormap[ds_source[spot]];
        xfrac += ds_xstep;
        yfrac += ds_ystep;
    }
    while (count--);
}



/*
================
=
= R_InitBuffer
=
=================
*/

void R_InitBuffer(int width, int height)
{
    int i;

    viewwindowx = (SCREENWIDTH - width) >> 1;
    for (i = 0; i < width; i++)
        columnofs[i] = viewwindowx + i;
    if (width == SCREENWIDTH)
        viewwindowy = 0;
    else
        viewwindowy = (SCREENHEIGHT - SBARHEIGHT - height) >> 1;
    for (i = 0; i < height; i++)
        ylookup[i] = I_VideoBuffer + (i + viewwindowy) * SCREENWIDTH;
}


/*
==================
=
= R_DrawViewBorder
=
= Draws the border around the view for different size windows
==================
*/

boolean BorderNeedRefresh;

void R_DrawViewBorder(void)
{
    byte *src, *dest;
    int x, y;

    if (scaledviewwidth == SCREENWIDTH)
        return;

    src = W_CacheLumpName("F_022", PU_CACHE);
    dest = I_VideoBuffer;

    for (y = 0; y < SCREENHEIGHT - SBARHEIGHT; y++)
    {
        for (x = 0; x < SCREENWIDTH / 64; x++)
        {
            memcpy(dest, src + ((y & 63) << 6), 64);
            dest += 64;
        }
        if (SCREENWIDTH & 63)
        {
            memcpy(dest, src + ((y & 63) << 6), SCREENWIDTH & 63);
            dest += (SCREENWIDTH & 63);
        }
    }
    for (x = viewwindowx; x < viewwindowx + viewwidth; x += 16)
    {
        V_DrawPatch(x, viewwindowy - 4, W_CacheLumpName("bordt", PU_CACHE));
        V_DrawPatch(x, viewwindowy + viewheight, W_CacheLumpName("bordb",
                                                                 PU_CACHE));
    }
    for (y = viewwindowy; y < viewwindowy + viewheight; y += 16)
    {
        V_DrawPatch(viewwindowx - 4, y, W_CacheLumpName("bordl", PU_CACHE));
        V_DrawPatch(viewwindowx + viewwidth, y, W_CacheLumpName("bordr",
                                                                PU_CACHE));
    }
    V_DrawPatch(viewwindowx - 4, viewwindowy - 4, W_CacheLumpName("bordtl",
                                                                  PU_CACHE));
    V_DrawPatch(viewwindowx + viewwidth, viewwindowy - 4,
                W_CacheLumpName("bordtr", PU_CACHE));
    V_DrawPatch(viewwindowx + viewwidth, viewwindowy + viewheight,
                W_CacheLumpName("bordbr", PU_CACHE));
    V_DrawPatch(viewwindowx - 4, viewwindowy + viewheight,
                W_CacheLumpName("bordbl", PU_CACHE));
}

/*
==================
=
= R_DrawTopBorder
=
= Draws the top border around the view for different size windows
==================
*/

boolean BorderTopRefresh;

void R_DrawTopBorder(void)
{
    byte *src, *dest;
    int x, y;

    if (scaledviewwidth == SCREENWIDTH)
        return;

/*	if(gamemode == shareware)
	{
		src = W_CacheLumpName ("FLOOR04", PU_CACHE);
	}
	else
	{
		src = W_CacheLumpName ("FLAT513", PU_CACHE);
	}
*/
    src = W_CacheLumpName("F_022", PU_CACHE);
    dest = I_VideoBuffer;

    for (y = 0; y < 34; y++)
    {
        for (x = 0; x < SCREENWIDTH / 64; x++)
        {
            memcpy(dest, src + ((y & 63) << 6), 64);
            dest += 64;
        }
        if (SCREENWIDTH & 63)
        {
            memcpy(dest, src + ((y & 63) << 6), SCREENWIDTH & 63);
            dest += (SCREENWIDTH & 63);
        }
    }
    if (viewwindowy < 35)
    {
        for (x = viewwindowx; x < viewwindowx + viewwidth; x += 16)
        {
            V_DrawPatch(x, viewwindowy - 4,
                        W_CacheLumpName("bordt", PU_CACHE));
        }
        V_DrawPatch(viewwindowx - 4, viewwindowy, W_CacheLumpName("bordl",
                                                                  PU_CACHE));
        V_DrawPatch(viewwindowx + viewwidth, viewwindowy,
                    W_CacheLumpName("bordr", PU_CACHE));
        V_DrawPatch(viewwindowx - 4, viewwindowy + 16,
                    W_CacheLumpName("bordl", PU_CACHE));
        V_DrawPatch(viewwindowx + viewwidth, viewwindowy + 16,
                    W_CacheLumpName("bordr", PU_CACHE));

        V_DrawPatch(viewwindowx - 4, viewwindowy - 4,
                    W_CacheLumpName("bordtl", PU_CACHE));
        V_DrawPatch(viewwindowx + viewwidth, viewwindowy - 4,
                    W_CacheLumpName("bordtr", PU_CACHE));
    }
}
