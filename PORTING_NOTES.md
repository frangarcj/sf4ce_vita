# SF4CE Vita port: estado y continuidad

Última actualización: 2026-10-16.

Este documento recoge evidencia reproducible del port, decisiones tomadas y
trabajo pendiente. Debe actualizarse junto con cambios relevantes del loader.

## Teclado de primera ejecución (2026-10-16)

- Se prueba el VPK publicado de CI en Vita3K portable retirando
  `savedata2.dat` y `.vita_name_entry_confirmed`, con copia previa en
  `/private/tmp/sf4ce-name-entry-20261016/`. Tocar el título abre el registro;
  tocar el campo abre el IME y permite escribir. Vita3K dibuja su propio
  overlay sin necesitar `sceCommonDialogUpdate`, por lo que esa prueba no
  detectaba que el port presentaba todos los frames con `GL_FALSE`.
- Los dos caminos de presentación del bucle pasan ahora `g_ime_active` a
  `vglSwapBuffers`: vitaGL actualiza y dibuja el diálogo del sistema mientras
  está abierto. La comprobación visual del teclado nativo queda pendiente
  de una Vita física.
- La ausencia del save también activa el registro aunque quede una marca
  antigua. Se comprueba antes de cargar la biblioteca: `NativeInit` escribe
  un save por defecto y comprobarlo después ocultaría el borrado original.
- VPK corregido probado con save borrado y marca antigua: registro, apertura
  de IME, escritura de `VitaFixed`, Submit, OK del juego, callback local tipo
  11, guardado y entrada en la ayuda inicial. Al reiniciar no vuelve a pedir
  registro. El log confirma la llamada a `sceCommonDialogUpdate` (Vita3K la
  declara no implementada y usa su overlay). Cierres de sesión con salida 0.
  Compilación VitaSDK, suite de host ASan/UBSan y validación de VPK correctas.
  Se conservan logs y save de prueba en la carpeta de diagnóstico y se
  restauran save, marca, opciones y replay previos de la copia portable.

## Preparación de v1.0-rc1 (2026-10-16)

- Repo de publicación: `frangarcj/sf4ce_vita`, público; release preparada como
  borrador y prerelease porque sigue pendiente validar una Vita física.
- El menú principal iniciaba su replay de fondo tras unos segundos y entraba
  en una carga incremental con lecturas inválidas (`NLD_ModeSelectReplayLoadLoop`
  → `NLD_GameMainLoadLoop`). `menu_background.h` comprueba cinco instrucciones
  y desvía sólo ese updater a su epílogo; conserva el menú estático y modos
  locales. Prueba ASan/UBSan: rama/destino y rechazo de firmas incompatibles.
- Se conservaron las modificaciones existentes del checkout hermano Vita3K.
  Se integró sólo ETC1 de https://github.com/Vita3K/Vita3K/pull/3903, revisión
  `7606ed4e85402aaa62d1aa0dadccbc5eb3de5007`. La primera pasada aplicó ETC1;
  se añadió después base-vertex adaptándolo a los cambios locales de clipping
  y rebasing de índices. Se omitió la sobrecarga de formato 5551 ajena a ETC1.
  Compilación local correcta. Se corrigió además la conversión de palabras
  ETC1 entre el orden de bytes GXM y el stream ETC2 y el orden de bloques antes
  de descomprimir: la PR original evitaba el cierre pero dejaba el fondo corrupto.
  No se han publicado ni enviado cambios a la PR desde esta tarea.
  La conversión ETC1 temporal del loader se retiró: el port usa ETC1 nativo.
- La ocultación de botones táctiles conserva su dibujo original al configurar
  el panel táctil (`ST_PadSettingSystem` y `ST_PadSettingToPauseSystem`); sigue
  ocultándolo durante el combate normal. Se redirigen imports PLT sin pisar
  las funciones de dibujo que necesita esa configuración.
- Icono y LiveArea eran placeholders vacíos y de tamaño incorrecto. Se crean
  SVG originales, PNG indexados de tamaños Vita y XML real de LiveArea a5.
  Los gráficos de `sce_sys`/`artwork` no contienen imágenes del APK.
- Como referencia de estructura se revisaron README, CMake, LiveArea y workflow
  de `ff3_vita`. Se añade CI con VitaSDK, dependencias fijadas, pruebas de host
  con sanitizers y comprobación estricta del contenido del VPK.
- El VPK permite sólo loader, SFO, cuatro imágenes Vita, XML y licencias.
  Se rechazan entradas extra y formatos/tamaños de PNG incorrectos.
  APK/XAPK/OBB/so/obm/ckb/cks/mp4 y referencias visuales locales quedan ignorados.
  Los datos extraídos del APK siguen excluidos. Por petición posterior del
  usuario, se publican tres capturas del port (combate, salto y lista de
  comandos) en `docs/releases/v1.0-rc1/` para la issue de VitaDB #103.
- `DebugReplay` acepta comandos añadidos al archivo al llegar a EOF, sólo con
  `debug_input_replay=1`. Permite recorrer menús en el guest sin eventos de
  entrada del escritorio. Probado con duraciones, EOF y comandos inválidos.

