# Anchor para 2Ship (PC + Android) y arreglos del build de Android

Escrito leyendo tus repos; NO esta compilado. Pasame el log de errores y lo corrijo.

## Como aplicarlo
Copia el contenido de esta carpeta encima de la raiz de tu repo (mismas rutas, sobrescribe).

## Anchor (mm/2s2h/Network/Anchor/)
- `AnchorSocket` / `AnchorClient`: red (Windows, Linux, macOS y Android), sin dependencias nuevas. Hilos de lectura y escritura, reconexion automatica.
- `AnchorGame`: pose en vivo (20 Hz), jugadores remotos como actores (copian la pose exacta, con nombre), PvP, objetos y flags compartidos por equipo.
- `AnchorRecolor`: colores de tunica, pelo y botas POR JUGADOR. Copia la display list para cada juego de colores en lugar de modificar la original (asi no se pisan entre jugadores).
- `AnchorMenu`: pestana "Anchor" en Settings (servidor, sala, equipo, nombre, colores, PvP).
- `BenMenu.cpp` y `mm/CMakeLists.txt` ya traen el enganche (`ws2_32` en Windows, libreria compartida en Android).

## Arreglos de Android
- Workflow: `cmake;3.28.3` no existe en sdkmanager (era el error). Ahora CMake 3.28.3 viene de Kitware y se pasa con `cmake.dir`.
- Se restauraron los archivos que faltaban en `Android/`: `res/`, clases de SDL, `AssetCopyUtil`, `gradlew` y wrapper (Gradle 8.3), `proguard-rules.pro`.
- El CMake raiz ya copia el extractor a los assets del APK y no corre CPack en Android.

## Limitaciones conocidas
1. El CMake nuevo no tiene soporte de Android para las dependencias (SDL, libzip, ogg/vorbis/opus, etc. venian del port viejo y de `libultraship`). Lo siguiente que fallara en CI casi seguro es la configuracion de CMake; pasame ese log.
2. Los metodos nativos de controles (`setButton`, `setAxis`...) viven en `libultraship`; sin ellos los controles tactiles no llegan al juego.
3. Colores: la clasificacion por tono (verde = tunica, amarillo = pelo, marron = botas) es una heuristica. Si algo se pinta mal, se ajusta en `AnchorRecolor.cpp`.
4. No se sincroniza el guardado completo ni las flags de escenas, solo objetos y flags de eventos.
