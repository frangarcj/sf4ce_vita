# SF4CE Vita port: estado y continuidad

Última actualización: 2026-08-15.

Este documento recoge evidencia reproducible del port, decisiones tomadas y
trabajo pendiente. Debe actualizarse junto con cambios relevantes del loader.

## Alcance y datos del juego

El proyecto carga la biblioteca Android ARMv7 oficial
`libGltest2jni.so` y proporciona ABI Bionic, JNI y servicios Vita. El repositorio
del port no debe distribuir APK, XAPK, bibliotecas de Capcom ni recursos del
juego. Esos ficheros deben ser aportados por el usuario desde una copia legítima.

Binario Android usado durante el análisis:

- `../sf4ce/merged/lib/armeabi-v7a/libGltest2jni.so`
- Build ID ELF: `520fc2ea2df898a682742a3f9ba7a22f185fa9e7`
- Base de carga actual: `0x98000000`
- Símbolos y offsets de este documento son direcciones virtuales estáticas;
  en ejecución se suma la base del módulo.

## Estado verificado

- El `.so` Android y su `libc++_shared.so` Android se cargan mediante el loader;
  no se sustituyen por supuestas bibliotecas Vita equivalentes.
- Las relocaciones dinámicas observadas quedan resueltas a cero pendientes.
- El juego alcanza logo, título, introducción de nombre, menús, selección de
  personaje/escenario y combate en Vita3K.
- El bucle usa 50.000 microsegundos por actualización (aproximadamente 20 FPS).
  La lógica está ligada al update; 60 FPS triplica la velocidad del juego.
- La pantalla táctil frontal se traduce al formato Android de 5 puntos por 8
  `short` y permite navegar por la interfaz.
- El diálogo de nombre usa `sceImeDialog`. Cerrar el IME sólo entrega el texto;
  el OK gráfico del juego confirma el nombre y dispara localmente el callback
  de validación tipo 11.
- Servicios de red se aíslan: las solicitudes normales reciben su callback de
  fallo y la validación de nombre recibe éxito local.
- No se parchean los gates de fade del título. Dos offsets que en una versión
  anterior se trataron como red resultaron ser comprobaciones `IsFadeEnd` y se
  retiraron.
- El warning repetitivo de `glShadeModel(GL_FLAT)` se elimina seleccionando
  directamente `GL_SMOOTH`, que es el comportamiento efectivo de vitaGL.
- Se detecta el pack local completo y se refleja el estado instalado de
  contenido sólo cuando los archivos requeridos existen.
- Menú principal: las tareas 12 (Store) y 13 (Battle) se eliminan de la tabla
  de `AppMain::ST_CreateModeSelectMainPanel(bool)` mediante un parche que
  verifica código y datos antes de escribir.
- La navbar de la derecha se construye aparte del panel principal. También se
  compacta de seis a cuatro entradas y conserva únicamente 47, 48, 49 y 52;
  en Vita3K se ha comprobado que ya no se crean 50 (Store) ni 51 (Battle).
- El mando físico llega al juego y se ha comprobado en Vita3K durante un
  combate (direcciones y ataques). Se fuerza el estado de mando conectado,
  se usa muestreo analógico extendido y se descartan muestras fallidas.
- El overlay móvil de combate está oculto sin desactivar el objeto `GamePad`:
  no aparecen cruceta, stick, botones, guías, icono de mando ni el botón
  gráfico `PAUSE`. La pantalla táctil sigue disponible para los menús.
- Las referencias JNI locales se liberan al salir de cada llamada nativa y
  `NewGlobalRef` promueve la referencia a una tabla global separada. Una carga
  y combate prolongados ya no agotan la tabla local.

## Menú offline y navbar

La pantalla tiene dos construcciones separadas:

1. `AppMain::ST_CreateModeSelectMainPanel(bool)`, VA `0x5dba18`, copia 11
   entradas desde VA `0x4f6518`. Las tareas 12 y 13 son Store/Battle. El loader
   compacta la tabla a 9 entradas y cambia `movw r0,#11` en `+0x5c` a 9.
2. La columna de texto de la derecha es una navbar independiente creada por
   `AppMain::ST_CreateModeSelectSubMenu(MODE_SELECT_MODE,bool)`, VA `0x5dc0d8`.
   Copia seis entradas desde VA `0x4f67f0`: IDs 47,48,49,50,51,52.

Acciones comprobadas en `GT_NewModeSelectSSMenuObject`:

- 48 -> modo 0, Main Menu
- 49 -> modo 4, Other
- 50 -> modo 3, Store (online)
- 51 -> modo 1, Battle (online)
- 52 -> modo 2, Solo Play
- 47 es la entrada auxiliar/cabecera de la navbar

El parche actual verifica ambas tablas y sus instrucciones de conteo antes de
escribir ninguna de ellas. Después compacta el panel a nueve entradas y la
navbar a 47, 48, 49 y 52, y cambia `movw r0,#6` en
`ST_CreateModeSelectSubMenu + 0x78` por 4. La validación conjunta evita aplicar
parcialmente offsets de esta versión a otro binario. Vita3K registró creación
de 47/48/49/52 y ninguna creación de 50/51.