- CI de GitHub del commit `185951479cf73e8901f8e817987d6a869a1d6574`: pruebas
  de host y compilación/VPK correctas. Se descarga y prueba el propio VPK de CI,
  no se sustituye por otro binario local. SHA256 del paquete de release:
  `2550c671c44666d9e4a30ec07c32744c04f3d26b89280a177d7402ec98891389`.
- Capturas de QA: `release-command-list.png` (publicada después para VitaDB) y
  `release-touch-config.png` (local). Lista de comandos, selección de personaje,
  detalles y configuración de botones dibujan bien con la versión del emulador
  corregida. La configuración conserva la interfaz táctil; no redefine el mando
  físico. La memoria protegida del emulador aún registra avisos recuperados.

- Validación final del VPK descargado de CI: 300 s de navegación por Main,
  Other, Options, lista de comandos/selección/detalles y configuración táctil
  (incluido diálogo de guardar y regreso). Otra sesión de 200 s recorre la
  intro con audio, Start para saltar, error de Record con retorno y combate
  Honda/Sagat. Se observa ~60 presentaciones y ~20 updates/s en combate;
  sin overlay táctil residual. Ambas sesiones finalizan con salida 0.
- Record depende de red y muestra Connection Error. El botón de retorno del
  diálogo permite continuar al modo local; no se presenta como una función
  offline soportada. No se certifica cada opción de servicios online.
- Se restauran las opciones/replay de la instalación portable de prueba. Los
  saves y recursos originales fuera de esa copia no se modifican. Logs y
  resultados locales: `release-final-results.json` en la carpeta de diagnóstico
  de esta pasada. Captura local final: `release-ci-combat.png`.
- El borrador GitHub contiene `SF4CE.vpk` (1.268.095 bytes) y `SHA256SUMS`.
  Se descargó otra vez desde GitHub y coincide byte a byte con el VPK de CI
  probado. El contenido publicado del repo fue auditado sin archivos del APK.
  La revisión posterior al commit de CI sólo cambia documentación.

## Intro original y negro al dejar el título inactivo (2026-10-16)

Se reprodujo el fallo sin pulsar nada: título normal a los 15 s y casi negro
persistente desde unos 35 s hasta los 180 s, con la lógica aún activa.
`AppMain::ST_TitleSystem` inicia el fade y borra dos etiquetas en el tick 600
antes de preguntar `isMovieExist`. Su ruta de vídeo ausente sólo reinicia el
contador; no rehace las etiquetas ni hace fade-in. No era una suspensión.

- El archivo original existe en `demand_assets/sf4_online.mp4`: 143.059.146
  bytes, H.264 Main, 960×540, 30000/1001 fps; AAC LC estéreo 44.100 Hz,
  duración 185,819 s. Es un recurso del usuario y no entra en el VPK.
- `loader/movie_player.cpp` implementa `MoviePlay`, `isMovieExist` e
  `isPlayMovie` mediante SceAvPlayer. Suspende el bucle del juego mientras
  reproduce, igual que la actividad de vídeo Android, y deja que `MovieObj`
  ejecute su transición original al título al terminar o saltar la intro.
- Tocar la pantalla, Cross, Circle o Start salta la reproducción.
  `intro_movie=0` la desactiva; `audio=0` también silencia el vídeo, drenando
  y temporizando el audio para no acumular paquetes.
- Reserva 32 MiB de memoria físicamente contigua para el decodificador y
  64 KiB para el dibujo inmediato de la pantalla de vídeo. Cinco texturas
  apuntan a los frames NV12 sin copiar/conversión CPU; conservan sus propias
  asignaciones pequeñas y las restauran antes de borrarlas. Las dimensiones
  visibles se leen de `moov/trak/tkhd` con límites de tamaño y profundidad: se
  recortan las cuatro filas de relleno que devuelve el decodificador (544)
  para mostrar las 540 filas originales sin basura en el borde. Se espera a GPU
  y al hilo de audio antes de cerrar el reproductor.
- El audio del juego libera únicamente su propio puerto durante la película;
  el hilo de vídeo usa un puerto BGM propio y después se recupera el mixer.
  Las llamadas de extracción de audio/vídeo se serializan.
- Sin archivo/backend, `loader/title_idle_movie.h` valida cinco instrucciones
  ARM y convierte sólo la rama condicional de `ST_TitleSystem+0x440` en una
  rama al mismo destino de título/input. También se instala si falla abrir
  o decodificar. No modifica fades globales ni rutinas de suspensión.
- Prueba de fallback en Vita3K: título visible e idéntico tras 170 s inactivo,
  después entrada en combate Honda/Sagat y salida limpia del emulador.
  ASan/UBSan comprueba firma, destino de rama, backend presente y rechazo
  sin escrituras ante código incompatible. El parser de dimensiones tiene
  pruebas ASan/UBSan de tkhd v0/v1, tracks de audio, tamaños de caja de 64 bits,
  truncamientos, desbordamiento y recorte de superficie; también lee el MP4 real.


- Prueba de reproducción con el firmware AVPlayer de Vita3K (configuración
  normal, sin forzar HLE): intro completa hasta EOF en 186,321 s incluyendo
  apertura, 8.005 bloques AAC decodificados (7.983 con señal), título visible
  después y mixer Cricket reiniciado. La captura del audio posterior dura
  20,83 s, RMS 1.608,66, pico 13.573: se recupera señal, no sólo el hilo.
