# CardPuter86

CardPuter86 es un emulador compacto de PC 8086 para M5Stack Cardputer. Adapta Fake86 y ESP32TinyFake86 al ESP32-S3, teclado, pantalla de 240x135, altavoz y tarjeta microSD del Cardputer.

| POST | BIOS |
| --- | --- |
| ![CardPuter86 POST](preview/cardputer86-post.png) | ![CardPuter86 BIOS](preview/cardputer86-bios.png) |
| DOS | BASIC |
| ![CardPuter86 DOS](preview/cardputer86-dos.png) | ![CardPuter86 BASIC](preview/cardputer86-basic.png) |

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

## Publicacion para M5Burner

Ejecute `./flash.sh --package` para crear una imagen completa de 8 MB que se puede importar en M5Burner v3 User Custom y un archivo ZIP con las imagenes nombradas por direccion, `m5burner.json`, el mapa de Flash y las sumas SHA-256. La version se lee de `VERSION` o se reemplaza con `--version X.Y.Z`. El empaquetado no graba el dispositivo e incluye la imagen IMG interna predeterminada.

La portada de M5Burner esta disponible como [SVG](preview/cardputer86-cover.svg) y [PNG](preview/cardputer86-cover.png); ambos archivos se incluyen en cada paquete.

## Controles

Las teclas normales del Cardputer corresponden a las teclas del PC. La tecla Aa corresponde a Shift, mientras que Ctrl y Alt funcionan como modificadores. Fn es la capa de funciones de CardPuter86: Fn+1 hasta Fn+0 envían F1-F10, Fn+- envía F11, Fn+= envía F12, Fn+` envía Esc y Fn+Retroceso envía Delete. En el modo de texto predeterminado al estilo DSx86, Fn+;, Fn+., Fn+, y Fn+/ desplazan la vista 40x16 hacia arriba, abajo, izquierda y derecha. El desplazamiento manual activa el modo FIXED; Fn+' devuelve una vista FIXED a su posición inicial superior izquierda y Fn+Espacio restaura AUTO. AUTO sigue la última línea de contenido y mantiene fijadas hasta dos líneas de estado detectadas al pie. Opt cambia entre este modo de texto legible con celdas 6x8 y el modo escalado. El modo escalado usa una fuente 3x5 para las pantallas de texto y escala las pantallas gráficas a toda la LCD. G0 queda reservado.

La vista de texto predeterminada usa los [glifos Adafruit Classic 5x7](https://github.com/adafruit/Adafruit-GFX-Library/blob/master/glcdfont.c) con licencia BSD en celdas 6x8. La vista de texto escalada usa [Tom Thumb](https://opengameart.org/content/tom-thumb-tiny-ascii-font-3x5) de Robey Pointer, publicada bajo CC0.

## Audio

El PC Speaker se genera en una tarea FreeRTOS fijada al Core 0 y se envia a I2S DMA en bloques de 128 muestras. La imagen predeterminada `cardputer86.img` incluye `SNDTEST.COM`; ejecutelo desde DOS para reproducir una breve prueba por PIT/canal 2 y puerto `61h`.

## Imagenes de disco

Los archivos `.img` grabables se guardan en una particion FAT independiente de la Flash interna o en la raiz de la microSD. Si hay varias imagenes, un menu de inicio permite elegir cual arrancar. Tambien se aceptan archivos `.dsk` antiguos.

Después de la comprobación opcional de SD, mantenga pulsado `Ctrl` para entrar en modo USB. Si la SD se activó con `Alt` y fue detectada, elija Flash interna o SD; de lo contrario se exporta la Flash interna. Copie los IMG, expulse la unidad de forma segura y reinicie.

Pulse `Ctrl` después de la comprobación SD para abrir Settings. El modo USB sólo se aplica al arranque actual. La opción de memoria de 512 KB se guarda en NVS y persiste tras apagar; desactivada, el PC emulado usa 128 KB. En modo 512 KB, las páginas activas usan una caché SRAM de 128 KB y las páginas frías una partición Flash con wear levelling.

Settings también guarda un perfil aproximado de velocidad 8086: 4.77 MHz, 8 MHz, 10 MHz, 12 MHz o Unlimited. Los perfiles limitados suponen una media de cuatro ciclos por instrucción, por lo que la velocidad exacta depende del código ejecutado.

## Software integrado

Los datos ROM y programas COM se encuentran en `ESP32/CardPuter86/CardPuter86/dataFlash`. La imagen independiente predeterminada es `ESP32/CardPuter86/data/cardputer86.img`.

## Proyecto original

CardPuter86 conserva el núcleo Fake86 y reconoce el trabajo de Mike Chambers en Fake86 y de Ackerman en ESP32TinyFake86.

## Licencia

CardPuter86 se distribuye bajo la [GNU General Public License v3.0 o posterior](LICENSE). Los componentes de terceros bajo BSD, CC0 y LGPL conservan sus avisos y condiciones originales.
