# CardPuter86

CardPuter86 es un emulador compacto de PC 8086 para M5Stack Cardputer. Adapta Fake86 y ESP32TinyFake86 al ESP32-S3, teclado, pantalla de 240x135, altavoz y tarjeta microSD del Cardputer.

## Compilar con PlatformIO

Abra `ESP32/CardPuter86` como proyecto de PlatformIO o compile desde una terminal:

```sh
cd ESP32/CardPuter86
pio run
```

Para cargar el firmware:

```sh
pio run --target upload
```

El script `flash.sh` del directorio principal también compila, detecta el puerto serie USB y carga el firmware.

## Controles

Las teclas normales del Cardputer corresponden a las teclas del PC. La tecla Aa corresponde a Caps Lock, mientras que Ctrl y Alt funcionan como modificadores. Opt es la capa de funciones de CardPuter86: Opt+1 hasta Opt+0 envían F1-F10, Opt+- envía F11 y Opt+= envía F12. Fn y G0 quedan reservados para funciones futuras.

## Software integrado

Los datos ROM, discos y programas COM se encuentran en `ESP32/CardPuter86/CardPuter86/dataFlash`. La herramienta `tools/ima2h`, procedente de ESP32TinyFake86, convierte imágenes y binarios compatibles en cabeceras C.

## Proyecto original

CardPuter86 conserva el núcleo Fake86 y reconoce el trabajo de Mike Chambers en Fake86 y de Ackerman en ESP32TinyFake86.
