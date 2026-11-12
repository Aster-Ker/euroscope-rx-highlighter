# RX Highlighter – EuroScope plug-in

Makes an aircraft's callsign **light up in your EuroScope lists** (departure list, arrival list,
SIL, etc.) **while that pilot is talking on frequency**.

It only changes the lists you set it up in. Nothing is drawn on the radar screen, and your
radar tags stay exactly as they are.

![Departure list: BAW123's callsign lights up yellow with an RX marker while it transmits](docs/images/list.png)

<sub>These images are illustrations of the plug-in's behaviour, not real EuroScope screenshots.
Colours and columns depend on your profile. Source: `docs/images/illustrations.html`.</sub>

It gets "who is transmitting" from your voice client:

| Voice client | How |
|---|---|
| **TrackAudio** (recommended) | WebSocket `ws://127.0.0.1:49080/ws`, `kRxBegin` / `kRxEnd` events |
| Older AFV-compatible clients | Falls back to polling `http://127.0.0.1:49080/transmitting` |

Nothing to configure. Start TrackAudio and the plug-in connects by itself and reconnects if
TrackAudio is restarted.

## Works alongside VATSIM UK's plug-ins

Built to load next to **UK Controller Plugin (UKCP)** and the SRD tools
(**vFPC** and UKCP's **SRD Search**):

* Same EuroScope SDK (compatibility code 16) and the same 32-bit build as UKCP.
  `scripts/fetch-sdk.ps1` downloads the SDK from the UKCP repository.
* Own command prefix (`.rxh`), so it never takes `.ukcp` or `.vfpc` commands.
* Own tag items, all prefixed `RX Highlighter:`. EuroScope scopes item IDs per plug-in,
  so they can't clash with UKCP or vFPC items.
* Draws **nothing on the radar** and adds no clickable screen objects, so it can't steal
  clicks, menus or ASR settings from UKCP or vFPC.
* Never calls EuroScope from its background thread. The network thread only touches its own
  data, and unloading or closing EuroScope takes well under a second.
* No conflicting settings keys (`RxColour`, `RxNormalColour`, `RxLingerMs`, `RxMarker`, `RxPort`).
* Talks only to `127.0.0.1:49080`, as a client. Neither UKCP nor vFPC uses that port.

## Install

1. Download `RxHighlighter.dll` from the
   [latest release](https://github.com/Aster-Ker/euroscope-rx-highlighter/releases/latest).
   Copy it somewhere stable, e.g. next to your other plug-ins in the UK
   controller pack (`...\UK\Data\Plugin\`).
2. EuroScope → **OTHER SET → Plug-ins… → Load** → choose `RxHighlighter.dll`.
   UKCP and vFPC stay loaded as normal. Load order does not matter.
3. Save your profile/plug-in settings when EuroScope asks, so the plug-in loads next time.

## Automatic setup for VATSIM UK SMR profiles

`tools/Setup-RxHighlighter-UK-SMR.bat` does the install and the list setup below for every
UK SMR profile in one go:

* copies `RxHighlighter.dll` into `UK\Data\Plugin`. It uses the DLL next to the batch file,
  or downloads the latest release if there isn't one.
* loads the plug-in in every profile that uses the SMR lists (36 profiles in the current pack)
* switches every callsign column in `Lists_SMR.txt` and `Lists_SMR_RECAT.txt` to
  **RX Highlighter: Callsign**, keeping each column's mouse-button functions, so UKCP,
  SRD Search and vFPC clicks are unchanged
* backs up every file it changes as `<file>.rxh-backup`

1. Close EuroScope.
2. Download [Setup-RxHighlighter-UK-SMR.bat](https://github.com/Aster-Ker/euroscope-rx-highlighter/raw/main/tools/Setup-RxHighlighter-UK-SMR.bat)
   (and optionally [Undo-RxHighlighter-UK-SMR.bat](https://github.com/Aster-Ker/euroscope-rx-highlighter/raw/main/tools/Undo-RxHighlighter-UK-SMR.bat))
   into your `UK` folder, the one containing `Data`, `Heathrow`, etc.
3. Double-click `Setup-RxHighlighter-UK-SMR.bat`.

Run it again after a UK controller pack update, because the updater may replace these files.
Running it twice is safe. `Undo-RxHighlighter-UK-SMR.bat` restores the backups and removes the DLL.

## Make the list line light up

EuroScope doesn't let plug-ins recolour EuroScope's own built-in columns. Instead, this
plug-in provides a callsign column that changes colour itself:

![Setup: change the Callsign column's tag item type and keep the same mouse functions](docs/images/setup.png)

1. Open the list (e.g. Departure List) → click the list's header menu → **Columns setup**.
   Leave your radar tag definitions alone, so the radar doesn't change.
2. Select the **Callsign** column and change its *Tag item type* to
   **`RX Highlighter: Callsign (lights up on RX)`**.
3. **Set the column's left/right mouse-button functions to what they were before.** This is
   what keeps your SRD and UKCP tools working. For example, keep "Open SRD Search" from UKCP,
   or your vFPC / flight-plan functions, on the same button. Tag functions belong to the
   column, not the item type, so they work on this item exactly as before.
4. Optional: add a narrow column with **`RX Highlighter: Transmitting marker`**. It shows
   `RX` while the aircraft is transmitting.

When the pilot isn't transmitting, the callsign is **white**. While they're on frequency it
changes to the highlight colour (yellow by default).

Choose a different colour for when nobody is talking with `.rxh normal R G B`. Type
`.rxh normal default` to use EuroScope's own assumed, notified and non-concerned colours
instead.

Check your setup without anyone talking: `.rxh test BAW123` lights up BAW123 for 5 seconds.

## Commands

![Example plug-in messages from .rxh test and .rxh status](docs/images/messages.png)

| Command | |
|---|---|
| `.rxh` | Help |
| `.rxh status` | Connection state, settings and who is lit up |
| `.rxh colour R G B` | Highlight colour (default `255 230 0`, bright yellow) |
| `.rxh normal R G B` | Callsign colour when **not** transmitting (default `255 255 255`, white) |
| `.rxh normal default` | Use EuroScope's own colours when not transmitting |
| `.rxh linger MS` | Stay lit for MS after the transmission ends (default 0 = turn off as soon as PTT is released, max 10000) |
| `.rxh marker TEXT` | Text shown by the marker item (default `RX`) |
| `.rxh port N` | Voice client port (default 49080) |
| `.rxh test CALLSIGN [SEC]` | Pretend CALLSIGN is transmitting (1–60 s, default 5) |

## Limitations

* **Updates straight away.** As soon as TrackAudio reports that a pilot pressed or released
  PTT, the plug-in asks EuroScope to redraw, without waiting for EuroScope's 1-second plug-in
  timer. Use `.rxh linger MS` if you'd like the highlight to stay on briefly after they stop.
* Highlights the **text colour** of the item. Plug-ins can't change a list row's background.
* Matches on network callsign, so it lights up anyone you **receive** in TrackAudio, on any
  of your RX frequencies.
* Controllers who transmit (e.g. `EGLL_N_TWR`) have no flight plan, so nothing lights up for them.

## Build

Needs Windows with Visual Studio 2022 (Desktop development with C++) and CMake.

```powershell
./scripts/build.ps1
```

This downloads the EuroScope SDK, builds a **32-bit** release DLL, runs the unit tests and
prints the path to `build\Release\RxHighlighter.dll`.

Manual build:

```powershell
./scripts/fetch-sdk.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

`.github/workflows/build.yml` builds the DLL on every push. To publish a release, bump
`RXH_VERSION_STRING` in `src/Version.h`, add `docs/releases/vX.Y.Z.md`, then publish a GitHub
release with tag `vX.Y.Z` and those notes. The workflow builds the DLL and attaches it to the
release automatically.

## Layout

```
src/dllmain.cpp            EuroScope entry points
src/RxHighlightPlugin.*    Tag items, commands, settings
src/MainThreadWaker.*      Runs list updates on EuroScope's UI thread the moment RX changes
src/RxRadarScreen.*        Redraw hook only (draws nothing on the radar)
src/VoiceClient.*          Background Winsock client (WebSocket, then HTTP fallback)
src/WebSocketCodec.*       Minimal RFC 6455 framing + HTTP response parsing
src/VoiceMessage.*         TrackAudio JSON message parsing
src/RxState.*              Thread-safe "who is transmitting" state
test/test_main.cpp         Unit tests (portable)
```
