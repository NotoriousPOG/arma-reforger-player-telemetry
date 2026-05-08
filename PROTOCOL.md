# Player Location and Base Data Protocol

A simple HTTP/JSON protocol for streaming live player positions and base / objective state from an Arma Reforger server (via a Workbench script mod) to an external receiver.

Designed for live-tracking maps, dashboards, analytics, AAR tooling. Push-only — the mod posts; the receiver listens. No bidirectional channel, no subscriptions, no replay protocol — each request is a complete snapshot the receiver may drop, replay, or aggregate at will.

**Status:** v1.1
**Reference mod:** `PlayerTelemetry` — see [`README.md`](README.md).

---

## Overview

| | |
|---|---|
| Transport | HTTPS POST, JSON body |
| Cadence | typical 5 s tick, configurable per server (mod enforces a 1 s floor) |
| Auth | bearer token, sent as either `Authorization` header or `?token=` query param |
| Idempotency | each request is a full snapshot; receivers may drop or aggregate |
| Originator | dedicated server only (mod skips clients via `Replication.IsServer()`) |

## Endpoint

```
POST <receiver-base-url><path>?server=<server-tag>[&token=<token>]
Authorization: Bearer <token>          # optional if ?token= is used
Content-Type: application/json
```

The path is **receiver-defined** — the mod's default is `/api/arma/live-scenario`, but operators set both `EndpointHost` and `EndpointPath` in the mod's config. A receiver implementing this protocol can serve any path.

The `server` query param is a short identifier for the originating server (e.g. `na-conflict`, `eu-main`). Receivers use it to multiplex multiple servers behind one auth token if they want.

### Auth: header *or* query param

Receivers MUST accept both forms because Enfusion's `RestContext` on dedicated servers does not reliably transmit custom HTTP headers — the reference mod sends the token as `?token=…`.

- **Preferred for any non-Enfusion client:** `Authorization: Bearer <token>` header.
- **Required path for Enfusion-mod clients:** `?token=<token>` query param.

A receiver SHOULD log a warning when the same request arrives with both forms set to different values; reject with `401`.

## Body

A single JSON object — one snapshot at one server tick:

```json
{
  "scenarioName":   "PlayerTelemetry",
  "mapName":        "",
  "updateInterval": 5.0,
  "timestamp":      12345.67,
  "players":        [ ... ],
  "objectives":     [ ... ]
}
```

### Top-level fields

| Field | Type | Description |
|---|---|---|
| `scenarioName` | string | Free-form scenario identifier. **In v1 the reference mod hardcodes this to `"PlayerTelemetry"`.** Receivers SHOULD treat it as informational, not as a key. |
| `mapName` | string | Terrain identifier. **In v1 the reference mod sends this as `""`** (empty). Receivers should fall back to the configured server tag or external metadata for terrain-aware behavior. |
| `updateInterval` | number | Seconds between ticks. Receivers use this to detect missed ticks. |
| `timestamp` | number | Server tick time in seconds (`BaseWorld.GetWorldTime()` — game time, not wall clock). |
| `players` | array of Player | Active player snapshots — see below. |
| `objectives` | array of Objective | Bases / capture points / HQs — see below. |

### Player

```json
{
  "name":    "PlayerOne",
  "uid":     "stable-reforger-uuid",
  "faction": "USSR",
  "coords":  [4500.0, 100.0, 3200.0],
  "status":  0
}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | Display name from `PlayerManager.GetPlayerName(playerId)`. |
| `uid` | string | Stable backend identity from `BackendApi.GetPlayerIdentityId(playerId)`. Empty string if backend is unavailable. |
| `faction` | string | `SCR_ChimeraCharacter.GetFactionKey()` — Reforger faction key (`USA`, `USSR`, `FIA`, …) or `""` if no faction. |
| `coords` | `[number, number, number]` | World-space `[x, y, z]` in meters. Raw `IEntity.GetOrigin()` — Y-up, terrain origin at one corner. |
| `status` | integer | `0` = alive, `2` = dead, `3` = in vehicle. (No `1` is currently emitted; reserved for future "unconscious" state. Priority order in the mod: dead > vehicle > alive.) |

Players whose controlled entity is `null` (between spawns, etc.) are dropped from the snapshot rather than emitted with empty fields.

### Objective

Objectives correspond to `SCR_CampaignMilitaryBaseComponent` instances on the map — Conflict capture points, HQs, bases that participate in the campaign system.

```json
{
  "name":            "Levie",
  "position":        [4500.0, 100.0, 3200.0],
  "factionKey":      "USSR",
  "baseType":        0,
  "isHQ":            false,
  "isControlPoint":  true,
  "seizingRadius":   50.0
}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | `SCR_CampaignMilitaryBaseComponent.GetBaseName()`. |
| `position` | `[number, number, number]` | World-space `[x, y, z]` of the base's owning entity. `[0, 0, 0]` if no owner entity exists. |
| `factionKey` | string | Currently owning faction's key, or `""` if neutral / contested. |
| `baseType` | integer | **Reserved. Always `0` in v1** — the mod does not populate this yet. Future versions may expose mod-specific base type IDs. |
| `isHQ` | boolean | `SCR_CampaignMilitaryBaseComponent.IsHQ()`. |
| `isControlPoint` | boolean | `SCR_CampaignMilitaryBaseComponent.IsControlPoint()`. |
| `seizingRadius` | number | Radius from the base's first `SCR_SeizingComponent`. `0` if the base has no seizing components. |

