# Prototipo de tiempo variable para JSR Android 1.07

Estado: **integrado como modo experimental en el NRO**, sin interpolacion de
poses. No equivale a haber convertido todos los sistemas del motor a 60 Hz.
Los modulos compartidos estan ahora en `source/jsr_engine_clock.*` y
`source/jsr_engine_fall.*`; los hooks, en `source/jsr_native60.*` y su ensamblador.

## Correccion del crash de entrada al menu (20261020)

Debug21/crash15 alcanza el segundo tutorial y falla en njCnkModPolygon
+0x56 (+0x2571b6), leyendo el segundo indice de un triangulo en 0x21000000.
El contador r10 negativo solo puede salir del bucle original si entro con
cero: la funcion decrementa un contador de 14 bits sin comprobar cero antes
de su primera iteracion. `jsr_render_safety.*` retorna el argumento alpha
original cuando (header & 0x3fff) == 0, sin leer indices ni vertices; conserva
el camino original para todos los lotes no vacios, incluidos headers con
extension de signo. No amplia el arena ni apaga sombras para ocultar el fallo.

`test-render-safety.py` reproduce el fallo en la instruccion exacta del APK,
valida headers vacios sin buffers y ejecuta los lotes no vacios del motor
(39 vertices, cuatro strides, retorno alpha). La disponibilidad del renderer
y sus destinos de vertices son fixtures; no se ha ejecutado la GPU ni el
tutorial completo. Se verifica tambien el nuevo trampolin contra el prefijo
real. El origen del lote vacio sigue sin confirmarse. Los registros de audio
de esta sesion ya contienen PCM no nulo.

El siguiente registro (build 202610201713, debug20/crash14) muestra un fallo
independiente y anterior: la biblioteca se cargo en 0x206b0000, dentro del
arena fijo 0x20000000..0x21000000. El mmap solicitado fallo con 0xd401 y
devolvio memoria alternativa; el motor ignora esa alternativa y copia en
0x20f80000. `jsr_memory_prepare` reserva el rango completo antes de so_load,
excluyendolo tanto de la biblioteca como de los alias temporales. Comprueba
que el rango este libre; el import mmap del port impide continuar si no se
obtiene la direccion fija. `test-memory.py` ejecuta el asignador ARM real con
RNG forzado a la direccion del crash y verifica rechazo, superposiciones,
errores, repeticion y compatibilidad con el camino del emulador. Los SVC son
fixtures: falta el arranque real. Esta correccion no altera GL ni los relojes.

El registro de consola de la compilacion 202610201654 aborta en
`sbGamePlayerColiRideCheck.part.5+0x396`, al recorrer el resultado de colision
en player+0x41c. `jtActionExecColi` libera sus listas de resultados en cada
frame, aunque un actor grabado haya omitido su logica ese frame. Retener el
actor sin volver a registrar sus consultas deja punteros liberados.

`jsr_collision60.*` captura las consultas de rayos, capsulas y objetos JT
durante cada paso logico del actor grabado y las vuelve a registrar en los
frames intermedios. Copia las coordenadas temporales y conserva la matriz
Ninja completa de 64 bytes para las consultas de objetos; no ejecuta de nuevo
la logica, las teclas ni las animaciones. Las listas se regeneran por el motor.
Se invalida la captura al abandonar el modo grabado, cambiar de modo/zona o
perder el foco. Si no puede capturar, ejecuta el callback original en lugar de
usar punteros antiguos: prioriza seguridad sobre sincronizacion bajo OOM.

Tambien se corrige la firma JNI de `init`: el APK declara `(III)V`
(ancho, alto, idioma), pero el port omitia el idioma en la pila. Se pasa
explicitamente 0 (ingles). Su relacion con el silencio no esta confirmada:
el log confirma salida audout operativa pero cero muestras del motor. Si
persiste, el nuevo diagnostico registra mixer, habilitacion, pausa del juego,
video e idioma; no fuerza volumen ni cambia preferencias guardadas.

Pruebas: `test-collision60.py`, `--prove-old-hold-fails`, `test-startup.py`,
`--prove-old-init-fails`, y los trampolines ARM contra prefijos reales del APK.
La liberacion de listas y la entrada JNI se ejecutan con codigo real; las
respuestas geometricas y mainInit son fixtures. Falta prueba de juego completo.

## Enfoque

Se adapta el principio de `follow_frame_time` y `pace_tunables` del archivo
`codboz_hud_handlers.c` recibido: medir tiempo monotono y compensar cada avance
dependiente de actualizacion. No se reutilizan direcciones ni estructuras de COD.

