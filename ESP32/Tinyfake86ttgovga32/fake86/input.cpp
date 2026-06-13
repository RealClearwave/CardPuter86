//  Fake86: A portable, open-source 8086 PC emulator.
//  input.cpp: keycode translation utilities (Cardputer port)

#include "gbConfig.h"
#include <stdint.h>
#include "gbGlobals.h"
#include "input.h"

#define uint8_t unsigned char

unsigned char keyboardwaitack = 0;

unsigned char translatescancode(unsigned short int keyval) {
    switch (keyval) {
        case 0x1B:  return (1);     break; // Esc
        case 0x30:  return (0xB);   break; // zero
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:  return (keyval - 0x2F); break;
        case 0x2D:  return (0xC);   break; // -_
        case 0x3D:  return (0xD);   break; // =+
        case 0x8:   return (0xE);   break; // backspace
        case 0x9:   return (0xF);   break; // tab
        case 0x71:  return (0x10);  break; // q
        case 0x77:  return (0x11);  break; // w
        case 0x65:  return (0x12);  break; // e
        case 0x72:  return (0x13);  break; // r
        case 0x74:  return (0x14);  break; // t
        case 0x79:  return (0x15);  break; // y
        case 0x75:  return (0x16);  break; // u
        case 0x69:  return (0x17);  break; // i
        case 0x6F:  return (0x18);  break; // o
        case 0x70:  return (0x19);  break; // p
        case 0x5B:  return (0x1A);  break;
        case 0x5D:  return (0x1B);  break;
        case 0xD:
        case 0x10F: return (0x1C);  break; // enter
        case 0x131:
        case 0x132: return (0x1D);  break; // ctrl
        case 0x61:  return (0x1E);  break; // a
        case 0x73:  return (0x1F);  break; // s
        case 0x64:  return (0x20);  break; // d
        case 0x66:  return (0x21);  break; // f
        case 0x67:  return (0x22);  break; // g
        case 0x68:  return (0x23);  break; // h
        case 0x6A:  return (0x24);  break; // j
        case 0x6B:  return (0x25);  break; // k
        case 0x6C:  return (0x26);  break; // l
        case 0x3B:  return (0x27);  break;
        case 0x27:  return (0x28);  break;
        case 0x60:  return (0x29);  break;
        case 0x130: return (0x2A);  break; // left shift
        case 0x5C:  return (0x2B);  break;
        case 0x7A:  return (0x2C);  break; // z
        case 0x78:  return (0x2D);  break; // x
        case 0x63:  return (0x2E);  break; // c
        case 0x76:  return (0x2F);  break; // v
        case 0x62:  return (0x30);  break; // b
        case 0x6E:  return (0x31);  break; // n
        case 0x6D:  return (0x32);  break; // m
        case 0x2C:  return (0x33);  break;
        case 0x2E:  return (0x34);  break;
        case 0x2F:  return (0x35);  break;
        case 0x12F: return (0x36);  break; // right shift
        case 0x13C: return (0x37);  break; // print screen
        case 0x133:
        case 0x134: return (0x38);  break; // alt
        case 0x20:  return (0x39);  break; // space
        case 0x12D: return (0x3A);  break; // caps lock
        case 0x11A:
        case 0x11B:
        case 0x11C:
        case 0x11D:
        case 0x11E:
        case 0x11F:
        case 0x120:
        case 0x121:
        case 0x122:
        case 0x123: return (keyval - 0x11A + 0x3B); break; // F1-F10
        case 0x12C: return (0x45);  break; // num lock
        case 0x12E: return (0x46);  break; // scroll lock
        case 0x116:
        case 0x107: return (0x47);  break; // home
        case 0x111:
        case 0x108: return (0x48);  break; // up
        case 0x118:
        case 0x109: return (0x49);  break; // pgup
        case 0x10D: return (0x4A);  break; // keypad -
        case 0x114:
        case 0x104: return (0x4B);  break; // left
        case 0x105: return (0x4C);  break; // center
        case 0x113:
        case 0x106: return (0x4D);  break; // right
        case 0x10E: return (0x4E);  break; // keypad +
        case 0x117:
        case 0x101: return (0x4F);  break; // end
        case 0x112:
        case 0x102: return (0x50);  break; // down
        case 0x119:
        case 0x103: return (0x51);  break; // pgdn
        case 0x115:
        case 0x100: return (0x52);  break; // ins
        case 0x7F:
        case 0x10A: return (0x53);  break; // del
        default:     return (0);
    }
}

uint8_t buttons = 0;
extern void setwindowtitle(uint8_t *extra);

void mousegrabtoggle() {
    // No mouse grab on Cardputer
}

extern uint32_t usefullscreen;
