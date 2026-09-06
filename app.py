"""
app.py — Streamlit frontend for the C++ chess engine.

All chess rules (legal moves, check/checkmate/stalemate, castling, en
passant, promotion) are computed by the compiled C++ binary (`chess
--engine`), via engine_client.py. This file only renders the board and
turns clicks into move commands sent to that engine.
"""

import streamlit as st
from engine_client import ChessEngine, EngineNotFoundError

st.set_page_config(page_title="C++ Chess", page_icon="♟️", layout="centered")

PIECE_UNICODE = {
    ("P", "w"): "♙", ("N", "w"): "♘", ("B", "w"): "♗",
    ("R", "w"): "♖", ("Q", "w"): "♕", ("K", "w"): "♔",
    ("P", "b"): "♟", ("N", "b"): "♞", ("B", "b"): "♝",
    ("R", "b"): "♜", ("Q", "b"): "♛", ("K", "b"): "♚",
}

LIGHT_SQ = "#EEEED2"
DARK_SQ = "#769656"
SELECTED_SQ = "#F6F669"
TARGET_SQ = "#BACA44"


def file_rank_to_algebraic(file_idx: int, rank_idx_from_top: int) -> str:
    """file_idx: 0=a..7=h. rank_idx_from_top: 0 = rank8 row ... 7 = rank1 row."""
    file_char = chr(ord("a") + file_idx)
    rank_num = 8 - rank_idx_from_top
    return f"{file_char}{rank_num}"


def init_engine():
    if "engine" not in st.session_state:
        try:
            st.session_state.engine = ChessEngine()
            st.session_state.state = st.session_state.engine.initial_state()
            st.session_state.engine_error = None
        except EngineNotFoundError as e:
            st.session_state.engine = None
            st.session_state.state = None
            st.session_state.engine_error = str(e)
    if "selected" not in st.session_state:
        st.session_state.selected = None  # algebraic square string like "e2"
    if "pending_promotion" not in st.session_state:
        st.session_state.pending_promotion = None  # (from_sq, to_sq)
    if "history" not in st.session_state:
        st.session_state.history = []


def reset_game():
    if st.session_state.get("engine") is not None:
        st.session_state.engine.quit()
    st.session_state.engine = ChessEngine()
    st.session_state.state = st.session_state.engine.initial_state()
    st.session_state.selected = None
    st.session_state.pending_promotion = None
    st.session_state.history = []


def try_move(from_sq: str, to_sq: str, promotion: str = ""):
    state = st.session_state.state
    uci = from_sq + to_sq + promotion
    # Check whether this exact move (with this promotion) is legal
    matches = [m for m in state.legal_moves if m[:4] == from_sq + to_sq]
    if not matches:
        st.session_state.selected = None
        return

    needs_promo_choice = any(len(m) == 5 for m in matches) and not promotion
    if needs_promo_choice:
        st.session_state.pending_promotion = (from_sq, to_sq)
        return

    new_state = st.session_state.engine.send_move(uci)
    if new_state.last_error == "NONE":
        st.session_state.history.append(uci)
    st.session_state.state = new_state
    st.session_state.selected = None
    st.session_state.pending_promotion = None


def square_click(algebraic: str):
    state = st.session_state.state
    selected = st.session_state.selected

    if st.session_state.pending_promotion is not None:
        return  # ignore board clicks while a promotion choice is pending

    if selected is None:
        # Select this square only if it holds a piece belonging to the side to move
        rows = state.board_rows
        file_idx = ord(algebraic[0]) - ord("a")
        rank_idx_from_top = 8 - int(algebraic[1])
        piece = rows[rank_idx_from_top][file_idx]
        if piece is not None and piece[1] == state.turn:
            st.session_state.selected = algebraic
        return

    if algebraic == selected:
        st.session_state.selected = None
        return

    legal_targets = {m[2:4] for m in state.legal_moves if m[:2] == selected}
    if algebraic in legal_targets:
        try_move(selected, algebraic)
    else:
        # Maybe switching selection to another of the player's own pieces
        rows = state.board_rows
        file_idx = ord(algebraic[0]) - ord("a")
        rank_idx_from_top = 8 - int(algebraic[1])
        piece = rows[rank_idx_from_top][file_idx]
        if piece is not None and piece[1] == state.turn:
            st.session_state.selected = algebraic
        else:
            st.session_state.selected = None


