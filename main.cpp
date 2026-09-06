// Console Chess Game in C++
// Features: full legal move generation, castling, en passant, promotion,
// check/checkmate/stalemate detection, simple algebraic move input (e2e4, e7e8q).

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>

enum Color { WHITE, BLACK, NONE_COLOR };
enum PieceType { EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

struct Piece {
    PieceType type = EMPTY;
    Color color = NONE_COLOR;
};

struct Move {
    int from = -1, to = -1;
    PieceType promotion = EMPTY;
    bool isCastleKingSide = false;
    bool isCastleQueenSide = false;
    bool isEnPassant = false;
    bool isDoublePawnPush = false;
};

static Color opposite(Color c) { return c == WHITE ? BLACK : WHITE; }

// Board squares indexed 0..63, 0 = a1, 7 = h1, 56 = a8, 63 = h8
static inline int fileOf(int sq) { return sq % 8; }
static inline int rankOf(int sq) { return sq / 8; }
static inline int sq(int file, int rank) { return rank * 8 + file; }
static inline bool onBoard(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }

class Board {
public:
    Piece squares[64];
    Color turn = WHITE;
    bool castleWK = true, castleWQ = true, castleBK = true, castleBQ = true;
    int epSquare = -1; // en passant target square, -1 if none
    int halfmoveClock = 0;
    int fullmoveNumber = 1;

    Board() { setupStartPosition(); }

    void setupStartPosition() {
        for (int i = 0; i < 64; i++) squares[i] = Piece{EMPTY, NONE_COLOR};
        PieceType backRank[8] = { ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK };
        for (int f = 0; f < 8; f++) {
            squares[sq(f, 0)] = Piece{backRank[f], WHITE};
            squares[sq(f, 1)] = Piece{PAWN, WHITE};
            squares[sq(f, 6)] = Piece{PAWN, BLACK};
            squares[sq(f, 7)] = Piece{backRank[f], BLACK};
        }
        turn = WHITE;
        castleWK = castleWQ = castleBK = castleBQ = true;
        epSquare = -1;
        halfmoveClock = 0;
        fullmoveNumber = 1;
    }

    bool isPathClear(int from, int to) const {
        int ff = fileOf(from), fr = rankOf(from);
        int tf = fileOf(to), tr = rankOf(to);
        int df = (tf > ff) - (tf < ff);
        int dr = (tr > fr) - (tr < fr);
        int f = ff + df, r = fr + dr;
        while (f != tf || r != tr) {
            if (squares[sq(f, r)].type != EMPTY) return false;
            f += df; r += dr;
        }
        return true;
    }

    int findKing(Color c) const {
        for (int i = 0; i < 64; i++)
            if (squares[i].type == KING && squares[i].color == c) return i;
        return -1;
    }

    // Is square 'target' attacked by side 'by'?
    bool isSquareAttacked(int target, Color by) const {
        int tf = fileOf(target), tr = rankOf(target);

        // Pawn attacks
        int pawnDir = (by == WHITE) ? -1 : 1; // attacker pawn moves opposite to find source
        int srcRank = tr + pawnDir;
        for (int df : {-1, 1}) {
            int srcFile = tf + df;
            if (onBoard(srcFile, srcRank)) {
                const Piece &p = squares[sq(srcFile, srcRank)];
                if (p.type == PAWN && p.color == by) return true;
            }
        }

        // Knight attacks
        static const int kdx[8] = {1,2,2,1,-1,-2,-2,-1};
        static const int kdy[8] = {2,1,-1,-2,-2,-1,1,2};
        for (int i = 0; i < 8; i++) {
            int f = tf + kdx[i], r = tr + kdy[i];
            if (onBoard(f, r)) {
                const Piece &p = squares[sq(f, r)];
                if (p.type == KNIGHT && p.color == by) return true;
            }
        }

        // King attacks
        for (int df = -1; df <= 1; df++)
            for (int dr = -1; dr <= 1; dr++) {
                if (df == 0 && dr == 0) continue;
                int f = tf + df, r = tr + dr;
                if (onBoard(f, r)) {
                    const Piece &p = squares[sq(f, r)];
                    if (p.type == KING && p.color == by) return true;
                }
            }

        // Sliding pieces: bishop/queen diagonals
        static const int diagX[4] = {1,1,-1,-1};
        static const int diagY[4] = {1,-1,1,-1};
        for (int d = 0; d < 4; d++) {
            int f = tf + diagX[d], r = tr + diagY[d];
            while (onBoard(f, r)) {
                const Piece &p = squares[sq(f, r)];
                if (p.type != EMPTY) {
                    if (p.color == by && (p.type == BISHOP || p.type == QUEEN)) return true;
                    break;
                }
                f += diagX[d]; r += diagY[d];
            }
        }

        // Sliding pieces: rook/queen straights
        static const int straightX[4] = {1,-1,0,0};
        static const int straightY[4] = {0,0,1,-1};
        for (int d = 0; d < 4; d++) {
            int f = tf + straightX[d], r = tr + straightY[d];
            while (onBoard(f, r)) {
                const Piece &p = squares[sq(f, r)];
                if (p.type != EMPTY) {
                    if (p.color == by && (p.type == ROOK || p.type == QUEEN)) return true;
                    break;
                }
                f += straightX[d]; r += straightY[d];
            }
        }

        return false;
    }

