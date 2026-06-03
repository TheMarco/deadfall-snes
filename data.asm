; Deadfall SNES - ROM data (header + converted graphics).
.include "hdr.asm"

; ---- BG1 gameplay tilesets, one per level (4bpp, 20 metatiles + 2 palettes
; ---- each: 16 gameplay + 4 block/gem shatter frames). Per-level gem sprite,
; ---- boulder frame, and block tint, matching the original. ~2.5KB each -> all
; ---- 10 fit in one superfree bank (~26KB). render_load_-
; ---- gameplay_tiles() swaps in the current level's tiles + palettes. ----
.section ".rodata_bgtiles" superfree
bg_tiles_1_pic:  .incbin "res/bg_tiles_1.pic"
bg_tiles_1_picend:
bg_tiles_1_pal:  .incbin "res/bg_tiles_1.pal"
bg_tiles_2_pic:  .incbin "res/bg_tiles_2.pic"
bg_tiles_2_picend:
bg_tiles_2_pal:  .incbin "res/bg_tiles_2.pal"
bg_tiles_3_pic:  .incbin "res/bg_tiles_3.pic"
bg_tiles_3_picend:
bg_tiles_3_pal:  .incbin "res/bg_tiles_3.pal"
bg_tiles_4_pic:  .incbin "res/bg_tiles_4.pic"
bg_tiles_4_picend:
bg_tiles_4_pal:  .incbin "res/bg_tiles_4.pal"
bg_tiles_5_pic:  .incbin "res/bg_tiles_5.pic"
bg_tiles_5_picend:
bg_tiles_5_pal:  .incbin "res/bg_tiles_5.pal"
bg_tiles_6_pic:  .incbin "res/bg_tiles_6.pic"
bg_tiles_6_picend:
bg_tiles_6_pal:  .incbin "res/bg_tiles_6.pal"
bg_tiles_7_pic:  .incbin "res/bg_tiles_7.pic"
bg_tiles_7_picend:
bg_tiles_7_pal:  .incbin "res/bg_tiles_7.pal"
bg_tiles_8_pic:  .incbin "res/bg_tiles_8.pic"
bg_tiles_8_picend:
bg_tiles_8_pal:  .incbin "res/bg_tiles_8.pal"
bg_tiles_9_pic:  .incbin "res/bg_tiles_9.pic"
bg_tiles_9_picend:
bg_tiles_9_pal:  .incbin "res/bg_tiles_9.pal"
bg_tiles_10_pic: .incbin "res/bg_tiles_10.pic"
bg_tiles_10_picend:
bg_tiles_10_pal: .incbin "res/bg_tiles_10.pal"
.ends

; ---- Title-screen BG2 image (256x256, the DEADFALL logo). ~15KB pic. ----
.section ".rodata_title" superfree
title_pic: .incbin "res/title.pic"
title_picend:
title_map: .incbin "res/title.map"
title_pal: .incbin "res/title.pal"
.ends

; ---- HUD glyph font (4bpp 8x8, ASCII 32-95) + palette ----
.section ".rodata_hudfont" superfree
hud_font_pic:  .incbin "res/hud_font.pic"
hud_font_picend:
hud_font_pal:  .incbin "res/hud_font.pal"
hud_font_palend:
.ends

; ---- HUD/text glyph font (2bpp 8x8, for BG3 fixed text layer) ----
; Opaque: white text (index1) on black (index2); space tile = solid black.
.section ".rodata_hudfont2" superfree
hud_font2_pic:  .incbin "res/hud_font2.pic"
hud_font2_picend:
hud_font2_pal:  .incbin "res/hud_font2.pal"
.ends

; ---- Per-level seamless BG2 parallax textures (256x256 = 32x32 tiles, <=1023
; ---- unique each). render_load_tileset() swaps in the current level's texture.
; ---- Each pic is ~32KB so it MUST sit alone in its own superfree bank (the
; ---- "<32KB per LoROM bank" rule); map+pal go in a separate tiny section.
; ---- Level 1 reuses the original texture (res/bgtex_1.* == the old test3 bgtex).
.section ".rodata_bgtex1pic" superfree
bgtex_1_pic: .incbin "res/bgtex_1.pic"
bgtex_1_picend:
.ends
.section ".rodata_bgtex1map" superfree
bgtex_1_map: .incbin "res/bgtex_1.map"
bgtex_1_pal: .incbin "res/bgtex_1.pal"
.ends

.section ".rodata_bgtex2pic" superfree
bgtex_2_pic: .incbin "res/bgtex_2.pic"
bgtex_2_picend:
.ends
.section ".rodata_bgtex2map" superfree
bgtex_2_map: .incbin "res/bgtex_2.map"
bgtex_2_pal: .incbin "res/bgtex_2.pal"
.ends

.section ".rodata_bgtex3pic" superfree
bgtex_3_pic: .incbin "res/bgtex_3.pic"
bgtex_3_picend:
.ends
.section ".rodata_bgtex3map" superfree
bgtex_3_map: .incbin "res/bgtex_3.map"
bgtex_3_pal: .incbin "res/bgtex_3.pal"
.ends

