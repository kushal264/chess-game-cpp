"""
engine_client.py

A thin Python wrapper around the compiled C++ chess engine binary
(`chess --engine`). It starts the engine as a subprocess, sends it move
commands over stdin, and parses the machine-readable status block it
prints back on stdout after every command.

This does NOT reimplement chess rules in Python -- all move legality,
check/checkmate/stalemate detection, castling, en passant, and promotion
logic lives in the C++ engine. This module only speaks its protocol.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from dataclasses import dataclass, field
from typing import Optional


class EngineNotFoundError(RuntimeError):
    pass


class EngineProtocolError(RuntimeError):
    pass


@dataclass
class EngineState:
    fen: str = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    status: str = "ONGOING"          # ONGOING | CHECK | CHECKMATE | STALEMATE
    last_error: str = "NONE"         # NONE | invalid_format | illegal_move
    legal_moves: list[str] = field(default_factory=list)

    @property
    def turn(self) -> str:
        # FEN field 2 is the active color: 'w' or 'b'
        return self.fen.split(" ")[1]

    @property
    def board_rows(self) -> list[list[Optional[tuple[str, str]]]]:
        """
        Returns the board as 8 rows (rank 8 first, rank 1 last), each a list
        of 8 entries that are either None (empty square) or a tuple of
        (piece_letter_upper, color) where color is 'w' or 'b'.
        """
        placement = self.fen.split(" ")[0]
        rows = []
        for rank_str in placement.split("/"):
            row: list[Optional[tuple[str, str]]] = []
            for ch in rank_str:
                if ch.isdigit():
                    row.extend([None] * int(ch))
                else:
                    color = "w" if ch.isupper() else "b"
                    row.append((ch.upper(), color))
            rows.append(row)
        return rows


def find_engine_binary(explicit_path: Optional[str] = None) -> str:
    """
    Locate the compiled chess engine binary. Checks, in order:
    1. an explicitly given path
    2. the CHESS_ENGINE_PATH environment variable
    3. a few common relative locations next to this script / the C++ project
    4. PATH
    """
    candidates = []
    if explicit_path:
        candidates.append(explicit_path)
    if os.environ.get("CHESS_ENGINE_PATH"):
        candidates.append(os.environ["CHESS_ENGINE_PATH"])

    here = os.path.dirname(os.path.abspath(__file__))
    candidates += [
        os.path.join(here, "chess"),
        os.path.join(here, "chess_engine_test"),
        os.path.join(here, "..", "chess-cpp", "chess"),
        os.path.join(here, "..", "chess-cpp", "build", "chess"),
        "./chess",
    ]

    for c in candidates:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return os.path.abspath(c)

    on_path = shutil.which("chess")
    if on_path:
        return on_path

    raise EngineNotFoundError(
        "Could not find the compiled chess engine binary. Build it first:\n"
        "  g++ -std=c++17 -O2 -o chess src/main.cpp\n"
        "then either place it next to this script, set the CHESS_ENGINE_PATH "
        "environment variable to its full path, or pass engine_path= explicitly."
    )


class ChessEngine:
    """Manages one running `chess --engine` subprocess for one game session."""

    def __init__(self, engine_path: Optional[str] = None):
        self.binary_path = find_engine_binary(engine_path)
        self.process: Optional[subprocess.Popen] = None
        self._start()

    def _start(self) -> None:
        self.process = subprocess.Popen(
            [self.binary_path, "--engine"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,  # line-buffered
        )

    def _read_block(self) -> EngineState:
        assert self.process is not None and self.process.stdout is not None
        state = EngineState()
        saw_start = False
        while True:
            line = self.process.stdout.readline()
            if line == "":
                raise EngineProtocolError(
                    "Engine process closed its output unexpectedly."
                )
            line = line.rstrip("\n")
            if line == "BOARD_START":
                saw_start = True
                continue
            if not saw_start:
                # Skip any stray output until we see a real block start
                continue
            if line == "BOARD_END":
                return state
            if line.startswith("FEN "):
                state.fen = line[len("FEN "):]
            elif line.startswith("STATUS "):
                state.status = line[len("STATUS "):]
            elif line.startswith("LASTERROR "):
                state.last_error = line[len("LASTERROR "):]
            elif line.startswith("LEGAL"):
                rest = line[len("LEGAL"):].strip()
                state.legal_moves = rest.split() if rest else []

    def initial_state(self) -> EngineState:
        """Read the very first status block the engine prints on startup."""
        return self._read_block()

    def send_move(self, uci_move: str) -> EngineState:
        """Send a move like 'e2e4' or 'e7e8q' and return the new state."""
        assert self.process is not None and self.process.stdin is not None
        self.process.stdin.write(uci_move.strip() + "\n")
        self.process.stdin.flush()
        return self._read_block()

    def quit(self) -> None:
        if self.process is None:
            return
        try:
            if self.process.stdin:
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
            self.process.wait(timeout=2)
        except Exception:
            self.process.kill()
        finally:
            self.process = None

    def is_alive(self) -> bool:
        return self.process is not None and self.process.poll() is None