    bool isInCheck(Color c) const {
        int k = findKing(c);
        if (k == -1) return false;
        return isSquareAttacked(k, opposite(c));
    }

    // Generate pseudo-legal moves (does not filter for leaving own king in check)
    std::vector<Move> generatePseudoLegalMoves(Color side) const {
        std::vector<Move> moves;
        for (int from = 0; from < 64; from++) {
            const Piece &p = squares[from];
            if (p.type == EMPTY || p.color != side) continue;
            int f = fileOf(from), r = rankOf(from);

            switch (p.type) {
                case PAWN: {
                    int dir = (side == WHITE) ? 1 : -1;
                    int startRank = (side == WHITE) ? 1 : 6;
                    int promoRank = (side == WHITE) ? 7 : 0;
                    // single push
                    if (onBoard(f, r + dir) && squares[sq(f, r + dir)].type == EMPTY) {
                        int to = sq(f, r + dir);
                        if (rankOf(to) == promoRank) {
                            for (PieceType promo : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                                Move m; m.from = from; m.to = to; m.promotion = promo;
                                moves.push_back(m);
                            }
                        } else {
                            Move m; m.from = from; m.to = to;
                            moves.push_back(m);
                        }
                        // double push
                        if (r == startRank && squares[sq(f, r + 2 * dir)].type == EMPTY) {
                            Move m2; m2.from = from; m2.to = sq(f, r + 2 * dir); m2.isDoublePawnPush = true;
                            moves.push_back(m2);
                        }
                    }
                    // captures
                    for (int df : {-1, 1}) {
                        int cf = f + df, cr = r + dir;
                        if (!onBoard(cf, cr)) continue;
                        int to = sq(cf, cr);
                        const Piece &target = squares[to];
                        if (target.type != EMPTY && target.color == opposite(side)) {
                            if (rankOf(to) == promoRank) {
                                for (PieceType promo : {QUEEN, ROOK, BISHOP, KNIGHT}) {
                                    Move m; m.from = from; m.to = to; m.promotion = promo;
                                    moves.push_back(m);
                                }
                            } else {
                                Move m; m.from = from; m.to = to;
                                moves.push_back(m);
                            }
                        } else if (to == epSquare && epSquare != -1) {
                            Move m; m.from = from; m.to = to; m.isEnPassant = true;
                            moves.push_back(m);
                        }
                    }
                    break;
                }
                case KNIGHT: {
                    static const int kdx[8] = {1,2,2,1,-1,-2,-2,-1};
                    static const int kdy[8] = {2,1,-1,-2,-2,-1,1,2};
                    for (int i = 0; i < 8; i++) {
                        int nf = f + kdx[i], nr = r + kdy[i];
                        if (!onBoard(nf, nr)) continue;
                        int to = sq(nf, nr);
                        if (squares[to].type == EMPTY || squares[to].color != side) {
                            Move m; m.from = from; m.to = to;
                            moves.push_back(m);
                        }
                    }
                    break;
                }
                case BISHOP:
                case ROOK:
                case QUEEN: {
                    std::vector<std::pair<int,int>> dirs;
                    if (p.type == BISHOP || p.type == QUEEN) {
                        dirs.push_back({1,1}); dirs.push_back({1,-1});
                        dirs.push_back({-1,1}); dirs.push_back({-1,-1});
                    }
                    if (p.type == ROOK || p.type == QUEEN) {
                        dirs.push_back({1,0}); dirs.push_back({-1,0});
                        dirs.push_back({0,1}); dirs.push_back({0,-1});
                    }
                    for (auto &d : dirs) {
                        int nf = f + d.first, nr = r + d.second;
                        while (onBoard(nf, nr)) {
                            int to = sq(nf, nr);
                            if (squares[to].type == EMPTY) {
                                Move m; m.from = from; m.to = to;
                                moves.push_back(m);
                            } else {
                                if (squares[to].color != side) {
                                    Move m; m.from = from; m.to = to;
                                    moves.push_back(m);
                                }
                                break;
                            }
                            nf += d.first; nr += d.second;
                        }
                    }
                    break;
                }
                case KING: {
                    for (int df = -1; df <= 1; df++) {
                        for (int dr = -1; dr <= 1; dr++) {
                            if (df == 0 && dr == 0) continue;
                            int nf = f + df, nr = r + dr;
                            if (!onBoard(nf, nr)) continue;
                            int to = sq(nf, nr);
                            if (squares[to].type == EMPTY || squares[to].color != side) {
                                Move m; m.from = from; m.to = to;
                                moves.push_back(m);
                            }
                        }
                    }
                    // Castling
                    Color enemy = opposite(side);
                    if (side == WHITE && from == sq(4, 0) && !isInCheck(WHITE)) {
                        if (castleWK && squares[sq(5,0)].type == EMPTY && squares[sq(6,0)].type == EMPTY
                            && squares[sq(7,0)].type == ROOK && squares[sq(7,0)].color == WHITE
                            && !isSquareAttacked(sq(5,0), enemy) && !isSquareAttacked(sq(6,0), enemy)) {
                            Move m; m.from = from; m.to = sq(6,0); m.isCastleKingSide = true;
                            moves.push_back(m);
                        }
                        if (castleWQ && squares[sq(3,0)].type == EMPTY && squares[sq(2,0)].type == EMPTY
                            && squares[sq(1,0)].type == EMPTY
                            && squares[sq(0,0)].type == ROOK && squares[sq(0,0)].color == WHITE
                            && !isSquareAttacked(sq(3,0), enemy) && !isSquareAttacked(sq(2,0), enemy)) {
                            Move m; m.from = from; m.to = sq(2,0); m.isCastleQueenSide = true;
                            moves.push_back(m);
                        }
                    } else if (side == BLACK && from == sq(4, 7) && !isInCheck(BLACK)) {
                        if (castleBK && squares[sq(5,7)].type == EMPTY && squares[sq(6,7)].type == EMPTY
                            && squares[sq(7,7)].type == ROOK && squares[sq(7,7)].color == BLACK
                            && !isSquareAttacked(sq(5,7), enemy) && !isSquareAttacked(sq(6,7), enemy)) {
                            Move m; m.from = from; m.to = sq(6,7); m.isCastleKingSide = true;
                            moves.push_back(m);
                        }
                        if (castleBQ && squares[sq(3,7)].type == EMPTY && squares[sq(2,7)].type == EMPTY
                            && squares[sq(1,7)].type == EMPTY
                            && squares[sq(0,7)].type == ROOK && squares[sq(0,7)].color == BLACK
                            && !isSquareAttacked(sq(3,7), enemy) && !isSquareAttacked(sq(2,7), enemy)) {
                            Move m; m.from = from; m.to = sq(2,7); m.isCastleQueenSide = true;
                            moves.push_back(m);
                        }
                    }
                    break;
                }
                default: break;
            }
        }
        return moves;
    }