def render_board():
    state = st.session_state.state
    rows = state.board_rows
    selected = st.session_state.selected
    legal_targets = set()
    if selected:
        legal_targets = {m[2:4] for m in state.legal_moves if m[:2] == selected}

    st.markdown(
        """
        <style>
        div[data-testid="stButton"] > button {
            width: 100%;
            aspect-ratio: 1 / 1;
            font-size: 2rem;
            line-height: 1;
            padding: 0;
            border-radius: 0;
            border: none;
        }
        </style>
        """,
        unsafe_allow_html=True,
    )

    for rank_idx_from_top in range(8):
        cols = st.columns(8, gap="small")
        for file_idx in range(8):
            algebraic = file_rank_to_algebraic(file_idx, rank_idx_from_top)
            piece = rows[rank_idx_from_top][file_idx]
            label = PIECE_UNICODE.get(piece, "") if piece else " "

            is_light = (file_idx + rank_idx_from_top) % 2 == 0
            bg = LIGHT_SQ if is_light else DARK_SQ
            if algebraic == selected:
                bg = SELECTED_SQ
            elif algebraic in legal_targets:
                bg = TARGET_SQ

            with cols[file_idx]:
                st.markdown(
                    f"""<div style="background-color:{bg};
                        border-radius:4px; text-align:center;
                        padding-top:2px; margin-bottom:-46px;
                        position:relative; z-index:0; height:44px;">
                        </div>""",
                    unsafe_allow_html=True,
                )
                if st.button(label, key=f"sq_{algebraic}"):
                    square_click(algebraic)
                    st.rerun()


def render_promotion_picker():
    from_sq, to_sq = st.session_state.pending_promotion
    st.info(f"Pawn promotion: {from_sq} → {to_sq}. Choose a piece:")
    cols = st.columns(4)
    labels = [("Queen", "q"), ("Rook", "r"), ("Bishop", "b"), ("Knight", "n")]
    for col, (name, code) in zip(cols, labels):
        with col:
            if st.button(name, key=f"promo_{code}"):
                try_move(from_sq, to_sq, code)
                st.rerun()


def render_sidebar():
    with st.sidebar:
        st.header("Game")
        if st.button("♻️ New game"):
            reset_game()
            st.rerun()

        st.subheader("Status")
        state = st.session_state.state
        turn_name = "White" if state.turn == "w" else "Black"
        st.write(f"**Turn:** {turn_name}")
        if state.status == "CHECK":
            st.warning(f"{turn_name} is in check!")
        elif state.status == "CHECKMATE":
            winner = "Black" if state.turn == "w" else "White"
            st.success(f"Checkmate! {winner} wins.")
        elif state.status == "STALEMATE":
            st.info("Stalemate — draw.")

        st.subheader("Move history")
        if st.session_state.history:
            pairs = []
            h = st.session_state.history
            for i in range(0, len(h), 2):
                white_move = h[i]
                black_move = h[i + 1] if i + 1 < len(h) else ""
                pairs.append(f"{i // 2 + 1}. {white_move} {black_move}")
            st.text("\n".join(pairs))
        else:
            st.caption("No moves yet.")

        with st.expander("Raw FEN"):
            st.code(state.fen, language=None)


def main():
    st.title("♟️ Chess — C++ Engine + Streamlit UI")
    st.caption(
        "The board and click UI are Streamlit; every rule (legal moves, "
        "check, checkmate, castling, en passant, promotion) is decided by "
        "the compiled C++ engine running underneath."
    )

    init_engine()

    if st.session_state.engine_error:
        st.error(st.session_state.engine_error)
        st.stop()

    render_sidebar()

    if st.session_state.pending_promotion is not None:
        render_promotion_picker()

    render_board()

    if st.session_state.state.status in ("CHECKMATE", "STALEMATE"):
        st.markdown("---")
        st.write("Game over. Click **New game** in the sidebar to play again.")


if __name__ == "__main__":
    main()
