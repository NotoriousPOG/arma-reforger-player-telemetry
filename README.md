# PlayerTelemetry

[![Reforger Workshop](https://img.shields.io/badge/Reforger%20Workshop-Player%20%26%20Base%20Live%20Data-blue)](https://reforger.armaplatform.com/workshop/1CDFD252B4101366-Player%2526BaseLiveData)

A server-side Arma Reforger Workbench mod that streams live game state to any HTTPS endpoint as JSON. Designed as the data layer for live tracking maps, after-action reports, dashboards, analytics, and any other tooling that benefits from a real-time view of your server.

**Available on the Reforger Workshop:** [Player & Base Live Data](https://reforger.armaplatform.com/workshop/1CDFD252B4101366-Player%2526BaseLiveData)

## What it does

Once per scenario lifecycle the mod hooks into the game's start and end events on the dedicated server. While the scenario is running, it ticks on a configurable interval (default 5 seconds, minimum 1 second) and emits a single JSON snapshot containing:

**For every connected player:**
- display name
- stable backend identity (Reforger UUID)
- faction key
- world position `[x, y, z]`, in meters
- status: alive (`0`), dead (`2`), or in-vehicle (`3`)

**For every Conflict military base on the map:**
- base name
- owning faction (or empty if neutral / contested)
- world position
- `isHQ` flag, `isControlPoint` flag
- seizing radius

The snapshot is serialized via `SCR_JsonSaveContext`, authenticated with a bearer token (sent as a query parameter due to Enfusion's REST limitations on dedicated servers), and POSTed to a host you configure.

## What it isn't

- **Not a map renderer.** The mod produces data; rendering is a separate concern. Pair with a Leaflet / MapLibre frontend (or whatever you like).
- **Not a tile generator.** For static map tiles, see the excellent [EnfusionMapMaker](https://github.com/nickludlam/EnfusionMapMaker).
- **Not bidirectional.** It's push-only — your receiver cannot ask the mod for anything. Each request is a complete, idempotent snapshot.
- **Not multi-tenant out of the box.** One server = one mod instance = one feed. A single receiver can multiplex multiple servers using the `?server=` tag, if you want.

## Why server-side and push-only?

- **Zero client overhead.** Players see no perf impact — the mod is gated by `Replication.IsServer()` and never executes on client machines.
- **Server is the source of truth.** No reconciling client snapshots.
- **HTTP is universal.** Any backend — Python, Node, Go, Rust, a serverless function, a Discord webhook — can consume the feed without any Reforger or Enfusion knowledge.
- **Stateless = robust.** Each snapshot is complete. Drop one, replay one, receive them out of order — the receiver tolerates all of it.

## Installation

1. Subscribe to **[Player & Base Live Data](https://reforger.armaplatform.com/workshop/1CDFD252B4101366-Player%2526BaseLiveData)** on the Reforger Workshop, or drop the mod folder into your server's addons directory if you're building from source.
2. Add `Player & Base Live Data` (mod ID `1CDFD252B4101366`) to your server's mod list.
3. Start the server once. The mod auto-writes an empty config template to `$profile:PlayerTelemetry/config.json` and exits with a warning.
4. Fill in the required fields (see below) and restart the server.

## Configuration

Profile-relative path: `$profile:PlayerTelemetry/config.json`.

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

| Field | Required | Notes |
|---|---|---|
| `EndpointHost` | yes | Hostname only, no scheme. HTTPS is forced. Example: `your-receiver.example.com` |
| `EndpointPath` | yes | Path on the receiver. Default: `/api/arma/live-scenario` |
| `ServerTag` | yes | Short identifier for this server (e.g. `na-conflict`, `eu-main`) |
| `AuthToken` | yes | Opaque token shared with the receiver out-of-band |
| `TickSeconds` | no | Ticks per snapshot. Default `5.0`, minimum `1.0` |
| `Enabled` | no | Master switch. Default `true` |
| `DebugLog` | no | Verbose per-tick logging. Default `false` |

The mod refuses to start if any of the four required fields are empty, emitting a clear warning per missing field. Tokens should be opaque random strings — generate one with `openssl rand -hex 32` or your platform's equivalent.

## Endpoint

```
POST https://<EndpointHost><EndpointPath>?server=<ServerTag>&token=<AuthToken>
Content-Type: application/json
```

Receivers may also accept `Authorization: Bearer <token>` as the auth method — the mod uses the query-param form because Enfusion's `RestContext` does not reliably transmit custom headers from a dedicated server.

## Snapshot shape (on the wire)

```json
{
  "scenarioName":   "PlayerTelemetry",
  "mapName":        "",
  "updateInterval": 5.0,
  "timestamp":      12345.67,
  "players": [
    {
      "name":    "PlayerOne",
      "uid":     "stable-reforger-uuid",
      "faction": "USSR",
      "coords":  [4500.0, 100.0, 3200.0],
      "status":  0
    }
  ],
  "objectives": [
    {
      "name":           "Levie",
      "position":       [4500.0, 100.0, 3200.0],
      "factionKey":     "USSR",
      "baseType":       0,
      "isHQ":           false,
      "isControlPoint": true,
      "seizingRadius":  50.0
    }
  ]
}
```

Notes on the v1 implementation:
- `scenarioName` is hardcoded to `"PlayerTelemetry"` — receivers should treat it as informational, not as a key.
- `mapName` is currently always `""`. Use `ServerTag` or external metadata for terrain identification.
- `baseType` is currently always `0`, reserved for future expansion.
- `status` enum: `0` = alive, `2` = dead, `3` = in vehicle. (No `1` is emitted; reserved for future "unconscious" state.)
- `timestamp` is server world time (game time), not wall clock.

The complete protocol specification — including limits, response codes, and the coordinate system — is documented in [`PROTOCOL.md`](PROTOCOL.md).

## Lifecycle

The mod hooks `SCR_BaseGameMode.OnGameStart` and `SCR_BaseGameMode.OnGameEnd`:

- **OnGameStart** → `TelemetryManager.Start()`: loads the profile config, registers a recurring tick callback.
- **OnGameEnd** → `TelemetryManager.Stop()`: removes the callback.

This means telemetry runs **per mission**, not per server process. A scenario change reinitialises the manager and re-reads the config.

## Building from source

This is a standard Arma Reforger Workbench script project. Open `PlayerTelemetry.gproj` in the Workbench, build, and copy the resulting addon into your server's mod folder.

```
PlayerTelemetry/
├── PlayerTelemetry.gproj
├── README.md
├── PROTOCOL.md
└── Scripts/
    └── Game/
        ├── SCR_BaseGameMode.c           # OnGameStart / OnGameEnd hooks
        ├── TelemetryManager.c           # Snapshot builder + REST poster
        ├── TelemetryProfileConfig.c     # Profile config struct + loader
        └── TelemetryRestCallback.c      # POST response handler
```

No prefabs, no UI, no client-side scripts. The entire mod is four script files.

## Receiver implementation

A receiver is just any HTTP server that:
1. Accepts `POST` on the path you configure.
2. Validates the bearer token from the `?token=` query param (or `Authorization` header).
3. Parses the JSON body.
4. Does whatever it wants with the snapshot — store it, broadcast over WebSocket, write to a tile-based map renderer, etc.

The mod doesn't ship a reference receiver. Build your own, or pair with an existing one in your community.

## Performance and safety notes

- **Tick floor**: the manager clamps `TickSeconds` to a 1 second minimum. Faster cadences would risk REST-stack contention without much practical benefit.
- **Non-blocking POST**: the mod uses Enfusion's async `RestContext.POST` and never blocks the game thread.
- **Best-effort delivery**: there is no retry. A failed POST is logged and dropped; the next tick replaces it. Operators monitoring `WARNING`-level logs will see network failures immediately.
- **Server-only**: the `Replication.IsServer()` check at the top of `Start()` ensures clients never tick.
- **Fail-closed config**: missing required fields disable the mod with a warning, rather than emitting partial snapshots or pointing at the wrong receiver.

## License

Source code in this repository is released under the [MIT License](LICENSE).

If this mod is published to the Reforger Workshop, the Workshop release is licensed under [APL-SA](https://www.bohemia.net/community/licenses/arma-public-license-share-alike) to comply with Workshop publishing requirements.
