# Rust Client/Server Networking

The Rust networking path is independent from the existing C++ client and server.
It intentionally uses a Rust-only protocol; both implementations remain available
until a later compatibility or removal decision.

## Process model decision

Rust server mode uses one viewer process per client.

The current application owns one ZvApp, one Viewer, and a root egui viewport.
Supporting multiple independent client viewers in one process would require a
session manager, per-session viewport IDs, and broader routing changes throughout
the UI. A process per client preserves the existing window model, isolates viewer
crashes, and gives each connection a natural lifetime boundary. The public server
is a small supervisor that starts the same zv executable in a hidden session mode
and proxies the client connection to it. There is no Python proxy and no
single-process alternative.

## CLI contract

    zv [IMAGES...]
    zv --server [--host 127.0.0.1] [--port 4207]
    zv --client <IMAGES...> [--host 127.0.0.1] [--port 4207]

- Local mode retains the existing viewer behavior.
- Server mode listens for clients and opens one viewer process per connection.
- The supervisor accepts at most 16 concurrent client sessions.
- Client mode sends image offers, then remains connected to serve original encoded
  file bytes when the remote viewer lazily requests them.
- --host defaults to 127.0.0.1 and --port defaults to 4207.
- The internal --server-session mode is hidden from help and is only used by the
  supervisor.

The server and client must use the Rust implementation and the same protocol
version. The Rust path is not wire-compatible with the C++ protocol.

## Protocol and runtime

Networking uses blocking std::net sockets, worker threads, and channels. Each
frame has a four-byte big-endian length prefix followed by a MessagePack map. The
map uses a named `type` discriminator and, except for body-less messages, a `body`
containing named fields. For example, the initial client message is equivalent
to:

```json
{"type": "hello", "body": {"version": 1, "capabilities": 1}}
```

Image contents use MessagePack's binary type rather than an integer array. The
`hello` message carries the explicit protocol version.

Initial message types cover the handshake, image offers, lazy data requests,
encoded image data, structured command results/errors, and graceful disconnects.

Every remote image has a stable connection-scoped ID. Offers carry its display
name, remote path metadata, optional dimensions, and format hint. The capability
handshake currently advertises image reads. Remote paths describe the client
filesystem and are never used as server-local paths.

## Implementation shape

1. The public server accepts a client and starts zv --server-session on an
   ephemeral loopback port.
2. The supervisor proxies bytes in both directions with blocking copy threads.
3. The session process accepts exactly one proxied connection and sends offers to
   the UI through a channel.
4. A remote image is decoded only when selected or preloaded. A worker asks the
   client for its original bytes, decodes them through the existing Rust image
   loaders, and wakes the UI.
5. A disconnect fails pending image requests; the supervisor terminates the
   corresponding viewer child.