## Input físico y overlay

El Java original está decompilado en
`../sf4ce/decompiled/sources/jp/gltest2/android/GCControllerLib.java`.
Los bits usados por el loader coinciden con la fuente original:

- D-pad: izquierda 1, derecha 2, arriba 4, abajo 8
- botones 1–4: 32, 64, 128, 256
- L1/R1: 512/1024
- Start/Select: 65536/131072
- sticks: `[-1,1]`; el eje Y Android y Vita crece hacia abajo

Implementación actual:

- Se llama a `sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE)` antes del
  bucle; en Vita3K devuelve `0x00000000`. Si
  `sceCtrlPeekBufferPositiveExt2` no entrega muestra, la estructura se pone a
  cero para no repetir input antiguo.
- `AndroidGCController::isConnectController()` VA `0x7f2018` devuelve siempre
  verdadero en Vita.
- Mapeo: X/Círculo/Cuadrado/Triángulo -> BTN01–04, L/R -> L1/R1,
  Start/Select y D-pad/analógico izquierdo. Se conservan bits simultáneos.
- Para mantener el input táctil en menús y quitar sólo el overlay de combate,
  se sustituyen por no-op `GamePad::DrawButton()` VA `0x6ea4b8`,
  `DrawGuid()` VA `0x6ea7e8`, `DrawStick()` VA `0x6eaae8` y
  `DrawGameControllerIcon()` VA `0x6eafec`.
- El texto `PAUSE` no lo dibuja uno de esos métodos: procede del atlas
  `cockpit.obm`. `ciImgRectCockpit` VA `0x4eb8b6` contiene 120 rectángulos de
  ocho `short`; la entrada 96 es exactamente
  `{372,378,88,26,44,12,96,0}`. El loader valida esa firma y pone a cero sólo
  ancho/alto/origen de dicha entrada. Start sigue pausando el juego.
- Las transiciones de botones se registran sólo durante las primeras 32 para
  facilitar pruebas. Retirar incluso ese límite cuando el port esté estable.

Pendiente: validar el mapeo completo, sticks, L/R, Start y Select en hardware
Vita. No reactivar ni eliminar globalmente `GamePad`: hacerlo rompería el
control de combate o la navegación táctil.

## Flujo de nombre

- `AppMain::GT_NameEntryDialog`: VA `0x5f6f1c`.
- OK gráfico: alrededor de `0x5f7968`; llama a
  `ST_NetworkConnectStartBase(type=11, success=0x5f7d44,
  failure=0x5f7dbc)`.
- El loader identifica exactamente el callback success como
  `GT_NameEntryDialog + 0xe28` y lo ejecuta en el siguiente frame.
- La marca persistente es `ux0:data/sf4ce/.vita_name_entry_confirmed` y sólo
  se escribe después de que el update del juego haya procesado el OK.
- La ruta de primera ejecución se aplica justo antes de `SetSceneTitle`, tras
  cargar el save; aplicarla antes queda sobrescrita por la carga de datos.

## Problemas conocidos

- Sólo aparecen cuatro personajes seleccionables en la prueba actual. Debe
  separarse “archivos presentes” de flags de progreso/compra/desbloqueo y
  localizar la inicialización de `PLAYER_USE_CHAR_NO`/contenido sin falsificar
  disponibilidad de archivos.
- Algunas texturas de combate muestran bloques magenta/verde y colores
  incorrectos. La traza más clara son uploads repetidos
  `glTexSubImage2D(... 512x512, format=0x1908, type=0x8034, ...)`, seguidos de
  `EXC_BAD_ACCESS`; tras una ejecución larga Vita3K puede terminar con
  `Unknown format 0x84000000` y `vk::Device::createImageView: invalid`.
  `0x8034` es un tipo packed de 16 bits y la ruta de conversión/upload merece
  auditarse antes de tocar canales globalmente. Capturar dimensiones, formato,
  tipo, textura ligada, alineación y primeros bytes. Revisar también si vitaGL
  espera una extensión/formato distinto para ese tipo y si la fuente es
  legible en los 512x512 completos.

  Se probó una conversión 16->16 en los wrappers
  `glTexImage2D_compat`/`glTexSubImage2D_compat` de
  `loader/bionic_compat.cpp`, aplicada sólo cuando `type ==
  GL_UNSIGNED_SHORT_5_5_5_1` (0x8034). El cierre de Vita3K observado durante la
  primera prueba no fue un crash del emulador: fue el usuario quien cerró la
  aplicación manualmente; el cambio en sí funcionaba y corregía los bloques
  magenta/verde y las transparencias extremas.

  Una primera reinstalación (2026-08-15) invertía el bloque completo de 16
  bits, moviendo también el bit de alpha (RGBA5551 Android ->
  A1B5G5R5/"1_5_5_5_REV"). En combate esto dejó la imagen mayormente correcta
  (Blanka bien) pero E.Honda salió completamente azul: prueba visual de que
  R y B seguían intercambiados y de que mover el bit de alpha era incorrecto.
  Blanka no cambió porque su piel tiene R≈B (bajo ambos), así que un swap R/B
  es un no-op ahí; Honda tiene R alto/B bajo (piel tostada), así que el mismo
  swap sí es visible.

  Una segunda prueba (2026-08-15) sólo intercambiaba R y B dejando el bit de
  alpha en su sitio original (bit 0), sin llegar a probarse: el usuario
  señaló que si en la prueba anterior (alpha movido a bit 15) la
  transparencia ya era correcta, mover el alpha SÍ era necesario, y el fallo
  estaba únicamente en el orden de R/B dentro de esa misma conversión. Es
  decir, no hay que elegir entre mover el alpha o intercambiar R/B: hacen
  falta ambas cosas a la vez.

  Corrección aplicada (2026-08-15): alpha en el bit 15 (como en el primer
  intento, que ya dejaba la transparencia correcta) combinado con R y B
  intercambiados entre sí dentro de los 15 bits restantes (equivalente a
  layout BGRA en orden "REV" bajo el bit de alpha, en vez de RGBA). Pendiente
  confirmar en Vita3K que Honda y el resto del roster ya muestran colores
  correctos con la transparencia intacta, y luego validar en hardware Vita
  real.
