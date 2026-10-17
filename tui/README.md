# AniParser TUI

The Linux terminal client uses Notcurses for a color UI and terminal bitmap
graphics. It is built only from its client sources and links Notcurses, CPR,
and nlohmann/json; it does not link the backend's `core` or `cache` targets.
Heap names come from the IPC `hello` reply. `CacheReader` maps those heaps with
read-only mappings and exposes only const Anime, Search, and Feed records. The
client sends IPC delete requests to release its references. Posters are
downloaded in a background worker and cached under the system temporary
directory. Image display depends on the terminal graphics protocol and the
Notcurses multimedia decoder; terminals without bitmap support show the
text-and-color layout.

Build inside the project development shell:

```sh
nix develop
cmake -S . -B out/Debug
cmake --build out/Debug --target aniparser_tui -j
```

Start the backend first, then launch the UI from a second terminal:

```sh
out/Debug/src/server/aniparser_server aniparser /tmp/aniparser.sock
out/Debug/tui/aniparser_tui aniparser /tmp/aniparser.sock
```

In Feed, arrow keys select poster cards. `/` starts a title search; results
show larger poster thumbnails with title, year, type, score, episode count,
and a short description. More results load automatically as you scroll; `n`
can request the next page early. Enter opens an Anime immediately from its cached
record while the full details load asynchronously. Press `v` or Enter on the
details screen to open viewing order, then select an entry and press Enter to
navigate to it. Escape returns to the previous Anime or list; `f` loads Feed,
`r` refreshes, and `q` quits.
