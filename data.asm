; Deadfall SNES - ROM data (header + converted graphics).
.include "hdr.asm"

; ---- BG1 gameplay tileset (4bpp, 16 metatiles) + palette ----
.section ".rodata_bgtiles" superfree
bg_tiles_pic:  .incbin "res/bg_tiles.pic"
bg_tiles_picend:
bg_tiles_pal:  .incbin "res/bg_tiles.pal"
bg_tiles_palend:
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

; ---- Seamless repeating background texture (128x96 = 16x12 tiles, ~193
; ---- unique). Tiled across BG2 for ALL levels: tiny + scrolls cleanly. ----
; pic is ~30KB (256x256 = up to 1000 tiles); keep it alone so the section fits one
; 32KB LoROM bank. map+pal live in a separate (tiny) superfree section.
.section ".rodata_bgtexpic" superfree
bgtex_pic: .incbin "res/bgtex.pic"
bgtex_picend:
.ends

.section ".rodata_bgtexmap" superfree
bgtex_map: .incbin "res/bgtex.map"
bgtex_pal: .incbin "res/bgtex.pal"
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
