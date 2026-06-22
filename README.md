# Advanced Query

Advanced Query is an open.mp component that lets Pawn scripts adjust selected public-facing server query data at runtime.

It currently provides two features:

- Override the max player count shown in the legacy server query.
- Override the hostname sent to players in the InitGame RPC.

The query max player override only affects what is advertised in the server query. It does not change the real server slot count, the configured `max_players`, or whether players can actually join.

## AI Generation Notice

This project was fully AI generated.

Review, test, and maintain it with that in mind, especially because the query max player override relies on open.mp internal LegacyNetwork layout details that are not exposed through the public SDK.

## How Query Slots Work

open.mp's legacy info query reports:

```text
current players / public max players
```

NPCs are subtracted from the public max player count by open.mp itself. For example, if the real server has `max_players = 310` and `10` NPCs are connected, the normal query display is:

```text
300 public slots
```

Advanced Query follows that behavior. If you call:

```pawn
SetQueryMaxPlayers(250);
```

the intended query display is:

```text
current_human_players / 250
```

If the requested value is below the current number of human players, the component raises the displayed max to the current human player count so the query does not show impossible values such as `120/100`.

## Natives

Include `advanced_query.inc` in your Pawn script:

```pawn
#include <advanced_query>
```

### SetQueryMaxPlayers

```pawn
native SetQueryMaxPlayers(slots);
```

Sets the public max player count shown in the legacy server query.

Returns the effective advertised slot count. Returns `0` if the component is unavailable or the server max player value cannot be read.

Notes:

- `slots` is clamped to the public max player limit.
- The public max player limit is `max_players - connected_npcs`.
- The displayed value is automatically kept at least as high as the current human player count.
- This does not change real slots.

Example:

```pawn
public OnGameModeInit()
{
    SetQueryMaxPlayers(100);
    return 1;
}
```

### GetQueryMaxPlayers

```pawn
native GetQueryMaxPlayers();
```

Returns the currently effective public query max player count.

If no override is active, this returns the normal public query max player count, which is the real configured max minus connected NPCs.

### ResetQueryMaxPlayers

```pawn
native ResetQueryMaxPlayers();
```

Resets the query max player override back to open.mp's normal public value.

Returns the restored public query max player count. Returns `0` on failure.

### SetInitGameHostname

```pawn
native SetInitGameHostname(const hostname[]);
```

Sets a separate hostname for the InitGame RPC sent to connecting players.

This does not change the normal server hostname or query hostname. It only changes the hostname value inside the InitGame RPC.

Returns `true` on success and `false` if the component is unavailable.

Hostnames longer than 64 bytes are truncated.

### ResetInitGameHostname

```pawn
native ResetInitGameHostname();
```

Disables the InitGame hostname override.

Returns `true` on success and `false` if the component is unavailable.

### IsInitGameHostnameSet

```pawn
native IsInitGameHostnameSet();
```

Returns `true` if the InitGame hostname override is currently active.

## Example

```pawn
#include <open.mp>
#include <advanced_query>
#include <YSI_Data\y_iterate>

public OnGameModeInit()
{
    SetQueryMaxPlayers(100);
    SetInitGameHostname("My Runtime Hostname");
    return 1;
}

public OnPlayerConnect(playerid)
{
    // Raise the public query capacity during runtime.
    if (Iter_Count(Player) >= 90)
    {
        SetQueryMaxPlayers(110);
    }
    return 1;
}
```

## Installation

1. Build the component.
2. Put the compiled `advanced-query` component in your server's `components` directory.
3. Add it to your open.mp component list.
4. Put `advanced_query.inc` in your Pawn include path.
5. Include it from your script.

## Building

Clone with submodules:

```bash
git clone --recursive https://github.com/edgyaf/omp-advanced-query.git
```

Configure and build with CMake.

Windows example:

```bash
cmake -S . -B build/windows-Release -A Win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-Release --config Release --parallel
```

Linux example:

```bash
cmake -S . -B build/linux-Release -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32
cmake --build build/linux-Release --config Release --parallel
```

## Limitations

- The query max player override is not a public open.mp SDK feature.
- The component locates and patches LegacyNetwork's private query max player value at runtime.
- If open.mp changes the LegacyNetwork layout, this feature may need updates.
- The component retries the query patch briefly when called early during script startup.
- InitGame hostname rewriting uses the public network outgoing RPC hook and is less layout-sensitive than the query slots override.
