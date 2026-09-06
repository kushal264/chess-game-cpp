# chess-game-cpp
# Chess — Streamlit Frontend for the C++ Engine

This is a web GUI for the `chess-cpp` project. **All chess rules live in the
C++ engine** — legal moves, check/checkmate/stalemate, castling, en passant,
promotion. This app is a thin Python/Streamlit layer that:

1. Starts the compiled C++ binary as a subprocess (`chess --engine`)
2. Sends it moves and reads back a small machine-readable status block
3. Renders the board as clickable squares

No chess logic is duplicated in Python. If you find a rules bug, fix it in
`chess-cpp/src/main.cpp`, not here.

## 1. Build the C++ engine first

From the `chess-cpp` project folder:

```bash
g++ -std=c++17 -O2 -o chess src/main.cpp
```

Copy (or symlink) the resulting `chess` binary into this `chess-streamlit`
folder — `app.py` looks for it right next to itself by default:

```bash
cp ../chess-cpp/chess ./chess
```

Alternatively, leave the binary wherever you built it and point at it with
an environment variable:

```bash
export CHESS_ENGINE_PATH=/full/path/to/chess
```

## 2. Install the Python requirements

```bash
pip install -r requirements.txt
```

(Only `streamlit` is needed — everything else is the standard library.)

## 3. Run it

```bash
streamlit run app.py
```

This opens a local web page. Click a piece to select it (legal destination
squares highlight), then click a destination square to move. Pawn
promotions pop up a piece picker. The sidebar shows whose turn it is,
check/checkmate/stalemate status, move history, and a "New game" button.

## How the engine protocol works

`chess --engine` (added as a mode on top of the same interactive game)
switches the binary to a scripted mode: it prints one status block after
startup and after every command:

```
BOARD_START
FEN <standard FEN string>
STATUS <ONGOING|CHECK|CHECKMATE|STALEMATE>
LASTERROR <NONE|invalid_format|illegal_move>
LEGAL <space-separated legal moves in UCI form, e.g. e2e4 e7e8q>
BOARD_END
```

You send it moves the same way you would in the human console mode —
one UCI-style move per line (`e2e4`, `e7e8q` for promotion) — and it
replies with an updated block. `engine_client.py` implements this protocol
in a small `ChessEngine` class; `app.py` only talks to that class, never
to the raw subprocess directly.

This design means you could build a *different* frontend (a native app, a
different web framework, a Discord bot) against the exact same C++ binary
and protocol without touching the engine at all.

## Files

```
chess-streamlit/
├── app.py              # Streamlit UI: board rendering + click handling
├── engine_client.py     # Subprocess wrapper that speaks the engine protocol
├── requirements.txt      # pip requirements (streamlit only)
└── chess                 # (you copy this here) the compiled C++ binary
```
