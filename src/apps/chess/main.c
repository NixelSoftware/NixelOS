#include "../../system/user/sys_api.h"
#include "engine.h"
#include "piece_assets.h"
#define CELL 42
#define BX 28
#define BY 70
static uint32_t window;
static Position game, history[128];
static int history_count, selected, pending, promotion, last_from, last_to, over;
static Move legal[MAX_MOVES];
static int legal_count;
static const char *status;
static void draw(int op,int x,int y,int w,int h,const char *text,uint32_t color) {
    user_window_draw_t d={0}; d.operation=op; d.x=x; d.y=y; d.width=w; d.height=h; d.text=text; d.color=color;
    sys_window_draw(window,&d);
}
static void fill(int x,int y,int w,int h,uint32_t c) { draw(USER_WINDOW_DRAW_FILL,x,y,w,h,0,c); }
static void text(int x,int y,const char *s,uint32_t c) { draw(USER_WINDOW_DRAW_TEXT,x,y,0,0,s,c); }
static void button(int x,int w,const char *s) { draw(USER_WINDOW_DRAW_BUTTON,x,30,w,26,s,0xC0C0C0); }
static int same(const Position *a,const Position *b) {
    if(a->side!=b->side || a->castle!=b->castle) return 0;
    /* An en passant square affects repetition only if a legal capture exists. */
    for(int i=0;i<64;i++) if(a->board[i]!=b->board[i]) return 0;
    if(a->ep!=b->ep) {
        Move moves[MAX_MOVES]; const Position *p=a->ep>=0?a:b; int n=chess_moves(p,moves);
        for(int i=0;i<n;i++) if(moves[i].to==p->ep && (p->board[moves[i].from]==PAWN || p->board[moves[i].from]==-PAWN)) return 0;
    }
    return 1;
}
static void update(void) {
    legal_count=chess_moves(&game,legal); over=0;
    if(!legal_count) { over=1; status=chess_check(&game,game.side)?(game.side==1?"Checkmate - Black wins":"Checkmate - White wins"):"Draw - stalemate"; return; }
    if(game.halfmove>=100) { over=1; status="Draw - fifty-move rule"; return; }
    if(chess_material_draw(&game)) { over=1; status="Draw - insufficient material"; return; }
    int repeats=0;
    for(int i=0;i<history_count;i++) if(same(&history[i],&game)) repeats++;
    if(repeats>=3) { over=1; status="Draw - threefold repetition"; return; }
    status=game.side==1?(chess_check(&game,1)?"Your turn - check!":"Your turn (White)"):"Black is thinking...";
}
static void reset(void) {
    chess_reset(&game); history_count=1; history[0]=game; selected=-1; pending=-1; promotion=QUEEN; last_from=last_to=-1; update();
}
static void play(Move m) {
    chess_play(&game,m); last_from=m.from; last_to=m.to; selected=-1; pending=-1;
    if(history_count==128) { for(int i=1;i<128;i++) history[i-1]=history[i]; history_count--; }
    history[history_count++]=game; update();
}
static void render(void) {
    fill(0,22,392,440,0xC0C0C0);
    button(12,94,"New game"); button(114,66,"Undo"); button(188,188,"Promote: Q R B N");
    for(int i=0;i<64;i++) {
        int x=BX+(i%8)*CELL,y=BY+(i/8)*CELL;
        uint32_t bg=((i/8+i%8)&1)?0x709477:0xECE4CB;
        if(i==last_from || i==last_to) bg=0xC5C56A;
        if(i==selected) bg=0xE5B84B;
        if((game.board[i]==KING || game.board[i]==-KING) && chess_check(&game,game.board[i]>0?1:-1)) bg=0xD67265;
        fill(x,y,CELL,CELL,bg);
        if(selected>=0) for(int j=0;j<legal_count;j++) if(legal[j].from==selected && legal[j].to==i) { fill(x+17,y+34,8,4,0x245A42); break; }
        if(game.board[i]) {
            int p=game.board[i],t=p<0?-p:p;
            int asset=(p>0?0:6)+t-1;
            user_window_draw_t sprite={0};
            sprite.operation=USER_WINDOW_DRAW_IMAGE;
            sprite.x=x+4; sprite.y=y+2; sprite.width=34; sprite.height=34;
            sprite.data=piece_assets[asset].data;
            sprite.data_length=piece_assets[asset].size;
            sys_window_draw(window,&sprite);
        }
    }
    for(int i=0;i<8;i++) { char f[2]={(char)('a'+i),0},r[2]={(char)('8'-i),0}; text(BX+i*CELL+17,410,f,0x202820); text(10,BY+i*CELL+14,r,0x202820); }
    text(12,432,pending>=0?"Choose promotion: Q R B N":status,0x202820);
    sys_window_present(window);
}
static void undo(void) {
    if(history_count<=1) return;
    int steps=game.side==1?2:1;
    if(steps>=history_count) steps=history_count-1;
    history_count-=steps; game=history[history_count-1]; selected=pending=-1; last_from=last_to=-1; update();
}
static void choose_promotion(int type) {
    promotion=type;
    if(pending>=0) { Move m={selected,pending,promotion}; play(m); }
}
static void click(int x,int y) {
    if(y>=30 && y<56) {
        if(x>=12 && x<106) reset();
        else if(x>=114 && x<180) undo();
        else if(x>=188 && x<376) { static const int types[4]={QUEEN,ROOK,BISHOP,KNIGHT}; choose_promotion(types[(x-188)*4/188]); }
        return;
    }
    if(over || pending>=0 || game.side!=1 || x<BX || x>=BX+8*CELL || y<BY || y>=BY+8*CELL) return;
    int sq=(y-BY)/CELL*8+(x-BX)/CELL;
    if(selected>=0) for(int i=0;i<legal_count;i++) if(legal[i].from==selected && legal[i].to==sq) {
        if(legal[i].promotion) pending=sq; else play(legal[i]); return;
    }
    selected=game.board[sq]>0 && sq!=selected?sq:-1;
}
void user_main(const char *arguments) {
    (void)arguments; window=0; reset();
    user_window_desc_t d={0}; d.x=150; d.y=40; d.width=392; d.height=462;
    const char *title="Chess"; for(int i=0;title[i];i++) d.title[i]=title[i];
    int32_t handle=sys_window_create(&d); if(handle<0) return; window=(uint32_t)handle;
    int close=0; render();
    while(!close) {
        sys_yield(); user_window_event_t event;
        while(sys_window_poll(window,&event)==1) {
            if(event.type==USER_WINDOW_EVENT_CLOSE) { close=1; break; }
            if(event.type==USER_WINDOW_EVENT_MOUSE && (event.buttons&1)) click(event.x,event.y);
            if(event.type==USER_WINDOW_EVENT_KEY) {
                int c=event.key;
                if(c==1 || c==27) { close=1; break; }
                if(c=='n' || c=='N') { if(pending>=0) choose_promotion(KNIGHT); else reset(); }
                if(c=='u' || c=='U') undo();
                if(c=='q' || c=='Q') choose_promotion(QUEEN);
                if(c=='r' || c=='R') choose_promotion(ROOK);
                if(c=='b' || c=='B') choose_promotion(BISHOP);
            }
            render();
        }
        if(!close && !over && game.side==-1) {
            Move best; if(chess_best(&game,&best,3)) play(best); render();
        }
    }
    sys_window_close(window);
}