    // Apply a move to the board (no legality check). Returns info needed for undo if desired.
    void applyMove(const Move &m) {
        Piece moving = squares[m.from];
        Color side = moving.color;

        // Update en passant target for next move
        int newEp = -1;
        if (moving.type == PAWN && m.isDoublePawnPush) {
            newEp = (m.from + m.to) / 2;
        }

        // Handle en passant capture
        if (m.isEnPassant) {
            int capturedSq = sq(fileOf(m.to), rankOf(m.from));
            squares[capturedSq] = Piece{EMPTY, NONE_COLOR};
        }

        // Move the piece
        squares[m.to] = moving;
        squares[m.from] = Piece{EMPTY, NONE_COLOR};

        // Handle promotion
        if (m.promotion != EMPTY) {
            squares[m.to].type = m.promotion;
        }

        // Handle castling rook move
        if (m.isCastleKingSide) {
            int rank = (side == WHITE) ? 0 : 7;
            squares[sq(5, rank)] = squares[sq(7, rank)];
            squares[sq(7, rank)] = Piece{EMPTY, NONE_COLOR};
        } else if (m.isCastleQueenSide) {
            int rank = (side == WHITE) ? 0 : 7;
            squares[sq(3, rank)] = squares[sq(0, rank)];
            squares[sq(0, rank)] = Piece{EMPTY, NONE_COLOR};
        }

        // Update castling rights
        if (moving.type == KING) {
            if (side == WHITE) { castleWK = false; castleWQ = false; }
            else { castleBK = false; castleBQ = false; }
        }
        if (moving.type == ROOK) {
            if (m.from == sq(0,0)) castleWQ = false;
            if (m.from == sq(7,0)) castleWK = false;
            if (m.from == sq(0,7)) castleBQ = false;
            if (m.from == sq(7,7)) castleBK = false;
        }
        // If a rook is captured on its home square, revoke castling too
        if (m.to == sq(0,0)) castleWQ = false;
        if (m.to == sq(7,0)) castleWK = false;
        if (m.to == sq(0,7)) castleBQ = false;
        if (m.to == sq(7,7)) castleBK = false;

        epSquare = newEp;
        turn = opposite(turn);
        if (side == BLACK) fullmoveNumber++;
    }