.section ".rodata_bgtex4pic" superfree
bgtex_4_pic: .incbin "res/bgtex_4.pic"
bgtex_4_picend:
.ends
.section ".rodata_bgtex4map" superfree
bgtex_4_map: .incbin "res/bgtex_4.map"
bgtex_4_pal: .incbin "res/bgtex_4.pal"
.ends

.section ".rodata_bgtex5pic" superfree
bgtex_5_pic: .incbin "res/bgtex_5.pic"
bgtex_5_picend:
.ends
.section ".rodata_bgtex5map" superfree
bgtex_5_map: .incbin "res/bgtex_5.map"
bgtex_5_pal: .incbin "res/bgtex_5.pal"
.ends

.section ".rodata_bgtex6pic" superfree
bgtex_6_pic: .incbin "res/bgtex_6.pic"
bgtex_6_picend:
.ends
.section ".rodata_bgtex6map" superfree
bgtex_6_map: .incbin "res/bgtex_6.map"
bgtex_6_pal: .incbin "res/bgtex_6.pal"
.ends

.section ".rodata_bgtex7pic" superfree
bgtex_7_pic: .incbin "res/bgtex_7.pic"
bgtex_7_picend:
.ends
.section ".rodata_bgtex7map" superfree
bgtex_7_map: .incbin "res/bgtex_7.map"
bgtex_7_pal: .incbin "res/bgtex_7.pal"
.ends

.section ".rodata_bgtex8pic" superfree
bgtex_8_pic: .incbin "res/bgtex_8.pic"
bgtex_8_picend:
.ends
.section ".rodata_bgtex8map" superfree
bgtex_8_map: .incbin "res/bgtex_8.map"
bgtex_8_pal: .incbin "res/bgtex_8.pal"
.ends

.section ".rodata_bgtex9pic" superfree
bgtex_9_pic: .incbin "res/bgtex_9.pic"
bgtex_9_picend:
.ends
.section ".rodata_bgtex9map" superfree
bgtex_9_map: .incbin "res/bgtex_9.map"
bgtex_9_pal: .incbin "res/bgtex_9.pal"
.ends

.section ".rodata_bgtex10pic" superfree
bgtex_10_pic: .incbin "res/bgtex_10.pic"
bgtex_10_picend:
.ends
.section ".rodata_bgtex10map" superfree
bgtex_10_map: .incbin "res/bgtex_10.map"
bgtex_10_pal: .incbin "res/bgtex_10.pal"
.ends

; ---- Player OBJ sprite sheet (4bpp, 128-wide layout) + palette ----
.section ".rodata_player" superfree
spr_player_pic:  .incbin "res/spr_player.pic"
spr_player_picend:
spr_player_pal:  .incbin "res/spr_player.pal"
spr_player_palend:
.ends

; ---- Enemy OBJ sprite sheet (4bpp, 128-wide layout) + palette ----
.section ".rodata_enemy" superfree
spr_enemy_pic:  .incbin "res/spr_enemy.pic"
spr_enemy_picend:
spr_enemy_pal:  .incbin "res/spr_enemy.pal"
spr_enemy_palend:
.ends

; ---- Robot OBJ sprite sheet (4bpp) + palette ----
.section ".rodata_robot" superfree
spr_robot_pic:  .incbin "res/spr_robot.pic"
spr_robot_picend:
spr_robot_pal:  .incbin "res/spr_robot.pal"
spr_robot_palend:
.ends

; ---- Lightning beam segments OBJ (horizontal + vertical, 4bpp) + palettes ----
.section ".rodata_zaph" superfree
spr_zap_h_pic:  .incbin "res/spr_zap_h.pic"
spr_zap_h_picend:
spr_zap_h_pal:  .incbin "res/spr_zap_h.pal"
spr_zap_h_palend:
.ends

.section ".rodata_zapv" superfree
spr_zap_v_pic:  .incbin "res/spr_zap_v.pic"
spr_zap_v_picend:
spr_zap_v_pal:  .incbin "res/spr_zap_v.pal"
spr_zap_v_palend:
.ends

; ---- Player + enemy death animations (OBJ, 4bpp, 5 frames each) ----
.section ".rodata_pdeath" superfree
spr_pdeath_pic:  .incbin "res/spr_pdeath.pic"
spr_pdeath_picend:
spr_pdeath_pal:  .incbin "res/spr_pdeath.pal"
.ends

.section ".rodata_edeath" superfree
spr_edeath_pic:  .incbin "res/spr_edeath.pic"
spr_edeath_picend:
spr_edeath_pal:  .incbin "res/spr_edeath.pal"
.ends

; ---- Falling-tile OBJ (gem/boulder/extra-life idle, 16x16) for smooth gravity ----
.section ".rodata_falls" superfree
spr_falls_pic:  .incbin "res/spr_falls.pic"
spr_falls_picend:
spr_falls_pal:  .incbin "res/spr_falls.pal"
.ends
