#ifndef _GB_GLOBALS_H
 #define _GB_GLOBALS_H
 #include "gbConfig.h"
 #include <stdio.h>

 #define fast_tiny_port_0x60 11
 #define fast_tiny_port_0x61 12
 #define fast_tiny_port_0x64 14
 #define fast_tiny_port_0x3C0 37
 #define fast_tiny_port_0x3C4 40
 #define fast_tiny_port_0x3D4 44
 #define fast_tiny_port_0x3D8 46
 #define fast_tiny_port_0x3D9 47
 #define fast_tiny_port_0x3B9 31

 //extern unsigned char verbose; //No lo necesito
 extern unsigned int lasttick;

 extern unsigned char bootdrive,renderbenchmark;

 extern unsigned char gb_force_set_cga;
 extern unsigned char gb_force_load_com;
 extern unsigned char gb_id_cur_com;
 extern unsigned char gb_force_boot;
 extern unsigned char gb_force_load_dsk;
 extern unsigned char gb_id_cur_dsk;
 #ifdef use_lib_snapshot
  extern unsigned char gb_force_snapshot_begin;
  extern unsigned char gb_force_snapshot_end;
 #endif

 #ifdef use_lib_limit_256KB
  extern unsigned char gb_check_memory_before;
 #endif

 extern unsigned char cf;
 extern unsigned char hdcount;

 extern unsigned char gb_portramTiny[51]; //Solo 51 puertos
 extern void * gb_portTiny_write_callback[5];
 extern void * gb_portTiny_read_callback[5];

 extern unsigned char running;

 extern unsigned char vidmode;

 #ifdef use_lib_debug_interrupt
  extern unsigned char gb_interrupt_before;
 #endif

 extern unsigned char didbootstrap;

 //Parte parsecl
 extern unsigned char slowsystem;
 extern unsigned short int constantw;
 extern unsigned short int constanth;

 extern unsigned char speakerenabled;

 extern const unsigned char fontcga[];
 extern unsigned long int gb_jj_cont_timer;
 extern unsigned char scrmodechange;

 extern const unsigned char palettecga[16];

 extern unsigned short int segregs[4];

 extern unsigned int usegrabmode;

 extern unsigned int x,y;

 extern unsigned char gb_video_cga[16384];

 extern unsigned char *gb_ram_00; //32 KB
 extern unsigned char *gb_ram_01;
 extern unsigned char *gb_ram_02;
 extern unsigned char *gb_ram_03;
 extern unsigned char *gb_ram_04;
 extern unsigned char *gb_ram_bank[5];

 // Cardputer: flat framebuffer 320x200 byte-indexed
 extern unsigned char *gb_frame_buffer;

 extern volatile unsigned char keymap[256];
 extern volatile unsigned char oldKeymap[256];

 extern unsigned char gb_key_cur[80];
 extern unsigned char gb_key_before[80];

 // Cardputer: TFT RGB565 palette (16 entries)
 extern unsigned short int gb_palette_rgb565[16];
 extern unsigned short int gb_palette_text_rgb565[16];

 extern unsigned char gb_font_8x8;

 //retrazo
 extern unsigned char port3da;

 extern unsigned char gb_reset;

 //Medicion tiempos
 extern unsigned int jj_ini_cpu,jj_end_cpu,jj_ini_vga,jj_end_vga;
 extern unsigned int gb_max_cpu_ticks,gb_min_cpu_ticks,gb_cur_cpu_ticks;
 extern unsigned int gb_max_vga_ticks,gb_min_vga_ticks,gb_cur_vga_ticks;
 extern unsigned char tiempo_vga;

 extern unsigned char keyboardwaitack;

 extern unsigned char gb_ram_truco_low;
 extern unsigned char gb_ram_truco_high;

 extern unsigned char gb_use_snarare_madmix;
 extern unsigned char gb_use_remap_cartdridge;

 extern unsigned char gb_frec_speaker_low;
 extern unsigned char gb_frec_speaker_high;
 extern unsigned char gb_cont_frec_speaker;
 extern volatile int gb_frecuencia01;
 extern volatile int gb_volumen01;
 extern volatile unsigned int gb_pulsos_onda;
 extern volatile unsigned int gb_cont_my_callbackfunc;
 extern volatile unsigned char speaker_pin_estado;

 extern unsigned char gb_delay_tick_cpu_milis;
 extern unsigned char gb_auto_delay_cpu;
 extern unsigned char gb_vga_poll_milis;
 extern unsigned char gb_keyboard_poll_milis;
 extern unsigned char gb_timers_poll_milis;

 extern unsigned char gb_invert_color;
 extern unsigned char gb_silence;

 #ifdef use_lib_speaker_cpu
  #define SAMPLE_RATE 10000
 #else
  #define SAMPLE_RATE 16000
 #endif

#endif
