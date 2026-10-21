# Componentes y referencias

| Componente | Autores / procedencia | Licencia / avisos |
| --- | --- | --- |
| android32 | aks796; avisos específicos en sus fuentes | [MIT conservada](runtime/LICENSE) |
| so_util | Andy Nguyen, fgsfds | MIT; avisos en `runtime/source/so_util.c` y `.h` |
| libnx / libnx32 | Autores de libnx, Switchbrew; vita2hos/xerpi; adaptaciones de aks796 | [ISC](https://github.com/switchbrew/libnx/blob/master/LICENSE.md); [fork libnx32](https://github.com/aks796/libnx32) |
| Mesa / Mesa32 | Desarrolladores de Mesa; devkitPro; adaptaciones AArch32 de aks796 | [Licencias por componente](https://docs.mesa3d.org/license.html); [Mesa32](https://github.com/aks796/mesa32) |
| libdrm / Nouveau | Red Hat y demás autores; adaptación de devkitPro | Avisos por archivo en [libdrm_nouveau](https://github.com/devkitPro/libdrm_nouveau) |
| miniz | Rich Geldreich, Tenacious Software, RAD Game Tools, Valve y colaboradores | [Aviso completo conservado](licenses/miniz-LICENSE.txt) |
| zlib | Jean-loup Gailly, Mark Adler y colaboradores | Licencia zlib; [upstream](https://zlib.net/) |
| Expat | Autores y mantenedores de libexpat | MIT; [upstream](https://github.com/libexpat/libexpat) |
| BLAKE3 | Equipo BLAKE3 y colaboradores | Opciones de licencia y avisos en [upstream](https://github.com/BLAKE3-team/BLAKE3) |
| GCC / binutils / newlib / devkitPro | Autores de cada proyecto y mantenedores de toolchains | Licencias y excepciones del SDK; no se incluye el SDK |
| LZ4 | Autores de LZ4 | Dependencia opcional del conversor Python, no incluida; [upstream](https://github.com/lz4/lz4) |
| FFmpeg | Autores de FFmpeg | Agradecimiento por el entorno de desarrollo; no enlazado ni incluido en esta release |

Referencia técnica especialmente agradecida: [seamusduncmcgrath/cod-boz-arm64-nx](https://github.com/seamusduncmcgrath/cod-boz-arm64-nx), derivado de [Producdevity/cod-boz-port](https://github.com/Producdevity/cod-boz-port). No se incluye ese repositorio en esta exportación.

La lista reconoce los proyectos identificados en las fuentes y en el entorno de compilación. Para redistribuir una compilación distinta, revisa también los avisos de todos los archivos y bibliotecas efectivamente enlazados. Los componentes externos no se incluyen en la carpeta de fuentes.