- Prueba de Start: intro saltada a los 30,540 s; regreso al título, navegación
  y combate Honda/Sagat con ~60 presentaciones/s y ~20 updates/s. Cierre de
  ambas sesiones mediante SIGTERM con salida 0 y transición `Stopping -> Idle`.
- La misma versión con `audio=0` reproduce y drena la intro sin abrir puerto
  de sonido; Start la salta a los 30,641 s y recupera el título visible.
  Salida limpia 0. Opciones portable originales restauradas al terminar.
- Binario de esta pasada: eboot SHA256
  `bf8d686fd12e88a18ae937488a195039f4bdab15dc936b17576f721301a5c427`;
  VPK SHA256
  `4a44a0cca58ec59fb71b42966078a6051beaef2732635304611373e054200cc9`.
  El eboot instalado en la copia portable coincide con el incluido en el VPK.
  VPK validado: nueve entradas, SELF/SFO/XML correctos, sin bibliotecas,
  assets o MP4 propietarios. Evidencias: `/private/tmp/sf4ce-idle-20261016/`;
  capturas `build/captures/intro-vita3k.png`, `title-after-intro.png`,
  `title-after-movie-skip.png` y `title-idle-fallback-170s.png`.
  Sigue pendiente validar decodificador, memoria, latencia y controles en
  hardware Vita; el escritorio bloqueado impide la comparación de ventana.

## Comparación de versiones locales (2026-10-15)

- La biblioteca usada por el port es ARMv7 de 1.05.00, build ID
  `520fc2ea2df898a682742a3f9ba7a22f185fa9e7`.
- `../sf4ce/SF-IV-CE-v1-08-02-mod.apk` contiene únicamente
  `lib/arm64-v8a/libGltest2jni.so`; ese archivo no sirve como sustitución en
  este loader ARM32. No se ha usado ni instalado para las pruebas del port.
- Su `AppMain::SetDefaultAnimationInterval` carga el double ubicado en
  VA `0x172178`: `0.05000000074505806` s, también unos 20 Hz. Se comprobó
  en el ELF AArch64, no a partir del nombre del APK. Este resultado describe
  esa copia local modificada; no certifica todas las distribuciones 1.08.02.
- Existe una edición oficial de Netflix para Android/iOS:
  https://www.netflix.com/tudum/articles/street-fighter-4-mobile-game-guide
  No se dispone de su biblioteca aquí: quedan sin verificar ARM32, cadencia,
  recursos y compatibilidad con el bridge. No hay evidencia aún de que mejore
  el rendimiento o la calidad gráfica de esta base.

## Cadencia nativa y renderizado interpolado (2026-10-15)

El candidato actual elimina el temporizador fijo del loader y permite dibujar
el combate normal a 60 Hz, conservando la cadencia de simulación nativa.

- Evidencia local: `GlTest2Renderer.java` arranca con 50 ms, pero implementa
  `setAnimationInterval(int)` y `FrameIntervalSet(int)`. El port ignoraba esas
  llamadas. `AppMain::SetDefaultAnimationInterval` solicita 0,05 s. En las
  pruebas aparecen peticiones de 33 ms, 5 ms durante cargas y 50 ms en combate.
  La conversión conserva los milisegundos enteros de Java; no se fuerza la
  lógica de combate a 60 actualizaciones por segundo.
- `loader/frame_timing.h` usa plazos absolutos y una cadencia racional de
  presentación: evita acumular el retraso de despertar que dejaba unos
  18,1 updates/s con el temporizador anterior. Una carga larga descarta deuda
  antigua; los cambios de intervalo restablecen el siguiente plazo.
- `smooth_rendering=1` activa un objetivo de 60 presentaciones/s en combate
  normal. El JNI NativeUpdate conserva sus operaciones de framebuffer y
  composición en cada presentación; ocho redirecciones PLT omiten lógica,
  touch, controlador, red, texto y CkUpdate en los frames intermedios.
  Dibujar sólo AppMain::DrawLoop dejaba incompleto ese recorrido gráfico.
- `loader/render_interpolation.cpp` interpola geometría float3 compatible y
  matrices de cámara. No crea nuevas poses ni mezcla imágenes del atlas:
  cambios de índices, UV, textura o contenido de textura descartan historial.
  También lo descartan cortes, giros discontinuos, pausas y cámaras especiales.
  Se reevalúa el estado después de SystemLoop para cubrir transiciones en
  ese mismo tick. Requiere el build ID soportado; otro binario conserva la
  cadencia nativa.
- Se omiten VBO y atributos desactivados, se limita el número de vértices y
  draws, y se restaura el estado GL después de cada draw interpolado.
  Presupuesto máximo: 4 MiB de buffers; E.Honda/Sagat usó unos 21 KiB.
- Coste: más trabajo gráfico y hasta un tick de latencia visual (50 ms en
  combate). `smooth_rendering=0` conserva el dibujo a cadencia nativa con el
  temporizador corregido. La lógica y las poses originales siguen su
  cadencia; 60 presentaciones no significa lógica de combate a 60 Hz.
