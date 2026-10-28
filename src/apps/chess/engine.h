#ifndef CHESS_ENGINE_H
#define CHESS_ENGINE_H
/* Squares run from a8 (0) to h1 (63); White is positive. */
enum { PAWN=1, KNIGHT, BISHOP, ROOK, QUEEN, KING };
typedef struct { int from, to, promotion; } Move;
typedef struct { int board[64], side, castle, ep, halfmove; } Position;
#define MAX_MOVES 256
void chess_reset(Position *p);
int chess_check(const Position *p, int side);
int chess_moves(const Position *p, Move *moves);
void chess_play(Position *p, Move m);
int chess_best(const Position *p, Move *best, int depth);
int chess_material_draw(const Position *p);
#endif