    // Generate fully legal moves: pseudo-legal moves that don't leave own king in check
    std::vector<Move> generateLegalMoves(Color side) {
        std::vector<Move> pseudo = generatePseudoLegalMoves(side);
        std::vector<Move> legal;
        for (const Move &m : pseudo) {
            Board copy = *this;
            copy.applyMove(m);
            if (!copy.isInCheck(side)) {
                legal.push_back(m);
            }
        }
        return legal;
    }

    bool hasAnyLegalMove(Color side) {
        return !generateLegalMoves(side).empty();
    }

    char pieceChar(const Piece &p) const {
        char c;
        switch (p.type) {
            case PAWN: c = 'p'; break;
            case KNIGHT: c = 'n'; break;
            case BISHOP: c = 'b'; break;
            case ROOK: c = 'r'; break;
            case QUEEN: c = 'q'; break;
            case KING: c = 'k'; break;
            default: return '.';
        }
        return (p.color == WHITE) ? std::toupper(c) : c;
    }

    // Standard FEN (Forsyth-Edwards Notation) for the current position.
    std::string toFEN() const {
        std::ostringstream out;
        for (int r = 7; r >= 0; r--) {
            int emptyCount = 0;
            for (int f = 0; f < 8; f++) {
                const Piece &p = squares[sq(f, r)];
                if (p.type == EMPTY) {
                    emptyCount++;
                } else {
                    if (emptyCount > 0) { out << emptyCount; emptyCount = 0; }
                    out << pieceChar(p);
                }
            }
            if (emptyCount > 0) out << emptyCount;
            if (r > 0) out << '/';
        }
        out << ' ' << (turn == WHITE ? 'w' : 'b') << ' ';
        std::string castling;
        if (castleWK) castling += 'K';
        if (castleWQ) castling += 'Q';
        if (castleBK) castling += 'k';
        if (castleBQ) castling += 'q';
        out << (castling.empty() ? "-" : castling) << ' ';
        out << (epSquare == -1 ? "-" : squareNameStatic(epSquare)) << ' ';
        out << halfmoveClock << ' ' << fullmoveNumber;
        return out.str();
    }

    static std::string squareNameStatic(int s) {
        std::string res;
        res += char('a' + fileOf(s));
        res += char('1' + rankOf(s));
        return res;
    }