- Prueba del eboot final con composición completa: 60,00 presentaciones/s y
  19,99 actualizaciones/s en unos 60 s de ventanas de combate estable
  (59,97–60,02 presentaciones/s entre ventanas).
  Los 72 puntos X/Y registrados de los saltos coinciden con la ejecución a
  cadencia nativa. Se comprobaron ataques, saltos, pausa, vuelta al combate y
  desplazamientos; la captura posterior a pausa ya muestra el combate actual.
  Las capturas intensivas se usan como QA visual, no como benchmark aislado.
- Pruebas de host con ASan/UBSan: cadencia exacta 60/20, jitter, cargas,
  unidades JNI, geometría intermedia distinta, restauración de matrices,
  cambios de atlas/índices/textura, VBO, límites y presupuesto de memoria.
  `debug_timing=1` imprime cada 5 s ambas cadencias, coste e historial.
- En Vita3K, las capturas tomadas en el mismo segundo ahora reciben un sufijo
  único para no sobrescribirse. Se conservan los cambios previos del checkout.
- Binario de la pasada de timing (anterior a la intro): eboot SHA256
  `9d529538ea9339aea57163885d61f9b2a1aa8642da3e79595614519c5ddb5afa`.
  Diagnósticos: `/private/tmp/sf4ce-timing-20261015/`, incluido
  `release-timing-results.json`. El mismo eboot arrancó y cerró limpiamente
  con audio/cámara/suavizado/MSAA desactivados; al terminar se restauraron
  las opciones portable previas y no quedó ningún proceso de prueba. Capturas: `build/captures/combat-60fps.png`
  y `build/captures/jump-60fps.png`; el vídeo corto de QA se capturó a 30 fps.

Sigue pendiente validar rendimiento y latencia en Vita real, todos los
escenarios/cinemáticas y el negro de la ventana del emulador con el escritorio
accesible. Esta prueba verifica el framebuffer interno; no sustituye esa
comparación de ventana.

## Candidato de release: segunda pasada (2026-10-15)

Se han implementado audio, corrección de uploads dinámicos, verificación del
pack y encuadre adaptativo. El VPK sigue siendo un candidato de prueba; falta
validación en Vita real y una pasada completa de escenarios y cinemáticas.

### Audio

- `loader/audio_bridge.cpp` conserva el mezclador/decodificadores Cricket
  integrados en la biblioteca del usuario. Sustituye la inicialización de
  plataforma Android y la salida por PCM estéreo Vita a 44.100 Hz, bloques
  de 1.024 frames y conversión float a S16 con saturación.
- Usa el modo polling de `GraphOutputDummy` con un worker Vita propio.
  Copia su vtable incluyendo cabeceras RTTI y sustituye únicamente start/stop;
  no pisa funciones Thumb de dos bytes. Los getters de directorios se
  redirigen mediante sus entradas PLT. `CkSuspend`/`CkResume` mantienen sus
  rutas nativas; el cierre detiene el worker antes de destruir el grafo.
- `audio=1` es el valor por defecto. La instalación exige el build ID
  `520fc2ea2df898a682742a3f9ba7a22f185fa9e7`; otra biblioteca o una
  inicialización fallida conserva el fallback silenciado.
- Se completó AttachCurrentThread/DetachCurrentThread del JavaVM de
  compatibilidad para los workers nativos. El sample type se fija a float y
  se habilita NEON, disponible en Cortex-A9. El presupuesto temporal de
  Cricket corresponde al bloque de 1.024 frames.
- Pruebas Vita3K: PCM no nulo, música, efectos y muestras de voz resueltas.
  `SoundObj::FileLoad` confirma el slot final; no quedaron muestras
  `unavailable` en los combates comprobados. Los mensajes nativos de búsqueda
  fallida en common/primer banco no implican fallo si el segundo banco resuelve
  la muestra. Captura final: `build/captures/release-audio-preview.wav`.
- `debug_audio_capture=1` guarda hasta 180 s de WAV en los datos del port;
  por defecto está apagado. Pendiente escuchar/medir latencia en hardware.

### Texturas

- Sustituida la conversión empírica 16->16 por RGBA5551 GLES -> RGBA8
  explícito, tanto en `glTexImage2D` como en `glTexSubImage2D`.
  La reserva inicial usa RGBA8 incluso si los datos son NULL, de modo que
  las actualizaciones posteriores conservan el mismo formato.
- La ruta inicial rápida de vitaGL y su conversor de subimagen no consumen
  los bits de la misma forma; reajustar ambos antes de llamar a vitaGL
  corrompía el segundo caso. Se respeta UNPACK_ALIGNMENT de la fuente y se
  restaura el estado después de subir el buffer convertido.
- Corregida también la lectura de cuatro bytes del log de un upload que
  podía contener sólo uno o dos bytes.
- Prueba con ASan/UBSan: los 65.536 píxeles posibles, alpha exacto,
  conservación de canales y filas impares con alineaciones 1/2/4/8.
  Vita3K muestra selección/VS sin el fondo magenta/verde anterior.
- Coste: estas texturas ocupan cuatro bytes por píxel en lugar de dos.
  Medir memoria/rendimiento en Vita antes de una release final.

### Personajes y entradas de prueba

- `HasCompleteLocalAssetPack` revisa los 1.985 archivos de combate de los
  32 personajes en el manifiesto soportado, además de los sentinelas del pack.
  Se excluyen terceros colores antiguos/opcionales. Sólo entonces se refleja
  el estado de contenido instalado, sin guardar compras ficticias.
