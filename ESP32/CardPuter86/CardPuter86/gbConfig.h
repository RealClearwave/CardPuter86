#ifndef _GB_CONFIG_H
 #define _GB_CONFIG_H

 #define true 1
 #define false 0

 // ============================================
 // Cardputer platform defines
 // ============================================
 #define use_lib_cardputer
 #define use_lib_tft
 #define use_lib_i2s
 #define use_lib_sdcard

 // Display dimensions
 #define CARDputer_SCREEN_W 240
 #define CARDputer_SCREEN_H 135
 #define FAKE86_FB_W 320
 #define FAKE86_FB_H 200

 //Debug i2s
 //#define use_lib_debug_i2s

 //Cuando uso capturadora usb
 //#define use_lib_capture_usb

 //Corrige teclados que no se inicializan(solucion dcrespo3D) - not needed for Cardputer
 //#define FIX_PERIBOARD_NOT_INITING

 //#define use_lib_sna_rare
 //#define use_lib_speaker_cpu

 //Section Optimice
 #define use_lib_fast_push
 #define use_lib_fast_pop
 #define use_lib_fast_op_add16
 #define use_lib_fast_op_sub16
 #define use_lib_fast_op_and8
 #define use_lib_fast_op_add8
 #define use_lib_fast_op_writew86
 #define use_lib_fast_op_xor8
 #define use_lib_fast_op_or16
 #define use_lib_fast_op_or8
 #define use_lib_fast_op_and16
 #define use_lib_fast_flag_log8
 #define use_lib_fast_flag_log16
 //#define use_lib_fast_flag_adc8
 //#define use_lib_fast_flag_adc16
 //#define use_lib_fast_flag_add8
 //#define use_lib_fast_flag_add16
 //#define use_lib_fast_flag_sbb8
 //#define use_lib_fast_flag_sbb16
 #define use_lib_fast_op_xor16
 //#define use_lib_fast_flag_sub8
 //#define use_lib_fast_flag_sub16
 #define use_lib_fast_op_sub8

 #define use_lib_fast_readw86
 #define use_lib_fast_flag_szp8
 #define use_lib_fast_flag_szp16

 #define use_lib_fast_op_adc8
 #define use_lib_fast_op_adc16
 #define use_lib_fast_op_sbb8
 #define use_lib_fast_op_sbb16

 #define use_lib_fast_modregrm
 #define use_lib_fast_readrm16
 #define use_lib_fast_readrm8
 #define use_lib_fast_writerm16
 #define use_lib_fast_writerm8
 #define use_lib_fast_op_div8


 #define use_lib_fast_decodeflagsword
 #define use_lib_fast_makeflagsword

 //#define use_lib_fast_doirq

 //#define use_lib_adlib
 //#define use_lib_disneysound
 //#define use_lib_mouse
 //#define use_lib_net

 //Usar 1 solo nucleo
 #define use_lib_singlecore

 //#define use_lib_snapshot
 //#define use_lib_limit_256KB

 #define gb_max_portram 0x3FF
 #define use_lib_limit_portram

 // 512 KB guest RAM backed by a 128 KB SRAM page cache and Flash swap.
 #define gb_max_ram 524288

 //#define use_lib_not_use_callback_port
 #define use_lib_fast_boot

 //options debug
 //#define use_lib_debug_interrupt



 //milisegundos espera en cada frame
 #define use_lib_delay_tick_cpu_auto 0
 #define use_lib_delay_tick_cpu_milis 0
 #define use_lib_vga_poll_milis 41
 #define use_lib_keyboard_poll_milis 20
 #define use_lib_timers_poll_milis 54

 //Logs
 #define use_lib_log_serial

#endif