    void print() const {
        std::cout << "\n";
        for (int r = 7; r >= 0; r--) {
            std::cout << (r + 1) << "  ";
            for (int f = 0; f < 8; f++) {
                std::cout << pieceChar(squares[sq(f, r)]) << " ";
            }
            std::cout << "\n";
        }
        std::cout << "\n   a b c d e f g h\n\n";
    }
};

static std::string squareName(int s) {
    std::string res;
    res += char('a' + fileOf(s));
    res += char('1' + rankOf(s));
    return res;
}

static bool parseSquare(const std::string &str, int &outSq) {
    if (str.size() != 2) return false;
    char fc = std::tolower(str[0]);
    char rc = str[1];
    if (fc < 'a' || fc > 'h') return false;
    if (rc < '1' || rc > '8') return false;
    outSq = sq(fc - 'a', rc - '1');
    return true;
}

// Parse user input like "e2e4" or "e7e8q". Returns true and fills 'from','to','promo' on success.
static bool parseInput(const std::string &input, int &from, int &to, PieceType &promo) {
    std::string s = input;
    s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
    if (s.size() < 4 || s.size() > 5) return false;
    std::string fromStr = s.substr(0, 2);
    std::string toStr = s.substr(2, 2);
    if (!parseSquare(fromStr, from)) return false;
    if (!parseSquare(toStr, to)) return false;
    promo = EMPTY;
    if (s.size() == 5) {
        char pc = std::tolower(s[4]);
        switch (pc) {
            case 'q': promo = QUEEN; break;
            case 'r': promo = ROOK; break;
            case 'b': promo = BISHOP; break;
            case 'n': promo = KNIGHT; break;
            default: return false;
        }
    }
    return true;
}

static void printHelp() {
    std::cout << "\nCommands:\n"
              << "  Enter a move like: e2e4  (from-square to-square)\n"
              << "  Promotion example: e7e8q  (q=queen, r=rook, b=bishop, n=knight)\n"
              << "  'moves' - list all legal moves for the side to move\n"
              << "  'help'  - show this help\n"
              << "  'quit'  - exit the game\n\n";
}

// ---------------------------------------------------------------------------
// Engine mode: a machine-readable protocol over stdin/stdout so external
// frontends (e.g. the Streamlit GUI) can drive this same C++ engine without
// parsing the human-facing ASCII board. Run with: ./chess --engine
//
// After startup and after every command, one status block is printed:
//
//   BOARD_START
//   FEN <fen string>
//   STATUS <ONGOING|CHECK|CHECKMATE|STALEMATE>
//   LASTERROR <message, or NONE>
//   LEGAL <space-separated uci moves, e.g. e2e4 e7e8q>
//   BOARD_END
//
// Input commands (one per line on stdin):
//   <uci move>   e.g. "e2e4" or "e7e8q"  -> attempts the move
//   "moves"      -> reprints the block (LEGAL already included every time)
//   "quit"       -> prints "BYE" and exits
// ---------------------------------------------------------------------------
static void printEngineBlock(Board &board, const std::string &lastError) {
    Color side = board.turn;
    std::vector<Move> legalMoves = board.generateLegalMoves(side);
    bool inCheck = board.isInCheck(side);

    std::string status;
    if (legalMoves.empty()) {
        status = inCheck ? "CHECKMATE" : "STALEMATE";
    } else {
        status = inCheck ? "CHECK" : "ONGOING";
    }

    std::cout << "BOARD_START\n";
    std::cout << "FEN " << board.toFEN() << "\n";
    std::cout << "STATUS " << status << "\n";
    std::cout << "LASTERROR " << (lastError.empty() ? "NONE" : lastError) << "\n";
    std::cout << "LEGAL";
    for (const Move &m : legalMoves) {
        std::cout << " " << squareName(m.from) << squareName(m.to);
        if (m.promotion != EMPTY) {
            char pc = 'q';
            switch (m.promotion) {
                case QUEEN: pc = 'q'; break;
                case ROOK: pc = 'r'; break;
                case BISHOP: pc = 'b'; break;
                case KNIGHT: pc = 'n'; break;
                default: break;
            }
            std::cout << pc;
        }
    }
    std::cout << "\n";
    std::cout << "BOARD_END" << std::endl; // flush
}

static int runEngineMode() {
    Board board;
    std::cout.setf(std::ios::unitbuf); // auto-flush every write
    printEngineBlock(board, "");

    std::string line;
    while (std::getline(std::cin, line)) {
        std::string trimmed = line;
        while (!trimmed.empty() && std::isspace(trimmed.front())) trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && std::isspace(trimmed.back())) trimmed.pop_back();
        if (trimmed.empty()) continue;

        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "quit" || lower == "exit") {
            std::cout << "BYE" << std::endl;
            break;
        }
        if (lower == "moves") {
            printEngineBlock(board, "");
            continue;
        }