`jsr_engine_clock.c` convierte nanosegundos reales a unidades de actualizacion
originales de 30 Hz. Un frame de 60 Hz representa aproximadamente 0.5 unidades;
uno de 30 Hz representa una. Conserva residuos enteros para futuros contadores,
reinicia al pausar/reanudar o retroceder el reloj y limita recuperacion de cargas
a 100 ms. No invoca simulacion ni modifica posiciones por si solo.

Los adaptadores `jsr_engine_motion_next` y `jsr_engine_motion_next_ex` compensan
el argumento de avance, sin alterar limites, flags o duracion del recurso.
No interpolan poses dibujadas: modifican el avance real de la animacion.
Se instalan 16 hooks atomicos, con firmas de instrucciones de este APK. El modo
`[display] native60=true` tiene prioridad sobre `interpolate`, incluso si las
firmas no coinciden: en ese caso se vuelve a la cadencia original, no a interpolar.

## Integracion experimental

- `jtActionExec` y entrada se procesan por presentacion. El jugador y su camara
  ejecutan sus callbacks cada frame; animacion, acercamientos angulares/lineales,
  aceleracion y giro usan el paso real respecto al tick original de 30 Hz.
- Gravedad de subida/caida se compensa dentro de la rutina de caida. El bloque
  verificado tiene contador propio y correccion de desplazamiento vertical.
- Las cuatro rutas de registro de colision (suelo, salto, grind y wallride)
  reciben velocidades e impulsos escalados antes de construir las consultas.
  Se restauran los parametros de entrada si no los cambio el motor; no se
  interpolan posiciones finales ni se deshacen respuestas de colision.
- Siete contadores identificados del jugador llevan residuos por actor/estado.
  Se compensa el decremento antes del callback para evitar expiraciones en
  frames intermedios. Esto presupone un decremento en esas rutas y necesita
  validacion contra todos los estados reales.
- Los callbacks de acciones en vivo se ejecutan por presentacion, incluidos
  entrada, preparacion de matrices, poses y registro de colisiones. No se omiten
  callbacks completos cuando el acumulador de contadores no cruza un tick.
  Los adaptadores de animacion y acercamiento tambien se aplican en esos callbacks.
  `jtStageUpdate` conserva sus pasos enteros y callbacks originales a 30 Hz.
- Excepcion para las grabaciones de los modos de entrada 2/3/4: el personaje,
  su camara hija y sus comandos comparten un solo reloj de 30 Hz. Sus callbacks
  completos avanzan con los parametros originales, sin volver a escalar gravedad,
  animacion, aceleracion ni colisiones. La entrada manual permanece por frame.
  El render sigue a 60 presentaciones; la evaluacion de clips descrita abajo
  puede aportar poses nuevas entre ticks. Se mantienen los demas
  callbacks de preparacion por presentacion, no se restaura el filtro global.
  La fase se calcula una sola vez por frame; se conserva el flanco de botones
  incluso cuando el jugador se procesa antes que su grabacion.
- No hay cambios de formato GL: profundidad 24 y stencil 8. La variante de
  profundidad 32 que fallo en consola no se utiliza.

La camara solo tiene compensados los pasos que pasan por `jtAngleSupple` y
`jtFloatSupple`; otros suavizados y contadores internos siguen pendientes.
La fisica, incrementos directos y contadores de otros actores no se han
convertido completamente: algunos sistemas por llamada pueden ir demasiado
rapido. Hay que convertirlos en sus puntos de avance, no omitiendo callbacks
enteros que tambien preparan recursos por frame. Probar especialmente enemigos,
objetos, registros de colision, replays, cargas y cambios de estado.

## Regresion del build 202610200407

La prueba en consola reporta T-pose, parpadeo y movimiento solo hacia delante.
El despachador anterior omitio `exGamePlayerKey` y cualquier preparacion de pose
no clasificada como jugador/camara en frames sin tick entero. Ese filtro se
retiro. La prueba de regresion ejecuta el `exGamePlayerKey` real del APK y sus
lectores `_GetAnalogAng`/`_GetAnalogDist`, con proveedores de entrada controlados:
con el filtro viejo reproduce direccion desactualizada; sin el filtro sigue
las ocho direcciones durante 600 frames consecutivos. La preparacion de poses
se comprueba con un callback de prueba por frame, no con un render del juego.
Por tanto, la desaparicion de la T-pose todavia necesita confirmacion en consola.

Tambien se corrige el ABI de `sbGamePlayerGrindSetCM`: consume r1 (lo copia a r6
y usa ese argumento en el giro), pero el wrapper lo descartaba. Ahora se conserva
con el modo experimental activo y tambien en el camino original.

