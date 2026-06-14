# CardPuter86

CardPuter86 es un emulador compacto de PC 8086 para M5Stack Cardputer. Adapta Fake86 y ESP32TinyFake86 al ESP32-S3, teclado, pantalla de 240x135, altavoz y tarjeta microSD del Cardputer.

## Compilar con PlatformIO

Abra `ESP32/CardPuter86` como proyecto de PlatformIO o compile desde una terminal:

```sh
cd ESP32/CardPuter86
pio run
```

Para la primera instalacion, cargue el firmware e inicialice la particion IMG interna:

```sh
./flash.sh --with-images
```

`--with-images` borra el dispositivo y reinstala la particion de imagenes predeterminada. Las ejecuciones posteriores de `./flash.sh` actualizan solo el firmware y conservan las imagenes importadas.

## Controles

Las teclas normales del Cardputer corresponden a las teclas del PC. La tecla Aa corresponde a Shift, mientras que Ctrl y Alt funcionan como modificadores. Fn es la capa de funciones de CardPuter86: Fn+1 hasta Fn+0 envían F1-F10, Fn+- envía F11 y Fn+= envía F12. Opt cambia entre texto 1:1 y gráficos escalados a pantalla completa. El texto largo se ajusta sin reducirse y la vista sigue la parte inferior del contenido. G0 queda reservado.

## Imagenes de disco

Los archivos `.img` grabables se guardan en una particion FAT independiente de la Flash interna o en la raiz de la microSD. Si hay varias imagenes, un menu de inicio permite elegir cual arrancar. Tambien se aceptan archivos `.dsk` antiguos.

Después de la comprobación opcional de SD, mantenga pulsado `Ctrl` para entrar en modo USB. Si la SD se activó con `Alt` y fue detectada, elija Flash interna o SD; de lo contrario se exporta la Flash interna. Copie los IMG, expulse la unidad de forma segura y reinicie.

## Software integrado

Los datos ROM y programas COM se encuentran en `ESP32/CardPuter86/CardPuter86/dataFlash`. La imagen independiente predeterminada es `ESP32/CardPuter86/data/cardputer86.img`.

## Proyecto original

CardPuter86 conserva el núcleo Fake86 y reconoce el trabajo de Mike Chambers en Fake86 y de Ackerman en ESP32TinyFake86.
