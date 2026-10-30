# 2Ship Android (arm64): controles táctiles configurables

Estado: escrito leyendo tus repos, **NO compilado**. Pásame los errores del compilador y los voy corrigiendo.

## Qué hay en este paquete

| Archivo | Para qué |
|---|---|
| `Android/app/src/main/java/com/dishii/mm/ControlsConfig.java` | Posición, tamaño, opacidad y colores de cada botón, más ajustes globales (se guardan en JSON) |
| `.../TouchControlsView.java` | Dibuja los controles (opacos y con borde) y manda los toques al juego. Multitoque y modo edición (arrastrar) |
| `.../ControlsSettings.java` | Menú dentro de la app: tamaño global, opacidad, borde, cámara; por botón: tamaño, opacidad, X/Y y 4 colores |
| `.../MainActivity.java` | El de Waterdish con el overlay XML reemplazado. Conserva los métodos nativos y `Enable/DisableTouchArea` |
| `Android/app/build.gradle` | Solo `arm64-v8a`, versión 2.0.0-anchor |
| `Android/app/src/main/AndroidManifest.xml` | Igual que el original, más permiso INTERNET (para Anchor) |
| `.github/workflows/android.yml` | CI: genera `2ship.o2r` y compila el APK en GitHub Actions |
| `ANCHOR_PROTOCOL.md` | Protocolo del servidor Anchor, leído de su código |

## Cómo aplicarlo

1. Copia estas carpetas encima de tu repo, en las mismas rutas. Sobrescribe `MainActivity.java`, `build.gradle` y `AndroidManifest.xml`.
2. Ya no se usan `res/layout/touchcontrol_overlay.xml` ni los drawables `ic_*`. Puedes borrarlos o dejarlos.
3. Sube a GitHub y ejecuta *Actions > android-arm64 > Run workflow*.

## Cómo se usa en el teléfono

- Botón `☰` (arriba al centro): abre el menú de controles.
- `◉`: oculta o muestra todos los controles.
- Menú > *Editar posiciones*: arrastra cualquier botón. Si solo lo tocas, abre su configuración (tamaño, opacidad, posición exacta, colores). `LISTO` guarda y sale.
- Los toques que no caen sobre un botón pasan al juego, así el menú ImGui sigue funcionando. La cámara táctil solo responde a la derecha del porcentaje de pantalla que elijas.

## Lo que falta (bloqueante)

Los métodos nativos `attachController`, `setButton`, `setAxis` y `setCameraState` no están en el repo principal. Viven en el submódulo `libultraship` del fork de Waterdish (commit `ece7ef4`), que venía vacío en tu zip.

Para subir a la versión nueva de 2Ship (que usa `kenix3/libultraship` en el commit `7f9b86a`) hay que portar esos parches. Sin ellos el lado Java compila, pero el juego no recibe los toques.

Necesito, como zip: `Waterdish/libultraship @ ece7ef4` y `kenix3/libultraship @ 7f9b86a`. Si se puede, también `Waterdish/ZAPDTR @ 24ca939` y `Waterdish/OTRExporter @ 1cbb1b6`, porque la extracción del ROM se hace en el teléfono.

## Anchor

El repo `anchor` es solo el servidor (Go). El cliente es código del juego y 2Ship no lo trae, ni en PC ni en Android. Hay que escribirlo desde cero para Majora's Mask.
