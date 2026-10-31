# Anchor para 2Ship: protocolo y paquetes propios

Servidor: https://github.com/garrettjoecox/anchor (TCP, JSON terminado en NUL, puerto 43383).
El servidor no conoce el juego. Estos son los paquetes que define el cliente de 2Ship:

| Tipo | Ruta | Contenido |
|---|---|---|
| `HANDSHAKE` | servidor | `roomId`, `clientId`, `clientState` (name, teamId, pvp, tunic, hair, boots, sceneId, form, health) |
| `UPDATE_CLIENT_STATE` | sala | `state` con los mismos campos; se envia al cambiar escena, forma, vida, colores o PvP |
| `PLAYER_POSE` | sala (`quiet`) | `x,y,z,ry,sceneId,form,health` y `j`: 69 enteros (23 articulaciones x 3) con la pose de Link |
| `PVP_HIT` | `targetClientId` | `damage` (16 = 1 corazon) |
| `ITEM_GIVE` | `targetTeamId` | `item` |
| `FLAG_SET` | `targetTeamId` | `flagType` (week event reg / event inf), `flag` |

Todos los paquetes llevan `clientId` para que el servidor no te los devuelva a ti.
Colores: enteros `0xRRGGBB`. PvP: ambos jugadores deben tenerlo activado.