        int from, to;
        PieceType promo;
        if (!parseInput(trimmed, from, to, promo)) {
            printEngineBlock(board, "invalid_format");
            continue;
        }

        std::vector<Move> legalMoves = board.generateLegalMoves(board.turn);
        Move *chosen = nullptr;
        for (Move &m : legalMoves) {
            if (m.from == from && m.to == to) {
                if (m.promotion != EMPTY) {
                    PieceType wantPromo = (promo == EMPTY) ? QUEEN : promo;
                    if (m.promotion == wantPromo) { chosen = &m; break; }
                } else {
                    chosen = &m; break;
                }
            }
        }

        if (!chosen) {
            printEngineBlock(board, "illegal_move");
            continue;
        }

        board.applyMove(*chosen);
        printEngineBlock(board, "");
    }
    return 0;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--engine") {
            return runEngineMode();
        }
    }

    Board board;
    std::cout << "=====================================\n";
    std::cout << "        Console Chess (C++)\n";
    std::cout << "=====================================\n";
    std::cout << "White is uppercase, Black is lowercase.\n";
    printHelp();

    while (true) {
        board.print();

        Color side = board.turn;
        std::vector<Move> legalMoves = board.generateLegalMoves(side);

        bool inCheck = board.isInCheck(side);
        if (legalMoves.empty()) {
            if (inCheck) {
                std::cout << (side == WHITE ? "White" : "Black") << " is checkmated. "
                          << (side == WHITE ? "Black" : "White") << " wins!\n";
            } else {
                std::cout << "Stalemate. The game is a draw.\n";
            }
            break;
        }

        if (inCheck) {
            std::cout << (side == WHITE ? "White" : "Black") << " is in check!\n";
        }

        std::cout << (side == WHITE ? "White" : "Black") << " to move";
        std::cout << " (move #" << board.fullmoveNumber << "): ";

        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::string trimmed = line;
        // basic trim
        while (!trimmed.empty() && std::isspace(trimmed.front())) trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && std::isspace(trimmed.back())) trimmed.pop_back();

        std::string lower = trimmed;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "quit" || lower == "exit") {
            std::cout << "Goodbye!\n";
            break;
        }
        if (lower == "help") {
            printHelp();
            continue;
        }
        if (lower == "moves") {
            std::cout << "Legal moves:";
            for (const Move &m : legalMoves) {
                std::cout << " " << squareName(m.from) << squareName(m.to);
                if (m.promotion != EMPTY) {
                    char pc = 'q';
                    switch (m.promotion) {
                        case QUEEN: pc = 'q'; break;
                        case ROOK: pc = 'r'; break;
                        case BISHOP: pc = 'b'; break;
                        case KNIGHT: pc = 'n'; break;
                        default: break;
                    }
                    std::cout << pc;
                }
            }
            std::cout << "\n";
            continue;
        }

        int from, to;
        PieceType promo;
        if (!parseInput(trimmed, from, to, promo)) {
            std::cout << "Invalid input format. Type 'help' for instructions.\n";
            continue;
        }

        // Find a matching legal move
        Move *chosen = nullptr;
        for (Move &m : legalMoves) {
            if (m.from == from && m.to == to) {
                if (m.promotion != EMPTY) {
                    if (promo == EMPTY) promo = QUEEN; // default to queen if unspecified
                    if (m.promotion == promo) { chosen = &m; break; }
                } else {
                    chosen = &m; break;
                }
            }
        }

        if (!chosen) {
            std::cout << "That move is not legal. Type 'moves' to see legal moves.\n";
            continue;
        }

        board.applyMove(*chosen);
    }

    return 0;
}
