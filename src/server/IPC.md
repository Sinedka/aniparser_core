# Local IPC protocol

`aniparser_server [cache-name] [endpoint] [data-directory]` listens on a local
IPC endpoint (defaults: `aniparser` and the platform endpoint).
Linux/macOS/Android/iOS use Unix domain sockets; Windows uses a named pipe such as
`\\.\pipe\aniparser`. The stream carries newline-delimited JSON; each request
has an `id` and `method`. Shared-memory payloads are not
copied over the socket. Handles are `{ "segment_id": N, "offset": N }`.

Start with `{"id":1,"method":"hello"}`. The response includes protocol/layout
versions and heap names for Anime, variable data, Search and Feed. Clients map
those heaps using the same cache name and platform provider, then
resolve handles locally. Map new segments as handles reference them; segment
counts in `hello` are only a snapshot.
File-backed platforms also return `shared_memory_directory` for the provider.

## Requests

- `getAnime`: `{"id":2,"method":"getAnime","anime_id":771}`. The immediate
  reply contains an Anime handle plus `loaded` and `refreshing` flags. Full
  cached details are returned immediately; the API is queried only when more
  than five minutes have passed since its last request. After a refresh,
  `anime.updated` arrives with the same request id, the handle and `ok` status.
- `getSearch`: `{"id":3,"method":"getSearch","params":{"q":"наруто",
  "offset":0}}`. Its reply contains one Search page handle and offset. Request
  another page by adding 10 to `offset`; each page owns a handle. If a cached
  page is older than five minutes, it is replied immediately and a later
  `search.updated` event with the same request id carries the refreshed handle.
  Release the old page handle after switching to the refreshed one.
- `getFeed`: `{"id":4,"method":"getFeed","watches":[]}`. The reply contains
  a Feed handle and a `refreshing` flag. A cached feed is returned immediately.
  If it is over five minutes old, the API refresh runs in the background and
  `feed.updated` arrives with the same request id and refreshed handle.
- `deleteAnime`, `deleteSearch`, and `deleteFeed`: pass the matching handle to
  release the reference or record. Release each returned handle when finished.

Search fields use the names in `yani::SearchInput`. Requests and replies are
independent JSON lines, so clients should match replies and events by `id`.

On macOS, the POSIX shared-memory provider is used and the socket uses the
portable `accept`/socket options branch. Windows uses `WindowsShmSegmentProvider`
for persistent mapped files; pass the shared data directory as the third CLI
argument. Android and iOS builds omit the desktop executable: the embedding
app starts `IpcServer` with a socket path inside its private container and uses
`FileShmSegmentProvider`. Android processes must share the app UID and files
directory. On iOS, pass the App Group container directory when the app and an
extension or other process need the same cache; use a socket path in that
container too. Start the iOS service while the app is active rather than
relying on a permanently running background server.