- `getCanUseCharaList(..., true)` devuelve 32. Se han comprobado personajes
  fuera del grupo inicial, incluyendo Gouken, Sagat y Dan contra Blanka, y
  Akuma contra Sagat. También se confirmó E.Honda contra Sagat con colores
  correctos, voces resueltas y saltos completos dentro del framebuffer.
  La nota anterior de sólo cuatro seleccionables queda superada.
- Seleccionar un personaje nuevo requiere tocar su retrato y volver a
  tocarlo para confirmar; después se elige color. Los ejemplos anteriores
  de Ryu/Ken partían de una selección que ya estaba activa.
- `debug_input_replay=1` consume `debug-input.txt` a la cadencia normal del
  juego, con `wait`, `tap` y `pad`. Permite integración reproducible mientras
  el escritorio está bloqueado; no envía eventos al sistema anfitrión.
  Ejemplos en `tests/replays/`; todas las opciones de debug están apagadas
  por defecto. Las muestras de pad fallidas ahora dejan sticks centrados
  en 128 en vez de introducir una dirección falsa.

### Cámara

- Base: 466 BAM (40,96 grados), altura 124 Q12, distancia Android 170 intacta.
  Se conservan las cinco firmas y los cuatro cambios del primer candidato.
- En combate normal, la perspectiva abre hasta 600 BAM (52,73 grados)
  según la altura Q16 de ambos luchadores. Si la separación supera 340
  unidades, recupera gradualmente el FOV original de 45 grados hasta 680
  unidades. La apertura es más rápida que el cierre.
- La redirección se limita a la matriz de cámara del combate. Fuera del
  estado normal se reenvía la perspectiva solicitada por el motor, preservando
  las cámaras nativas de cinemáticas. Queda una pasada exhaustiva de especiales.
- Pruebas: reposo, movimiento, ataques, saltos repetidos y separación.
  Las primeras pruebas adaptativas de 550 BAM aún dejaban poco margen bajo
  los pies; el candidato actual amplía a 600. Capturas finales:
  `build/captures/release-combat.png` y `build/captures/release-jump.png`.

### Vita3K y empaquetado

- En `../Vita3k`, Qt no atendía SDL_EVENT_QUIT, que SDL emite al recibir
  SIGINT/SIGTERM. Ahora detiene la sesión y sale: no deja una instancia
  antigua viva interfiriendo con pruebas posteriores.
- Esa prueba reveló una carrera adicional: el worker borraba su entrada del
  mapa de hilos antes de liberar su última referencia a ThreadState. El
  anfitrión podía desmontar memoria mientras se destruían CPU/stack/TLS.
  Ahora libera esa referencia bajo el mutex antes de avisar al cierre.
- Se confirmó cierre Running -> Stopping -> Idle con salida del proceso.
  El negro inicial de ventana se seguirá distinguiendo de fades y de una
  captura interna correcta. El Mac está bloqueado: falta repetir la
  comparación ventana/framebuffer tras desbloquearlo.
- Los cambios del emulador conservan las modificaciones previas del usuario;
  además siguen los dos exports kubridge añadidos en la primera sesión.
- `antialiasing=0/1/2` ya controla MSAA real. Se retiraron del ejemplo README
  bilinear/postfx, que aún no tienen implementación. `debug_log=0` reduce
  trazas de assets, GL/JNI y touch sin ocultar diagnósticos de arranque.
- XML LiveArea corregido y validado. El VPK incluye LICENSE y avisos, y
  `tools/check_package.py` comprueba SELF, TITLE_ID, XML, rutas y ausencia de
  bibliotecas/assets propietarios. `tools/run_vita3k_smoke.py` requiere que
  una instancia de prueba termine por SIGTERM con código 0 y limpieza de sesión.
- Compilación Vita y pruebas de host con ASan/UBSan completadas. eboot del
  candidato: `515cdb62b5658858de57f6a9c25055ca3701c4cc747ca05370f5b4cf03ad5733`.
- El mismo eboot también arranca hasta el título con `audio=0`,
  `battle_camera=0` y `antialiasing=0`, y cierra limpiamente la sesión.
  Se restauraron las opciones portable previas después de la prueba.
- Registros y copias de esta pasada: `/private/tmp/sf4ce-release-20261015/`.
  La instalación original y sus guardados no se han reemplazado; se usa la
  copia portable aislada. La configuración global portable queda restaurada.

## Primer candidato de encuadre (2026-10-15)

El primer candidato sustituía los experimentos de distancia 60/FOV 55 descritos
más abajo por un encuadre que conserva la distancia Android original (170) y el
seguimiento dinámico, estrecha el FOV a 466 BAM (40,96 grados) y eleva el centro
vertical de 80 a 124 unidades. El objetivo es aumentar el tamaño de los
luchadores y bajar la línea de los pies sin acercar la cámara a los modelos.
Ampliar el FOV no corrige las diferencias de escala debidas a la profundidad;
a igual posición de cámara sólo reduce el tamaño de toda la escena.

- Implementación: `loader/battle_camera.h`. Se verifican las cinco firmas
  antes de escribir; una versión distinta no recibe modificaciones parciales
  por un fallo de firma. Las dos llamadas de perspectiva usan el mismo FOV.
