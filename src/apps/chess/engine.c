#include "engine.h"
static int absval(int x) { return x < 0 ? -x : x; }
static int color(int x) { return (x > 0) - (x < 0); }
static int inside(int r,int c) { return r>=0 && r<8 && c>=0 && c<8; }
void chess_reset(Position *p) {
    static const int back[8]={ROOK,KNIGHT,BISHOP,QUEEN,KING,BISHOP,KNIGHT,ROOK};
    for(int i=0;i<64;i++) p->board[i]=0;
    for(int i=0;i<8;i++) { p->board[i]=-back[i]; p->board[i+8]=-PAWN; p->board[i+48]=PAWN; p->board[i+56]=back[i]; }
    p->side=1; p->castle=15; p->ep=-1; p->halfmove=0;
}
static int attacked(const Position *p,int sq,int side) {
    int r=sq/8,c=sq%8;
    for(int i=0;i<64;i++) {
        int piece=p->board[i]; if(color(piece)!=side) continue;
        int dr=r-i/8,dc=c-i%8,t=absval(piece);
        if(t==PAWN && dr==-side && absval(dc)==1) return 1;
        if(t==KNIGHT && ((absval(dr)==2 && absval(dc)==1)||(absval(dr)==1 && absval(dc)==2))) return 1;
        if(t==KING && absval(dr)<=1 && absval(dc)<=1) return 1;
        if((dr || dc) && ((t==BISHOP && absval(dr)==absval(dc)) ||
           (t==ROOK && (!dr || !dc)) || (t==QUEEN && (!dr || !dc || absval(dr)==absval(dc))))) {
            int rr=i/8,cc=i%8,sr=color(dr),sc=color(dc);
            rr+=sr; cc+=sc;
            while(rr!=r || cc!=c) { if(p->board[rr*8+cc]) break; rr+=sr; cc+=sc; }
            if(rr==r && cc==c) return 1;
        }
    }
    return 0;
}
int chess_check(const Position *p,int side) {
    for(int i=0;i<64;i++) if(p->board[i]==side*KING) return attacked(p,i,-side);
    return 1;
}
void chess_play(Position *p,Move m) {
    int piece=p->board[m.from],target=p->board[m.to],t=absval(piece),side=p->side;
    if(t==PAWN && m.to==p->ep && !target) p->board[m.to+side*8]=0;
    p->board[m.to]=m.promotion ? side*m.promotion : piece; p->board[m.from]=0;
    if(t==KING) {
        p->castle &= side==1 ? ~3 : ~12;
        if(m.to-m.from==2) { p->board[m.to-1]=p->board[m.to+1]; p->board[m.to+1]=0; }
        if(m.from-m.to==2) { p->board[m.to+1]=p->board[m.to-2]; p->board[m.to-2]=0; }
    }
    if(m.from==63 || m.to==63) p->castle &= ~1;
    if(m.from==56 || m.to==56) p->castle &= ~2;
    if(m.from==7 || m.to==7) p->castle &= ~4;
    if(m.from==0 || m.to==0) p->castle &= ~8;
    p->ep=t==PAWN && absval(m.to-m.from)==16 ? (m.to+m.from)/2 : -1;
    p->halfmove=t==PAWN || target ? 0 : p->halfmove+1;
    p->side=-side;
}
static void add(const Position *p,Move *moves,int *n,int from,int to) {
    if(absval(p->board[to])==KING) return;
    int promote=absval(p->board[from])==PAWN && (to<8 || to>=56);
    for(int t=promote?KNIGHT:0;t<=(promote?QUEEN:0);t++) {
        Move m={from,to,t}; Position q=*p; chess_play(&q,m);
        if(!chess_check(&q,p->side) && *n<MAX_MOVES) moves[(*n)++]=m;
    }
}
int chess_moves(const Position *p,Move *moves) {
    static const int dirs[8][2]={{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
    static const int knights[8][2]={{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    int n=0,side=p->side;
    for(int i=0;i<64;i++) {
        if(color(p->board[i])!=side) continue;
        int t=absval(p->board[i]),r=i/8,c=i%8;
        if(t==PAWN) {
            int rr=r-side;
            if(inside(rr,c) && !p->board[rr*8+c]) {
                add(p,moves,&n,i,rr*8+c);
                if(r==(side==1?6:1) && !p->board[(r-2*side)*8+c]) add(p,moves,&n,i,(r-2*side)*8+c);
            }
            for(int dc=-1;dc<=1;dc+=2) if(inside(rr,c+dc)) {
                int to=rr*8+c+dc;
                if(color(p->board[to])==-side || (to==p->ep && p->board[to+side*8]==-side*PAWN)) add(p,moves,&n,i,to);
            }
        } else if(t==KNIGHT) {
            for(int d=0;d<8;d++) { int rr=r+knights[d][0],cc=c+knights[d][1];
                if(inside(rr,cc) && color(p->board[rr*8+cc])!=side) add(p,moves,&n,i,rr*8+cc);
            }
        } else {
            for(int d=0;d<8;d++) {
                if((t==BISHOP && d<4)||(t==ROOK && d>=4)) continue;
                int rr=r+dirs[d][0],cc=c+dirs[d][1];
                while(inside(rr,cc)) {
                    int to=rr*8+cc;
                    if(color(p->board[to])==side) break;
                    add(p,moves,&n,i,to);
                    if(p->board[to] || t==KING) break;
                    rr+=dirs[d][0]; cc+=dirs[d][1];
                }
            }
            if(t==KING && i==(side==1?60:4) && !chess_check(p,side)) {
                int rights=side==1?1:4;
                if((p->castle&rights) && p->board[i+3]==side*ROOK && !p->board[i+1] && !p->board[i+2] && !attacked(p,i+1,-side)) add(p,moves,&n,i,i+2);
                if((p->castle&(rights*2)) && p->board[i-4]==side*ROOK && !p->board[i-1] && !p->board[i-2] && !p->board[i-3] && !attacked(p,i-1,-side)) add(p,moves,&n,i,i-2);
            }
        }
    }
    return n;
}
int chess_material_draw(const Position *p) {
    int minors=0,knights=0,bishop_color=-1,mixed=0;
    for(int i=0;i<64;i++) {
        int t=absval(p->board[i]);
        if(t==PAWN || t==ROOK || t==QUEEN) return 0;
        if(t==KNIGHT) { minors++; knights++; }
        if(t==BISHOP) { int c=(i/8+i%8)&1; minors++; if(bishop_color>=0 && c!=bishop_color) mixed=1; bishop_color=c; }
    }
    return minors<=1 || (!knights && !mixed);
}
static int evaluate(const Position *p) {
    static const int value[7]={0,100,320,330,500,900,0}; int score=0;
    for(int i=0;i<64;i++) {
        int pc=p->board[i],t=absval(pc),s=color(pc),r=i/8,c=i%8;
        int center=6-absval(2*r-7)/2-absval(2*c-7)/2;
        score+=s*(value[t]+((t==KNIGHT || t==BISHOP)?center*8:0)+(t==PAWN?(s==1?6-r:r-1)*8:0));
    }
    return score*p->side;
}
static int search(const Position *p,int depth,int alpha,int beta,int ply) {
    Move moves[MAX_MOVES]; int n=chess_moves(p,moves);
    if(!n) return chess_check(p,p->side)?-30000+ply:0;
    if(p->halfmove>=100 || chess_material_draw(p)) return 0;
    if(!depth) return evaluate(p);
    /* Captures first improve alpha-beta pruning without dynamic allocation. */
    for(int i=0;i<n;i++) for(int j=i+1;j<n;j++)
        if(absval(p->board[moves[j].to])+moves[j].promotion > absval(p->board[moves[i].to])+moves[i].promotion) { Move m=moves[i]; moves[i]=moves[j]; moves[j]=m; }
    for(int i=0;i<n;i++) { Position q=*p; chess_play(&q,moves[i]); int v=-search(&q,depth-1,-beta,-alpha,ply+1); if(v>alpha) alpha=v; if(alpha>=beta) break; }
    return alpha;
}
int chess_best(const Position *p,Move *best,int depth) {
    Move moves[MAX_MOVES]; int n=chess_moves(p,moves),score=-32000;
    for(int i=0;i<n;i++) { Position q=*p; chess_play(&q,moves[i]); int v=-search(&q,depth>0?depth-1:0,-32000,-score,1); if(v>score) { score=v; *best=moves[i]; } }
    return n!=0;
}