## Personajes de las lecciones en el escenario

El build 202610200701 ejecutaba tambien el consumo de comandos grabados de
`exGamePlayerKey` cada presentacion: los modos 2/3/4 avanzaban el contador de
duracion `+0x78` y el puntero de registros `+0x70` demasiado rapido respecto
a las animaciones/fisicas compensadas. Ahora esos modos usan residuos de
tiempo independientes por objeto. El modo 0 de entrada en vivo sigue por frame.
En frames sin avance del guion se conserva el estado pulsado y se actualiza
el byte de botones previos `+0x74`, para no repetir flancos de salto/spray.
No se salta la preparacion de poses del personaje ni el registro de colisiones.
Se consume como maximo un comando por presentacion y se conserva el credito
pendiente despues de una carga, en vez de descartar comandos intermedios.

`exPractice` tambien incrementaba sus esperas `+0x7c` por llamada: los estados
2/4 superaban el umbral de 90 en la mitad de tiempo. Se compensa solo ese
incremento, conservando el resto del callback por frame y sus transiciones.
Los perfiles de ambas funciones se comprueban antes de instalar ningun hook.
No se modifica la animacion pequena del menu de pausa (`exGameTutorialLesson`).

La prueba ARM ejecuta los modos 2/3/4 reales del APK y compara duraciones,
limites de registros y flancos de botones con el mismo guion a 30 Hz, a
30/40/50/60 presentaciones. Ejecuta tambien los estados de espera reales de
`exPractice`. Los proveedores de entrada, memcpy y busqueda de actores usan
fixtures; no es una prueba visual de la leccion completa en consola.

Compilacion experimental desde la raiz del workspace:

```powershell
$env:JSR_TIMING_MODE = 'native60'
python work/build-audio-native.py
Remove-Item Env:JSR_TIMING_MODE
```

Ese build usa `native60=true` por defecto si no existe la opcion en config.ini.
Con `native60=false`, `interpolate=false` se vuelve a los 30 FPS originales.
No sobrescribir una opcion explicita del usuario ni reutilizar archivos de
configuracion de Citron sin comprobar sus valores.

## Avance: gravedad y contadores por actor

`jsr_engine_fall.c` adapta el bloque de caida de `0x1e604e` a `0x1e6072`.
Actualiza la velocidad con gravedad por tiempo real y conserva el contador
entero mediante un residuo independiente por actor/estado. No usa el pulso
global del reloj para temporizadores que pueden empezar en diferentes frames.
La transicion que pone a cero el campo `+0x208` solo ocurre al expirar.
El integrador debe reiniciar `JsrEngineCountdown` al entrar en un nuevo estado
o reasignar el temporizador, incluso si su nuevo valor coincide con el anterior.

La funcion devuelve el desplazamiento vertical de vuelo libre correspondiente
al paso. Para conservar el comportamiento discreto original de 30 Hz al
subdividirlo, usa `v_final * h + g * h * (h - 1) / 2`, donde `h` es el tiempo
real en unidades de 1/30 s. Dividir solo la gravedad, o multiplicar solo la
velocidad final por `h`, no conserva simultaneamente velocidad y trayectoria.
Esta formula no cubre limites de velocidad, contactos, impulsos o aterrizajes:
la integracion experimental lo aplica a la velocidad de entrada del registro
de colisiones, pero todavia no valida contactos ni respuestas del juego completo.

`test-native60-fall.py` ejecuta el bloque original 180 veces como referencia
en ARM/Unicorn y compara gravedad, vuelo libre, contador y transicion contra
la adaptacion compilada a ARM a 30/40/50/60 Hz. Tambien prueba contadores
desfasados entre actores, expiracion unica, pausa, reinicio y cargas largas.
Pasar estas pruebas no valida el salto completo ni el juego a 60 Hz.

Se reviso ademas `jtStageUpdate`: el bloque `0x1c4596..0x1c4608` incrementa
tiempos globales y por area, decrementa los restantes e invoca callbacks al
llegar a cero. No basta con escalar su contador visible: hay que preservar
todos esos avances y callbacks, sin omitir el resto de la actualizacion.

## Evidencia del APK recibido

Biblioteca `libjetsetradio.so`, SHA256:
`13473abea32209ea7f606525f09c3514b9b2c01656901a0e240ed3de6a7fdf91`

Direcciones relativas a su imagen ELF, no direcciones absolutas en Switch:

| Subsistema | Funcion / instrucciones | Estado |
| --- | --- | --- |
| Reloj Android | `CTimer::update`, `0x23be1c` | Ya calcula tiempo real; no basta para la logica heredada |
| Animacion basica | `jtMotionNextFrame`, `0x1ae354` | Adaptador probado contra la funcion real |
| Animacion con subrango | `jtMotionNextFrameEx`, `0x1ae414` | Adaptador probado contra la funcion real, incluidos argumentos en pila |
| Gravedad | `sbGamePlayerJump_ProcFallBase.isra.0`, bloque `0x1e604e` | Resta gravedad en cada llamada, sin consultar CTimer |
| Contador de caida | `0x1e6062` a `0x1e6070` | Decrementa entero por llamada y puede cambiar un estado al llegar a cero |
| Tiempo de partida | `jtStageUpdate`, desde `0x1c4596` | Hook integrado; pasos y callbacks originales a 30 Hz |
| Temporizador habilitado | `jtStageCountTimer`, `0x1c3710` | Solo habilita el conteo: NO es el avance del reloj |
| Movimiento/giro | Cuatro rutas SetCM, `sbGamePlayerSetTurnRatio` | Adaptadores experimentales integrados; sin escalar posiciones absolutas |

La pose usada por `njSetCamera` incluye parametros de proyeccion y ocupa 40
bytes, mientras que la pose tras la matriz de `njSetView` ocupa 28. La
correccion ya hecha para esa diferencia permanece en el NRO entregado.

## Desfase de las demostraciones en el escenario

Los builds 202610200720/202610200728 limitaban solo los comandos grabados a 30 Hz,
pero ejecutaban el personaje y la camara a 60 Hz. Compensar algunas magnitudes
no conserva todos los historiales de entrada, transiciones y contadores internos
de una grabacion. La ruta conjunta de 30 Hz evita ese cambio del paso fisico.

La regresion `--prove-old-replay-physics-fails` restaura la politica anterior:
reproduce el doble de callbacks del personaje con los mismos comandos. La prueba
positiva compara 60 muestras ordenadas con una referencia de 30 Hz a 30/40/50/60
presentaciones, incluidos botones, historiales, fisica y animacion de fixtures,
la camara asociada y el callback de entrada original del APK. No ejecuta una
leccion completa ni demuestra que el render de los personajes sea correcto.

## Evaluacion nativa de clips grabados por presentacion

`jsr_pose60.c` captura las operaciones `jtMotionCopyMotion` y
`jtMotionCompositionOut` del paso original del personaje grabado, junto con
los avances `jtMotionNextFrame/Ex`. Durante `drGamePlayer` vuelve a evaluar las
operaciones con tiempos fraccionarios de los clips originales, usando el
evaluador del juego. No mezcla matrices ni poses de dos frames dibujados.

La fisica, botones, transiciones y sus contadores siguen en el paso original.
Los buffers de pose se guardan antes de evaluar el dibujo y se restauran al
terminarlo; los frames de simulacion tampoco se avanzan desde el renderer.
Los personajes controlados en vivo mantienen la ruta de animacion por frame.

Los snapshots dinamicos guardan el estado previo al primer uso de cada destino.
Esto permite reevaluar feedback, dependencias hacia delante, destinos multiframe
y buffers compartidos sin reutilizar el resultado logico ya avanzado. Se guardan
y restauran tambien todos los bytes del estado logico antes/despues de dibujar.
El tamano se calcula con las formulas de los allocadores reales 3/Q3/R3, no con
el supuesto de un frame. Ya no hay topes de 16 actores, 32 comandos, 12 buffers
o 128 modelos: las estructuras crecen por demanda y se liberan al reiniciar.
Se conservan limites de seguridad de 4096 operaciones y 16 MiB por buffer.

Los dos avances principales de transicion usan los incrementos del descriptor
original de movimiento, incluso en su primer tick. Otras mezclas continuan su
tasa observada entre ticks compatibles. Esto avanza parametros de clips, no
interpola poses ni matrices dibujadas. La tasa estimada de rutas no identificadas
no equivale a haber reconstruido su curva completa. Los cursores se asocian por
clip/tiempo para admitir usos diferentes de un mismo recurso.

Sigue siendo experimental: formatos invalidos, falta de memoria o un fallo del
evaluador conservan/restauran la pose logica. No incluye una conversion general
de cinematicas, replays dibujados por otras funciones ni todos los NPC especiales.
No se afirma que absolutamente todas las animaciones del juego esten convertidas.

`test-pose60.py` cubre feedback, alias, 40 comandos/buffers, 129 modelos,
destinos multiframe, tasas de transicion, restauracion y rechazo seguro.
`test-pose60-real.py` usa los allocadores, copia y compositor esqueletico reales
del APK para 3/Q3/R3: fases fraccionarias, crossfades, feedback y multiframe.
Se suministran malloc/copia/floor/ceil/fraction y un esqueleto sintetico de un
nodo; no se ejecuta el renderer GPU. Los tres trampolines se comparan con los
prefijos reales del APK. El build previo se conserva.