- Se confirmó mediante desensamblado otra restauración de Y=80 en
  `CameraViewSet(bool) + 0x180`, VA `0x52ba54`, aplicada en modo 10. Ahora usa
  la misma altura que `BattleCameraInit + 0x258`.
- `battle_camera=1` en `options.cfg` activa este candidato (valor por defecto);
  `battle_camera=0` conserva la cámara Android original. Menús y HUD conservan
  sus coordenadas originales.
- Prueba de host: `tests/battle_camera_test.cpp`, ejecutada con Clang y
  AddressSanitizer/UndefinedBehaviorSanitizer. Comprueba instrucciones ARM,
  los cuatro cambios exactos, distancia conservada y rechazo sin escrituras
  cuando falla cualquiera de las cinco firmas, faltan funciones o son Thumb.
- Las cinco firmas también se contrastaron con la biblioteca Android local.
- Build Vita del loader: completado, VPK y eboot generados. También se probó
  el arranque hasta el título con `battle_camera=0`; el log selecciona la
  cámara Android original. La instancia final queda con `battle_camera=1`.
- Prueba en `../Vita3k/build/macos/bin/Vita3K.app`: el primer arranque descubrió
  imports ausentes `kuKernelAllocMemBlock` (NID `0x2ef7c290`) y
  `kuKernelCpuUnrestrictedMemcpy` (`0x91d9cabc`). Se añadieron en ese checkout
  dos wrappers HLE hacia las implementaciones Sysmem existentes, y sus NID.
  El emulador recompilado carga correctamente RX en `0x97000000`/`0x98000000`,
  resuelve los imports y llega al título. El log del candidato confirma
  `[Camera] console framing: FOV=40.96 height=124 distance=170`.
- La prueba usa una aplicación y copias de los guardados en el filesystem
  portable de ese build; assets y bibliotecas enlazan los datos existentes.
  No se reemplazó el eboot de la instalación de `/Applications/Vita3K.app`.
- Prueba visual completada en Training, Ryu contra Ken: desplazamiento,
  golpes y salto. Capturas reales en `build/captures/combat-framing.png` y
  `build/captures/combat-jump.png`. En reposo el tamaño y la línea de pies
  se acercan a la referencia PS3; en el ápice del salto las manos de Ryu
  rozan/salen del borde superior. Pendiente ajustar ese caso, probar extremos
  de escenario y cámaras de especiales antes de considerar final el encuadre.
- eboot candidato probado SHA-256:
  `e788524713736ae37e18a8021ea04a2644a999fc4ca6bc23e8e15c91287284c5`.
- Logs y copias previas: `/private/tmp/sf4ce-viewport-20261015/`.

### Ruta de prueba de combate y capturas

El acceso sí se completó en Training con Ryu contra Ken (2026-10-15), se
comprobaron desplazamiento y golpes. El primer candidato de 35 grados/Y=100
mostró luchadores demasiado grandes; se abrió a 40,96 grados/Y=124 para la
segunda comparación con la captura de PS3.

Cuando los clics de Computer Use fallen con `AXError.notImplemented`, el
loader admite `debug_touch_cursor=1` en `options.cfg` (apagado por defecto):
Select + D-pad mueve un puntero desde (480,272) a 8 píxeles por update;
Select + Cross genera un toque frontal. Durante Select se bloquea el envío
de esos botones al mando Android. No necesita modificar los datos del juego.

Para esta sesión, una copia de configuración en
`/private/tmp/sf4ce-viewport-20261015/combat-test.yml` usa Select primario ShiftLeft,
alternativo KeyA (el modificador sintetizado por Computer Use llega como KeyA
en este Qt/macOS), y captura F12 con PNG. Se controla mediante
`vita.pressKey("shift+Right", {durationMs: ...})`, `shift+Down`, `shift+x`.
El título sólo avanza mediante un toque; Cross/Start de mando no bastan.

Ruta en coordenadas del framebuffer 960x544:

1. Título: tocar aproximadamente (480,272); esperar la carga del menú.
2. Main Menu: Solo Play, aproximadamente (700,480).
3. Solo Play: Training, aproximadamente (700,480).
4. Personaje 1: Ryu, aproximadamente (320,328); color 1 en (190,430).
5. Personaje 2: Ken, aproximadamente (315,430); color 1 en (680,430).
6. Training carga automáticamente la sala de entrenamiento, sin selección de
   escenario. Esperar a que acabe la pantalla de movimientos/Now Loading.
7. Soltar Select: las flechas y X ya actúan como mando de combate.
8. F12 guarda el framebuffer real en
   `../Vita3k/build/macos/bin/portable/screenshots/Street Fighter IV CE/`.

En la prueba de diagnóstico se encontró una instancia anterior de Vita3K que
no terminó con SIGTERM. Dos procesos del mismo bundle coexistían y Computer
Use seleccionaba la ventana congelada, mientras la captura interna pertenecía
a la instancia nueva. Esto explica esa observación, pero no permite asegurar
que todos los negros iniciales anteriores tuvieran la misma causa. Tras cerrar el proceso anterior y volver a seleccionar
el bundle, el título se presenta correctamente. No se aplicó un parche de
alpha: la función de captura fuerza alpha=255 y ese dato del PNG no permite
inferir el alpha de la superficie original. La sincronización de superficies
se descartó como corrección. Sigue pendiente revisar el cierre de Vita3K;
para repetir pruebas hay que confirmar que el proceso anterior haya salido.
Las trazas temporales añadidas al renderer se retiraron. Un arranque limpio
con la sincronización original (`disable-surface-sync=true`) volvió a mostrar
el título correctamente; la configuración portable global quedó restaurada.
La prueba final usa `--keep-config --load-config -c <combat-test.yml> -r SF4CE0000`.
Se volvió a llegar a combate Ryu/Ken con la única instancia activa y la
ventana mostró el encuadre correctamente. Después macOS bloqueó la sesión
y Computer Use dejó de permitir input; las capturas de combate anteriores
y los logs quedaron guardados.

