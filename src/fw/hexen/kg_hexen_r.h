// karabas (Hexen Ф1г): helpers of the renderer copies (r_segs.c, r_plane.c, r_things.c).
#pragma once
#include "w_wad.h"
#include "w_file.h"

// r_data.c
extern int *texturewidthmask;
extern short **texturecolumnlump;
extern unsigned short **texturecolumnofs;
extern byte **texturecomposite;
void R_GenerateComposite(int texnum);

// R_GetColumn of r_data.c without the call of W_CacheLumpNum for a lump of the memory-mapped WAD
// (its pointer is mapped + position, as W_CacheLumpNum returns it); same result.
static inline byte *kg_getcolumn(int tex, int col)
{
    int lump, ofs;
    col &= texturewidthmask[tex];
    lump = texturecolumnlump[tex][col];
    ofs = texturecolumnofs[tex][col];
    if (lump > 0)
    {
        lumpinfo_t *l = lumpinfo[lump];
        if (l->wad_file->mapped != NULL)
            return l->wad_file->mapped + l->position + ofs;
        return (byte *) W_CacheLumpNum(lump, PU_CACHE) + ofs;
    }
    if (!texturecomposite[tex])
        R_GenerateComposite(tex);
    return texturecomposite[tex] + ofs;
}

#ifdef KG_HWDRAW
// r_draw.c: the drawer of the FPGA
int kg_drw_cmidx(const byte *colormap);     // its colormap index, -1: not for the drawer
int kg_drw_col(int x, int yl, int yh, fixed_t texturemid, fixed_t iscale, int cm, const byte *source);
#endif