## Recuperacion por debajo de 30 presentaciones

`jsr_native60_run` subdivide el tiempo de un frame largo en hasta tres llamadas
completas a step(), con su entrada, acciones, colisiones, preparacion y dibujo.
No repite solo el personaje despues del arbol de acciones: conserva el orden
del motor y sus matrices. Hay un unico swap al terminar las llamadas. Se sigue
dibujando cada subpaso para no omitir posibles efectos del renderer; esto aumenta
el coste y NO mejora por si mismo el rendimiento.

Cada subpaso tiene reloj y fase propios; al terminar se restaura el reloj de
presentacion. Se detiene si cambia el area o el bloqueo de 30 FPS del motor,
se solicita salir o se pierde el foco.
Conserva los pasos grabados y las esperas reales de exPractice entre 10 y 60
presentaciones/s. Sigue vigente el tope de 100 ms para cargas/suspension:
no se recupera un bloqueo arbitrariamente largo ni se prometen 30 ticks reales
si la maquina no consigue ejecutar el trabajo necesario.

## Visibilidad y licencia del menu

`[display] draw_distance=2.5` amplia ocho limites de visibilidad y los radios
de los tres circulos del escenario. `detail_distance=1.5` aumenta nueve
umbrales de detalle de modelos/texturas. Draw admite 1.0 a 2.5; detalle 1.0 a 2.0.
La migracion de config version 1 a 2 cambia draw_distance=1.5 a 2.5; conserva
otros valores personalizados. La distancia de detalle sigue siendo 1.5.
No se modifica el plano lejano ni el formato de profundidad. El perfil se
valida antes de escribir; una biblioteca incompatible conserva sus limites.
Esto reduce cambios de distancia/LOD, no elimina las cargas entre zonas ni
repara texturas corruptas. Puede aumentar el coste de CPU/GPU y reducir FPS.

`[game] unlock_full_game=true` activa por defecto el intercambio de licencia
del motor. El callback `isLicensed` debe devolver el token de `licenseID()I`,
no el booleano 1: `IsDemoVersion` compara ambos valores. El token se obtiene
una vez y se conserva; la opcion no modifica partidas, personajes ni logros.
`false` desactiva este intercambio. La aparicion final del menu y el impacto
visual/rendimiento requieren comprobacion en consola.

## Pruebas

Desde el workspace, con Python y `work/pydeps` (Unicorn):

```
python work/test-native60.py
python work/test-native60-gravity.py
python work/test-native60-fall.py
python work/test-native60-integration.py
python work/test-native60-integration.py --prove-old-dispatch-fails
python work/test-native60-integration.py --prove-old-tutorial-timing-fails
python work/test-native60-integration.py --prove-old-practice-timing-fails
python work/test-native60-integration.py --prove-old-replay-physics-fails
python work/test-native60-integration.py --prove-old-catchup-fails
python work/test-native60-integration.py --audit-low-fps
python work/test-native60-trampolines.py
python work/test-visuals-license.py
python work/test-pose60.py
python work/test-pose60-real.py
```

La primera compila el reloj/adaptadores a ARM y ejecuta las dos funciones
originales de animacion del APK en Unicorn. Compara tiempo equivalente a
30/40/50/60 Hz, avance positivo/negativo/fraccionario, loop/clamp/subrangos,
100 segundos de acumulacion entera, pausa y cargas. Necesita devkitARM.

La segunda ejecuta el bloque real de gravedad y contador, sin reemplazar su
aritmetica. Demuestra que compensar gravedad sola no corrige el contador.

Estas pruebas NO simulan una partida ni validan colisiones, altura del salto,
distancia recorrida, enemigos, camara o eventos. Pasarlas no permite afirmar
que todo JSR funcione correctamente a 60 Hz.

## Pendiente de validacion y conversion

1. Adaptar gravedad y desplazamiento del jugador conjuntamente; verificar
   trayectoria, aterrizajes, grinding, aceleracion y frenado.
2. Identificar contadores discretos por actor y preservar transiciones y
   callbacks exactamente una vez, no dividir enteros por dos.
3. Adaptar camara, enemigos, objetos y cinematicas del motor.
4. Compensar los incrementos por llamada de los actores aun no adaptados,
   con pruebas de sus campos y temporizadores, sin omitir su preparacion por frame.
5. Comparar partidas en Switch: duracion real, respuesta, audio y render.

No se distribuye el APK ni su biblioteca con este prototipo.