## Revisión para release (2026-10-15)

Esta revisión inicial detectó los problemas abordados en la segunda pasada
resumida al principio del documento. La validación en Vita real sigue pendiente,
así como la cobertura completa de escenarios y cinemáticas.

Se corrigieron dos problemas de preparación: README omitía el runtime Android
`libc++_shared.so` y documentaba softfp aunque CMake compila con hardfp; además,
el XML de LiveArea cerraba `subtitle` como `subtittle` y no era XML válido.
La plantilla corregida se verificó con un parser XML.

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
- El combate solicita una actualización cada 50 ms. La lógica se conserva a
  esa cadencia y el renderizado interpolado permite 60 presentaciones/s;
  véase la sección de cadencia al principio de este documento.
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

- Histórico: se observaron sólo cuatro personajes seleccionables. La pasada
  actual verifica el pack completo, obtiene 32 en la lista nativa y ha cargado
  varios personajes adicionales. Véase la segunda pasada al inicio.
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
- La cámara de combate está claramente más alta/atrás que en la versión PS3
  (mucho más suelo visible, mucho más aire sobre las cabezas de los
  personajes), comparando capturas de ambas versiones. Los luchadores son
  sprites 2D; el fondo sí usa una cámara 3D real con perspectiva.

  Investigación (2026-08-15): el `.so` Android conserva símbolos C++ sin
  eliminar para las funciones de cámara (`BattleCameraInit()`,
  `RequestResetCamera()`, `CameraViewSet(bool)`, `CameraMoveCheck()`,
  `CameraDirectionSet()`, etc.). Se descartó que la diferencia venga de la
  resolución que reporta el loader: `GlTest2Renderer.onSurfaceChanged` está
  vacío en el Java decompilado y el resultado de `getDisplaySize` se
  descarta sin usar, así que el nativo no tiene ningún canal para conocer el
  tamaño real de pantalla en tiempo de ejecución.

  `BattleCameraInit()` (VA `0x52a558`, 608 bytes) fija la pose base de
  cámara de cada combate: un punto de mira en `(X por stage, Y, 0)` y un
  offset de cámara `(0, 0, Z)` respecto a ese punto, ambos como literales
  Q12 (fixed-point ÷4096) en el propio pool de literales de la función:
  `Y=80.0` en VA `0x52a7b0` (offset `+0x258` desde el inicio de la función)
  y `Z=170.0` (distancia) en VA `0x52a7ac` (`+0x254`). El FOV es una
  constante independiente, 512 en unidades BAM (4096 por círculo) = 45°,
  usada tanto en `BattleCameraInit` como en `CameraDirectionSet`.
  `RequestResetCamera()` y el constructor de `_CAMERA_OFFSET_CONTROL` son
  callejones sin salida (sólo tocan un flag/tabla o inicializan a cero).
  `CameraMoveCheck()` sólo perturba la posición de cámara resultante (temblor,
  clamp contra paredes) por encima de esta pose base; no lee ni escribe los
  dos literales anteriores. No se cerró del todo cómo se calcula la posición
  final de cámara por frame a partir de target+distancia+dirección (haría
  falta un descompilador para confirmarlo con certeza), pero `Y=80.0` es
  claramente el ancla vertical común a todo el sistema.

  Primer experimento (2026-08-15): reducir el literal `Y` de 80.0 a 40.0
  (mitad), sin tocar `Z`. Probado en Vita3K contra `android_test.png`: sí
  redujo el aire sobre las cabezas (Ryu y Abel quedan con la cabeza casi
  pegada a la barra de vida), pero aumentó muchísimo el suelo visible en
  primer plano — la cámara se inclinó más hacia abajo en vez de acercarse.
  Conclusión: `Y` alimenta la altura del punto de mira y por tanto el
  ángulo de cabeceo (pitch) de la cámara, no una "altura" independiente del
  ángulo; bajarlo aleja el punto de mira del ojo en el eje vertical y la
  cámara cabecea más hacia el suelo. Se descartó este cambio.

  Segundo experimento (2026-08-15): en vez de `Y`, se reduce `Z` (la
  distancia, offset `+0x254`) de 170.0 a 85.0 (mitad), dejando `Y` en su
  valor original. La idea es un "dolly in" puro a lo largo de la misma
  dirección de vista: no debería cambiar el ángulo de cabeceo, sólo acercar
  la cámara para que los personajes ocupen más frame, reduciendo aire y
  suelo proporcionalmente. Confirmado en Vita3K (`android_test2.png`): el
  ángulo se mantuvo nivelado, aire y suelo se redujeron, encuadre bastante
  más parecido a PS3. Sigue sobrando algo de aire/suelo respecto a la
  referencia.

  Tercer experimento (2026-08-15): segunda pasada más suave en la misma
  dirección, `Z` de 170.0 (original) a 60.0. Confirmado en Vita3K
  (`android_test3.png`): encuadre bastante más parecido a PS3, pero a esa
  distancia aparece distorsión de perspectiva en movimientos con
  extremidades extendidas (el puño/brazo de Abel se ve exagerado) — el
  clásico artefacto de estar demasiado cerca para el ángulo de visión (FOV)
  fijo de la cámara.

  Cuarto experimento (2026-08-15, instalado): en vez de seguir acercando la
  cámara, se ensancha el FOV para alejarla ópticamente sin tocar su
  distancia física. El FOV es la constante independiente `512` en unidades
  BAM (4096 por círculo = 45°), usada dos veces: `mov r1, #512` en
  `BattleCameraInit` (VA `0x52a614`, offset `+0xbc`, codificación de
  inmediato rotado de 8 bits) y `movw r1, #0x200` en `CameraDirectionSet`
  (VA `0x52ab40`, offset `+0x74`, inmediato de 16 bits directo) — misma
  constante, dos codificaciones ARM distintas porque `mov` con rotación no
  puede representar cualquier valor arbitrario. Las dos instrucciones deben
  llevar el mismo valor o la llamada de cada frame en `CameraDirectionSet`
  pelea contra la de `BattleCameraInit`. Se sustituye la instrucción `mov`
  por una `movw` equivalente (mismo tamaño, mismo efecto, igual que ya hace
  el compilador en el otro sitio) para poder codificar libremente el nuevo
  valor. Primer valor: 45° -> ~55° (512 -> 626 BAM). Implementado en
  `loader/main.cpp` (`WidenBattleCameraFov`), verificando ambas
  instrucciones antes de escribir. Pendiente confirmar visualmente en
  Vita3K si 55° corrige la distorsión sin perder demasiado el encuadre
  ganado con la distancia.