- Audio está silenciado mediante stubs de Ck/SoundObj; falta backend real.
- El logging de GL/JNI/touch es excesivo y afecta rendimiento. Mantener trazas
  limitadas o bajo una opción de debug.
- Falta validar en hardware Vita además de Vita3K.

## Orden de trabajo recomendado

1. Estabilizar `glTexSubImage2D` para `GL_RGBA` + `0x8034`. Es el problema que
   corrompe más imagen, llena el log y puede tumbar Vita3K; probar una
   conversión local a RGBA8 antes de modificar el comportamiento general.
2. Validar en una Vita real el mando, el diálogo IME, el flujo de primera
   ejecución y el menú offline. Conservar los checks de versión de cada parche.
3. Localizar los flags reales de personajes/contenido. No marcar como
   instalado contenido cuyo archivo no esté presente y no incluir assets del
   juego en el repositorio/VPK.
4. Reducir trazas de assets, GL y JNI detrás de una opción de depuración y
   medir rendimiento sin logging. Mantener 20 updates/s salvo que se desacople
   explícitamente simulación de render.
5. Sustituir los stubs de audio por un backend Vita compatible y revisar su
   licencia antes de incorporar código externo.

## Archivos y comandos útiles

- Loader principal: `loader/main.cpp`
- Compatibilidad Bionic/Ck: `loader/bionic_compat.cpp`
- Resolución/carga ELF: `loader/so_util.c`
- Timing/configuración: `loader/config.h`
- Build: `cmake --build build -j6`
- Ejecutable: `build/eboot.bin`
- SHA-256 del último binario instalado y probado (sin conversión 5551):
  `b11a8c7113617244234f95be646952b067c9df6423bca6db087469c32ddd0252`
- SHA-256 del binario candidato instalado en Vita3K, con alpha en bit 15 y
  R/B intercambiados, pendiente de confirmar en combate (2026-08-15):
  `00b143fe5de0664d4834176d815aacf2dc1606a6a10bdb507a741c27afaea747`
  (binarios intermedios descartados: inversión completa con bit de alpha
  movido pero R/B sin intercambiar correctamente, Honda azul,
  `39d45dcbd20af9d3ca88b629346713cd5b4ee17973cc39b9133a7e53f7a12334`; swap
  R/B sin mover alpha, no probado por reintroducir el problema de
  transparencia, `54d81df3b1928ae172323959387fc5d10afa086fa9adc04d6ffcaba8dfac9c3a`)
- Instancia Vita3K:
  `/Users/frangar/Library/Application Support/Vita3K/Vita3K/fs/ux0/app/SF4CE0000/eboot.bin`
- Datos/log:
  `/Users/frangar/Library/Application Support/Vita3K/Vita3K/fs/ux0/data/sf4ce/boot.log`
- Ejecutar: `/Applications/Vita3K.app/Contents/MacOS/Vita3K -r SF4CE0000`

## Licencias y atribución

- La arquitectura y `so_util` proceden de la familia de loaders Vita de Andy
  Nguyen/FF3 Vita bajo licencia MIT; sus avisos deben conservarse.
- `loader/elf.h` procede de glibc y conserva LGPL-2.1-or-later.
- vitaGL es GPL-3.0 y el binario enlaza contra ella; la distribución combinada
  debe cumplir GPL-3.0 y ofrecer el código fuente correspondiente del port.
- vitaShaRK es LGPL-3.0; kubridge y demás dependencias deben conservar sus
  avisos/licencias en cualquier distribución.
- No copiar código de otros ports sin revisar licencia y atribución. Registrar
  origen, commit y fragmento reutilizado en este documento o en un fichero de
  notices.
- Nunca incluir ni publicar bibliotecas o assets propietarios de Street
  Fighter; el VPK distribuible debe contener sólo el loader y recursos propios
  con permiso de redistribución.
