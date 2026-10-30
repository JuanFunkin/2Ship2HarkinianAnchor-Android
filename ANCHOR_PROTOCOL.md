# Protocolo de Anchor (leído de server.go, client.go, team.go y room.go)

Transporte: TCP, puerto 43383 (servidor público por defecto: `anchor.hm64.org:43383`).
Cada paquete es un JSON terminado en un byte NUL (0x00). Tamaño máximo: 8 MiB.

## Conexión

1. **Handshake** (obligatorio, primer paquete):
   `{"type":"HANDSHAKE","roomId":"<sala>","clientId":<0 o id previo>,"clientState":{"teamId":"<equipo>", ...}}`
   - `clientId` 0: el servidor asigna uno. Si reconectas con el mismo id y la misma sala, reemplaza la sesión vieja.
   - El servidor responde con el estado de todos los clientes y un `UPDATE_ROOM_STATE`.
2. **Health check**: `{"type":"STATS"}` devuelve `uniqueCount`, `gameCompleteCount` y `onlineCount`.
3. Tras ~30 s sin actividad el servidor manda `{"type":"HEARTBEAT","quiet":true}`. Una sala inactiva 5 min se borra.

## Enrutado (según los campos del paquete)

- `targetClientId`: solo a ese cliente.
- `targetTeamId`: al equipo. Con `"addToQueue":true` se guarda en la cola del equipo (máx. 512) para quien entre después.
- `UPDATE_CLIENT_STATE`: actualiza el estado del cliente (`state.teamId` decide el equipo).
- `REQUEST_TEAM_STATE`: pide el guardado de un compañero en línea (el que tenga `isSaveLoaded=true`), o el último guardado del equipo más su cola si no hay nadie.
- `UPDATE_TEAM_STATE`: responde esa petición y se guarda como estado del equipo.
- `UPDATE_ROOM_STATE`: estado global de la sala; se reenvía a todos.
- Cualquier otro tipo: broadcast a toda la sala.
- `"quiet":true` evita el log del servidor (úsalo en paquetes de posición).
- `GAME_COMPLETE` incrementa el contador global.

## Qué falta

El servidor no conoce el juego. El significado de los paquetes (flags, ítems, escena, posición) lo define el cliente dentro de 2Ship. Para Majora's Mask hay que escribir ese cliente y engancharlo a los hooks de `GameInteractor`.