Non-campaign bases (e.g. neutral POIs that aren't part of Conflict) are not emitted.

## Recommended limits

These are guidance for receivers, not protocol-mandated.

| Limit | Suggested default | Rationale |
|---|---|---|
| Max players per snapshot | 256 | Reforger server-cap headroom |
| Max objectives per snapshot | 1024 | Realistic Conflict scenarios run 30–200 |
| Max body size | 1 MB | |
| Max request rate | ≤ 720 req/hr per server token | Matches the mod's default 5 s tick |

If `players` exceeds the limit, receivers should reject the snapshot with `400`. If `objectives` exceeds, truncate silently. Mod-side validation should target the player limit specifically.

## Response codes

| Code | Meaning | Mod action |
|---|---|---|
| `200` | Accepted | Continue |
| `400` | Validation failure (bad JSON shape, type mismatch, too many players) | Stop and log; do not retry — the snapshot is structurally bad |
| `401` | Bad / missing token | Stop, log, alert operator. The token went into config wrong |
| `429` | Rate-limited | Back off; the mod is ticking too fast for this receiver |
| `5xx` | Receiver error | Drop snapshot; the next tick will carry fresh state |

The reference mod calls `RestContext.POST` non-blocking and only logs response codes — it never blocks the game thread on a retry. Snapshots are stateless, so dropping one is safe.

## Coordinate system

Reforger world space is meters, **Y-up**, with terrain origin at one corner. Map renderers (Leaflet / MapLibre) typically use `CRS.Simple` and need to know the terrain's full extent to build the projection. Mods always send raw world coordinates; receivers and renderers handle any axis flip or scaling for visualization.

## Mod-side configuration (operator-facing)

The reference mod (`PlayerTelemetry`) reads `$profile:PlayerTelemetry/config.json` on the dedicated server. If the file is missing on first run, the mod auto-writes a template and exits with a warning telling the operator to fill in the required fields and restart.

```json
{
  "AuthToken":    "",
  "EndpointHost": "",
  "EndpointPath": "/api/arma/live-scenario",
  "ServerTag":    "",
  "TickSeconds":  5.0,
  "Enabled":      true,
  "DebugLog":     false
}
```

| Field | Default | Notes |
|---|---|---|
| `AuthToken` | `""` | Required. Empty disables the mod with a warning. |
| `EndpointHost` | `""` | Required. HTTPS is forced — the mod prepends `https://`. |
| `EndpointPath` | `/api/arma/live-scenario` | Receiver-defined; the spec doesn't mandate a path. |
| `ServerTag` | `""` | Required. Goes onto the URL as `?server=`. |
| `TickSeconds` | `5.0` | Floor of `1.0` enforced — mod clamps `< 1000 ms` periods. |
| `Enabled` | `true` | |
| `DebugLog` | `false` | When `true`, mod logs player count per tick + raw success body. |

## Lifecycle

The mod hooks `SCR_BaseGameMode.OnGameStart` and `OnGameEnd`:
- **OnGameStart** → manager `Start()`: loads config, registers periodic callback.
- **OnGameEnd** → manager `Stop()`: removes the callback.

This means telemetry runs **per mission**, not per server process. A scenario change restarts the manager. Config is re-read every mission start.

## Versioning

This protocol is currently unversioned on the wire — there is no `Spec-Version` header or field. Future revisions should consider:
- Adding `Spec-Version: <int>` as a request header so receivers can reject incompatible clients cleanly.
- Treating any added top-level field as optional until v2.

## License

MIT.

---

## Changelog

### v1.1
- Corrected `status` field type from string enum to integer (`0`/`2`/`3`).
- Documented dual-auth (header *and* query param), with rationale tied to Enfusion's REST stack.
- Flagged `scenarioName` as hardcoded in v1 of the reference mod.
- Flagged `mapName` as empty in v1 of the reference mod.
- Flagged `baseType` as reserved / always `0` in v1.
- Added `seizingRadius` source detail (`base.GetCapturePoints()[0].GetRadius()`).
- Added 1 s tick floor.
- Added per-mission lifecycle note (`OnGameStart` / `OnGameEnd`).
- Added operator-facing config block as a separate section.

### v1.0
- Initial spec.
