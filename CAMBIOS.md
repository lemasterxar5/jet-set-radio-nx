# Cambios de código desde la primera release 1.0.0

Base: ZIP de fuentes conservado antes de la auditoría, build 202610211727.
Destino: build 202610220305.

Contiene únicamente archivos de código/build modificados o nuevos, completos y con sus rutas originales.
No es un proyecto independiente: copia `source/` y `runtime/` sobre los fuentes de la primera release.
Alternativamente, aplica `CHANGES.patch` desde la raíz del proyecto con `git apply CHANGES.patch`; no combines ambos métodos. El parche incluye únicamente las diferencias de código.

No incluye NRO, bibliotecas, datos del juego, archivos sin cambios ni documentación general actualizada.
Las diferencias exclusivas de CRLF/LF se ignoran. `manifest.json` registra los hashes y la procedencia.

## Archivos (18)

- modified: `runtime/source/rt_audout.c`
- modified: `source/jsr.h`
- modified: `source/jsr_audio.c`
- modified: `source/jsr_collision60.c`
- modified: `source/jsr_collision60.h`
- modified: `source/jsr_collision60_tramp.S`
- modified: `source/jsr_config.c`
- modified: `source/jsr_engine_clock.c`
- modified: `source/jsr_engine_clock.h`
- added: `source/jsr_event60.c`
- added: `source/jsr_event60.h`
- added: `source/jsr_event60_tramp.S`
- modified: `source/jsr_input.c`
- added: `source/jsr_input_tramp.S`
- modified: `source/jsr_main.c`
- modified: `source/jsr_native60.c`
- modified: `source/jsr_pose60.c`
- modified: `source/jsr_pose60.h`
