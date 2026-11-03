# Lienzo Casero

Programa de diseño sencillo para usar en casa, inspirado en la forma de trabajar de Canva:
plantillas a la izquierda, lienzo en el centro y una barra arriba que cambia según lo que
tengas seleccionado. Está hecho en **C** con [raylib](https://www.raylib.com/) y funciona
sin internet en **Windows 10 y 11 (64 bits)**.

![Captura de Lienzo Casero](docs/captura.png)

## Qué se puede hacer

- **Plantillas**: 6 de ejemplo (cumpleaños, invitación, Navidad, lista de la compra,
  menú semanal, nota para la nevera) y las tuyas propias con "Guardar plantilla".
- **Textos** con tamaño, color y alineación. Doble clic para escribir.
- **Fuentes**: 25 gratuitas incluidas (Lobster, Pacifico, Bungee, Bebas Neue, Playfair...),
  las de Windows y las que tú añadas (.ttf u .otf de fonts.google.com, dafont.com...).
  Una fuente añadida viaja dentro del diseño: al abrirlo en otro ordenador se instala sola.
- **Efectos de texto**: sombra, contorno, relieve 3D, grabado y neón, con color e intensidad.
- **Formas**: rectángulo (también con esquinas redondeadas), círculo, triángulo, estrella,
  corazón, línea y flecha. Con relleno, borde y transparencia.
- **Dibujos**: 140 dibujos de colores en 10 temas (fiestas, amor, animales, comida, Navidad...).
  También se pueden abrir archivos **SVG** propios, por ejemplo los de vector4free.com.
- **Imágenes**: desde el ordenador, arrastrándolas a la ventana o pegándolas con Ctrl+V.
  Se guardan dentro del diseño, así que un `.lienzo` se puede pasar a otro ordenador.
- **Mover, cambiar de tamaño y girar** con el ratón. Imán para centrar en el lienzo.
- **Capas**: ver/ocultar, bloquear, subir, bajar, duplicar y borrar.
- **Deshacer y rehacer** (Ctrl+Z / Ctrl+Y).
- **Exportar** a PNG (para WhatsApp o redes) y a PDF (para imprimir).
- Tamaños listos: Instagram, historia, A4 vertical y horizontal, tarjeta, póster...

## Instalarlo en Windows

1. Entra en la pestaña **Actions** del repositorio, abre la última ejecución correcta de
   "Compilar" y descarga `LienzoCasero-instalador-windows`.
   (Cuando se publique una versión, también estará en **Releases**.)
2. Descomprime el zip y ejecuta `LienzoCasero-0.3.0-instalador.exe`.
3. Windows puede avisar con "Windows protegió su PC" porque el programa no está firmado:
   pulsa **Más información** y luego **Ejecutar de todas formas**.

El instalador crea un acceso directo en el escritorio y en el menú Inicio, y hace que los
archivos `.lienzo` se abran con doble clic. Se desinstala desde *Configuración > Aplicaciones*.
Los diseños y plantillas de cada persona se guardan en `%APPDATA%\LienzoCasero`.

## Atajos de teclado

| Atajo | Qué hace |
|---|---|
| Ctrl+Z / Ctrl+Y | Deshacer / rehacer |
| Ctrl+S / Ctrl+Mayús+S | Guardar / guardar como |
| Ctrl+O / Ctrl+N | Abrir / nuevo diseño |
| Ctrl+C / Ctrl+V / Ctrl+X | Copiar / pegar / cortar |
| Ctrl+D | Duplicar |
| Supr | Borrar lo seleccionado |
| Flechas (con Mayús. de 10 en 10) | Mover lo seleccionado |
| Ctrl+E / Ctrl+P | Exportar PNG / PDF |
| Ctrl+0 | Ajustar el lienzo a la ventana |
| Rueda del ratón | Zoom |
| Botón derecho o Espacio + arrastrar | Mover la vista |

## Compilar desde el código

Hace falta CMake 3.16 o más nuevo y un compilador de C. raylib se descarga solo.

**En Windows** (con [MSYS2](https://www.msys2.org/) y su terminal *UCRT64*):

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake make
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

**Desde Linux, para Windows** (así lo hace GitHub automáticamente):

```sh
sudo apt install mingw-w64 nsis
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build-win
makensis -DEXE=$PWD/build-win/LienzoCasero.exe installer/lienzo-casero.nsi
```

**En Linux** (para probar):

```sh
sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
cmake -B build && cmake --build build && ./build/LienzoCasero
```

## Cómo está organizado el código

| Archivo | Qué contiene |
|---|---|
| `src/main.c` | La ventana, los paneles, las barras y lo que pasa con el ratón y el teclado |
| `src/doc.c` | El diseño: elementos, capas, deshacer/rehacer y guardar/abrir `.lienzo` |
| `src/render.c` | Cómo se dibuja cada cosa, los tipos de letra y exportar a PNG/PDF |
| `src/templates.c` | Carpeta de plantillas y las plantillas de ejemplo |
| `src/ui.c` | Botones, deslizadores y casillas de texto |
| `src/platform.c` | Ventanas de Abrir/Guardar de Windows y archivos con tildes |
| `installer/` | Script del instalador (NSIS) |

## Próximos pasos

- Importar diseños descargados de Canva en PDF, con los textos como capas editables.
- Agrupar y seleccionar varios elementos a la vez.
- Recortar imágenes y filtros sencillos.

## Licencias de terceros

- raylib: licencia zlib/libpng.
- nanosvg (lectura de SVG): licencia zlib.
- Dibujos: Twemoji, CC BY 4.0 (ver `assets/dibujos/LEEME.md`).
- Fuentes incluidas: SIL Open Font License 1.1 y Apache 2.0 (ver `assets/fonts/LEEME.md`).