- Audio: el puente Cricket/Vita sustituye el silenciado por defecto; véase la
  segunda pasada. Se conservan stubs para `audio=0` y bibliotecas no soportadas.
- GL/JNI/touch y assets se registran en detalle sólo con `debug_log=1`. Falta
  medir rendimiento y memoria en hardware con el logging por defecto apagado.
- Falta validar en hardware Vita además de Vita3K.

## Orden de trabajo recomendado

1. Probar el candidato en una Vita real: instalación, primera ejecución/IME,
   mando completo, memoria, latencia de audio y rendimiento con debug apagado.
2. Completar cobertura de escenarios, especiales/ultras y luchadores restantes.
   Comparar capturas de reposo, saltos y separación con las referencias.
3. Repetir la comparación ventana/framebuffer en macOS al desbloquear el equipo;
   comprobar que no reaparece un negro persistente con una única instancia.
4. Medir sesiones prolongadas y cargas sucesivas en hardware. Mantener el
   encuadre y audio originales disponibles mediante configuración para A/B.
5. Preparar publicación sólo después de cerrar esas validaciones, conservando
   las licencias/atribuciones y excluyendo bibliotecas y datos propietarios.

## Archivos y comandos útiles

- Loader principal: `loader/main.cpp`
- Compatibilidad Bionic/Ck: `loader/bionic_compat.cpp`
- Resolución/carga ELF: `loader/so_util.c`
- Timing/configuración: `loader/config.h`
- Build: `cmake --build build -j6`
- Ejecutable: `build/eboot.bin`
- SHA-256 del último binario instalado y probado (sin conversión 5551):
  `b11a8c7113617244234f95be646952b067c9df6423bca6db087469c32ddd0252`
- SHA-256 del binario candidato instalado en Vita3K, con alpha en bit 15,
  R/B intercambiados, distancia de cámara reducida de 170.0 a 60.0 y FOV
  ensanchado de 45° a ~55° (cuarto experimento de cámara), pendiente de
  confirmar en combate (2026-08-15):
  `ff34e94195360f2fdf78d56e81156b6c1199e5dfe8661cb9302ddca83e732b82`
  (binarios intermedios descartados: inversión completa con bit de alpha
  movido pero R/B sin intercambiar correctamente, Honda azul,
  `39d45dcbd20af9d3ca88b629346713cd5b4ee17973cc39b9133a7e53f7a12334`; swap
  R/B sin mover alpha, no probado por reintroducir el problema de
  transparencia, `54d81df3b1928ae172323959387fc5d10afa086fa9adc04d6ffcaba8dfac9c3a`;
  colores 5551 corregidos sin tocar cámara,
  `00b143fe5de0664d4834176d815aacf2dc1606a6a10bdb507a741c27afaea747`; cámara
  bajada por altura Y (descartado, exceso de suelo visible),
  `3e8a44f031631bfe637d72f707f796445c5f224c165285c134861765370b6579`;
  distancia 170.0 -> 85.0, mejora confirmada pero aún con margen,
  `066936d9e474367c1f15a973b4c904913ec75ea3ccafa59320c5f31c818088dc`;
  distancia 170.0 -> 60.0, buen encuadre pero distorsión de perspectiva en
  movimientos con extremidades extendidas,
  `bc9d8b72cae423ba17103b94c65db5ba516ba56db6a0269eca736be40fac761d`)
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
