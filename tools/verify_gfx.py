#!/usr/bin/env python3
"""PPU-style preview of the converted SNES graphics (no emulator needed).

Decodes res/bg_tiles.pic (4bpp) + .pal (BGR555) and renders Level 1's starting
section exactly as render.c maps it (metatile -> 4 tiles TL/TR/BL/BR, 16px HUD
offset). Also decodes res/spr_player.pic and lays out the player frames.
Writes preview.png for visual verification of tile decode / palette / ordering.
"""
import re, ast
import numpy as np
from PIL import Image

RES = "res"
LV = "../cubed/src/levels/level1.js"

# tile types
EMPTY,BLOCK,GEM,BOULDER,SPAWN,PORTAL,RSPAWN,ELBLK,ELIFE = range(9)
MT_EMPTY,MT_BLOCK0,MT_GEM0,MT_BOULDER,MT_PORTAL0,MT_SPAWN0,MT_EXTRALIFE,MT_ROBOTSPN = 0,1,4,7,8,12,14,15

def metatile(t):
    return {EMPTY:MT_EMPTY,BLOCK:MT_BLOCK0,GEM:MT_GEM0,BOULDER:MT_BOULDER,
            SPAWN:MT_SPAWN0,PORTAL:MT_PORTAL0,RSPAWN:MT_ROBOTSPN,
            ELBLK:MT_BLOCK0,ELIFE:MT_EXTRALIFE}[t]

def load_pal(path):
    b = open(path,"rb").read()
    out=[]
    for i in range(0,len(b),2):
        c=b[i]|(b[i+1]<<8)
        r,g,bl=c&31,(c>>5)&31,(c>>10)&31
        out.append((r*255//31,g*255//31,bl*255//31))
    while len(out)<16: out.append((255,0,255))
    return out

# Gameplay tiles now use TWO sub-palettes (see render.c mt_palbits / build_gfx
# MT_PAL): block/boulder/gem -> pal0, the markers (metatile >= 8) -> pal1.
def mt_palette(mt, pal0, pal1):
    return pal1 if mt >= 8 else pal0

def decode_4bpp(path):
    d=open(path,"rb").read()
    tiles=[]
    for t in range(len(d)//32):
        o=t*32; tile=np.zeros((8,8),np.uint8)
        for y in range(8):
            p0,p1=d[o+y*2],d[o+y*2+1]
            p2,p3=d[o+16+y*2],d[o+16+y*2+1]
            for x in range(8):
                b=7-x
                tile[y,x]=((p0>>b)&1)|(((p1>>b)&1)<<1)|(((p2>>b)&1)<<2)|(((p3>>b)&1)<<3)
        tiles.append(tile)
    return tiles

def blit(img, tile, pal, px, py, transparent=None):
    for y in range(8):
        for x in range(8):
            v=tile[y,x]
            if v==transparent: continue
            img[py+y,px+x]=pal[v]

def parse_section0():
    t=open(LV).read()
    s=t.index("[",re.search(r"=\s*",t).end()); d=0
    for i in range(s,len(t)):
        d+= t[i]=="[" ; d-= t[i]=="]"
        if d==0: arr=t[s:i+1]; break
    return ast.literal_eval(re.sub(r"//[^\n]*","",arr))[0]

def main():
    fullpal=load_pal(f"{RES}/bg_tiles.pal")
    pal0,pal1=fullpal[0:16], (fullpal[16:32] if len(fullpal)>=32 else fullpal[0:16])
    bg=decode_4bpp(f"{RES}/bg_tiles.pic")
    sec=parse_section0()

    # BG1 view: 256x224, HUD 16px black on top. Each metatile drawn through its
    # own sub-palette so gems (pal0 red) and markers (pal1 blue) look correct.
    view=np.zeros((224,256,3),np.uint8)
    for gy in range(13):
        for gx in range(16):
            mt=metatile(sec[gy][gx]); base=mt*4
            pal=mt_palette(mt,pal0,pal1)
            ox,oy=gx*16, 16+gy*16
            blit(view,bg[base+0],pal,ox,oy)       # TL
            blit(view,bg[base+1],pal,ox+8,oy)     # TR
            blit(view,bg[base+2],pal,ox,oy+8)     # BL
            blit(view,bg[base+3],pal,ox+8,oy+8)   # BR
    Image.fromarray(view).resize((512,448),Image.NEAREST).save("preview_bg.png")

    # Player frames: 7x2 grid of 16x16, transparent shown magenta
    ppal=load_pal(f"{RES}/spr_player.pal")
    pt=decode_4bpp(f"{RES}/spr_player.pic")  # 64 tiles in 16-wide grid
    pv=np.full((2*16, 7*16, 3),(255,0,255),np.uint8)
    for fr in range(2):
        for fc in range(7):
            base=fr*32+fc*2
            ox,oy=fc*16, fr*16
            blit(pv,pt[base],ppal,ox,oy)
            blit(pv,pt[base+1],ppal,ox+8,oy)
            blit(pv,pt[base+16],ppal,ox,oy+8)
            blit(pv,pt[base+17],ppal,ox+8,oy+8)
    Image.fromarray(pv).resize((7*48,2*48),Image.NEAREST).save("preview_player.png")
    print("wrote preview_bg.png (512x448) and preview_player.png")

if __name__=="__main__":
    main()
