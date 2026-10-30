/* Native ring-3 frontend for upstream Syndig (GPL-3.0-or-later). */
#include "../../kernel/types.h"
#include "../../system/user/sys_api.h"
#include "../../system/graphics/wm/theme.h"
#include "upstream/buffer.h"
#include "upstream/polyphony.h"
#include "upstream/oscillator.h"
#include "upstream/envelope.h"
#include "upstream/lane.h"
#include "upstream/midi.h"
#include "upstream/channel.h"
#include "instruments.h"
#include "opl.h"
static void silence(void){stop_all_sound();if(opl_enabled)opl_stop_all();}

#define W 600
#define H 420
#define AUDIO_FRAMES 1024
#define VISUAL_FRAME_SAMPLES (44100u / 30u)
#define TILE_TOP 331
#define TILE_TARGET 397
#define TILE_HEIGHT 4
#define TILE_LOOKAHEAD (44100u * 3u)
#define TILE_LEFT 16
#define TILE_WIDTH (13*43)
static const uint32_t part_colors[16]={
 COLOR_ACCENT,COLOR_BTN_CLOSE,COLOR_BTN_MAX,0x00C6A0F6,
 0x00F5A97F,0x008BD5CA,0x00F5BDE6,COLOR_BTN_MIN,
 0x00B7BDF8,0x00EE99A0,0x0091D7E3,0x00EDB7D5,
 0x00A5C2F5,0x00D9DBA1,0x00D5B6E3,0x00A3D6CC
};
static uint32_t wh;
static int close_requested, audio_ready;
static int ui_dirty = 1;
static uint64_t last_draw_tick;
static int program, volume=96, attack=20, decay=35, sustain=90, release=30;
static int active_note=-1;
static int16_t pcm[AUDIO_FRAMES*2];
static int pcm_pending;
static uint32_t pcm_offset, pcm_progress_tick;
static int audio_acquired;
static const char* playback_error;
static char audio_device[64];
#include "midi_file.h"
static midi_song_t song;
static int song_loaded, song_playing;
static uint64_t played_frames;
static uint32_t song_index;
static uint16_t held[128];
static uint8_t last_note=255;
static char song_name[96];
static void song_stop(void) { song_playing=0; song_index=0; played_frames=0; for(int i=0;i<128;i++)held[i]=0; last_note=255; silence(); }
static void song_start(void) { song_stop(); instruments_reset(); if(song_loaded) song_playing=1; }
static void song_step(uint32_t frames) {
 if(!song_playing)return;
 uint64_t end=played_frames+frames;
 while(song_index<song.count && song.events[song_index].frame<=end){
  midi_event_t *e=&song.events[song_index++];
  if(e->type==0x90){held[e->note]|=(1u<<e->channel);last_note=e->note;instruments_note_on(e->channel,e->note,e->velocity);}
  else if(e->type==0x80){held[e->note]&=~(1u<<e->channel);instruments_note_off(e->channel,e->note);}
  else if(e->type==0xC0)instruments_program(e->channel,e->note);
  else if(e->type==0xB0){instruments_control(e->channel,e->note,e->velocity);if(e->note==120||e->note==123)for(int n=0;n<128;n++)held[n]&=~(1u<<e->channel);}
 }
 played_frames=end;
 if(song_index>=song.count && played_frames>song.duration_frames+44100u){song_playing=0;ui_dirty=1;silence();}
}

static const char* names[]={"Square","Saw down","Saw up","Triangle","Sine","Noise","Impulse","Wave sq25","Wave sq50","Wave saw dn","Wave saw up","Wave tri","Wave sine","Wave noise","Double pulse"};
static void rect(int x,int y,int w,int h,uint32_t color) {
 user_window_draw_t d={0};d.operation=USER_WINDOW_DRAW_FILL;d.x=x;d.y=y;d.width=w;d.height=h;d.color=color;
 sys_window_draw(wh,&d);
}
static void colored_label(int x,int y,const char *s,uint32_t color) {
 user_window_draw_t d={0};d.operation=USER_WINDOW_DRAW_TEXT;d.x=x;d.y=y;d.text=s;d.color=color;sys_window_draw(wh,&d);
}
static void label(int x,int y,const char *s){colored_label(x,y,s,COLOR_TEXT_PRIMARY);}
static void button(int x,int y,int w,int h,const char *s) {
 user_window_draw_t d={0};d.operation=USER_WINDOW_DRAW_BUTTON;d.x=x;d.y=y;d.width=w;d.height=h;d.text=s;d.color=COLOR_SURFACE;sys_window_draw(wh,&d);
}
static void number(int value,char *s) {
 char t[12];int i=0,j=0;if(!value){s[0]='0';s[1]=0;return;}
 while(value&&i<11){t[i++]=(char)('0'+value%10);value/=10;}
 while(i)s[j++]=t[--i];s[j]=0;
}
static void control(int x,int y,const char *title,int value) {
 char s[12];label(x,y,title);rect(x,y+18,100,12,COLOR_MODERN_BG);rect(x+2,y+20,value*96/127,8,COLOR_ACCENT);
 number(value,s);label(x+105,y+18,s);
}
static void draw(void) {
 user_window_draw_t begin={0};begin.operation=USER_WINDOW_DRAW_BEGIN;
 if(sys_window_draw(wh,&begin)<0)return;
 static uint16_t previous_held[13];
 static int previous_active=-1;
 int full=ui_dirty;
 char s[12];
 if(full){
  rect(0,18,W,H-18,COLOR_SURFACE);
  /* Text syscalls accept 63 characters plus the terminator. */
  char caption[64];const char *title=song_loaded?song_name:"No MIDI file (run: syndig filename.mid)";
  int n=0;while(n<63 && title[n]){caption[n]=title[n];n++;}caption[n]=0;
  label(14,24,caption);
  button(380,155,95,24,song_playing?"PAUSE":"PLAY");button(485,155,95,24,"RESTART");
  label(160,157,"Elapsed / total (sec):");
  number((int)(song.duration_frames/44100u),s);label(240,171,s);
  label(14,47,"Keyboard:");button(108,39,30,26,"<");button(144,39,180,26,names[program]);button(330,39,30,26,">");
  button(410,39,100,26,"STOP ALL");
  control(16,83,"Volume",volume);control(155,83,"Attack",attack);
  control(294,83,"Decay",decay);control(433,83,"Sustain",sustain);
  control(16,135,"Release",release);
  label(160,137,"Click a key to play or release");
  label(16,181,"Keyboard: A W S E D F T G Y H U J K | Space: release");
  label(16,303,playback_error?playback_error:audio_device);
  label(16,197,opl_enabled?"Synth: hardware OPL (9 voices)":"Synth: software");
  for(int ch=0;ch<16;ch++){
   int x=TILE_LEFT+ch*35;
   rect(x,293,6,7,part_colors[ch]);
   number(ch+1,s);label(x+8,293,s);
  }
 }
 rect(160,171,72,8,COLOR_SURFACE);
 number((int)(played_frames/44100u),s);label(160,171,s);
 /* Repaint the moving lane only; avoid clearing the entire window at 30 Hz. */
 rect(TILE_LEFT,TILE_TOP,TILE_WIDTH,TILE_TARGET-TILE_TOP,COLOR_SURFACE);
 if(song_loaded){
  for(uint32_t i=0;i<song.count;i++){
   midi_event_t *e=&song.events[i];
   if(e->frame>played_frames+TILE_LOOKAHEAD)break;
   if(e->type!=0x90||e->end_frame<=played_frames)continue;
   uint64_t start=e->frame>played_frames?e->frame-played_frames:0;
   uint64_t end=e->end_frame-played_frames;
   if(end>TILE_LOOKAHEAD)end=TILE_LOOKAHEAD;
   int span=TILE_TARGET-TILE_TOP;
   int bottom=TILE_TARGET-(int)(start*span/TILE_LOOKAHEAD);
   int top=TILE_TARGET-(int)(end*span/TILE_LOOKAHEAD);
   if(bottom-top<TILE_HEIGHT)top=bottom-TILE_HEIGHT;
   if(top<TILE_TOP)top=TILE_TOP;
   int x=TILE_LEFT+e->note*TILE_WIDTH/128;
   int width=(e->note+1)*TILE_WIDTH/128-e->note*TILE_WIDTH/128;
   rect(x,top,width-1,bottom-top,part_colors[e->channel]);
  }
 }
 rect(TILE_LEFT,TILE_TARGET,TILE_WIDTH,18,COLOR_MODERN_BG);
 for(int note=0;note<128;note++)if(held[note]){
  int x=TILE_LEFT+note*TILE_WIDTH/128;
  int width=(note+1)*TILE_WIDTH/128-note*TILE_WIDTH/128;
  /* Stack colours when several parts hold the same pitch. */
  int parts=0,part=0;
  for(int ch=0;ch<16;ch++)if(held[note]&(1u<<ch))parts++;
  for(int ch=0;ch<16;ch++)if(held[note]&(1u<<ch)){
   int top=part*18/parts,bottom=(part+1)*18/parts;
   rect(x,TILE_TARGET+top,width-1,bottom-top,part_colors[ch]);part++;
  }
 }
 for(int i=0;i<13;i++){
  int x=16+i*43, note=60+i;
  if(full || previous_held[i]!=held[note] ||
     (previous_active==note)!=(active_note==note)){
   rect(x,211,40,80,(note==active_note||held[note])?COLOR_ACCENT:COLOR_TASKBAR_ITEM_ACT);
   rect(x,211,40,1,COLOR_SURFACE_BORDER);rect(x,211,1,80,COLOR_SURFACE_BORDER);rect(x+39,211,1,80,COLOR_SURFACE_BORDER);
   number(note,s);colored_label(x+8,271,s,
    (note==active_note||held[note])?COLOR_MODERN_BG:COLOR_TEXT_PRIMARY);
  }
  previous_held[i]=held[note];
 }
 previous_active=active_note;
 sys_window_present(wh);
 ui_dirty=0;last_draw_tick=played_frames/VISUAL_FRAME_SAMPLES;
}
static void set_program(int p){if(p<0)p=14;if(p>14)p=0;program=p;if(opl_enabled)opl_program(MANUAL_CHANNEL,p*8);set_oscillator_type(MANUAL_CHANNEL,(oscillator_type)p);}
static void set_controls(void){
 instruments_volume(volume);
 ch_config[MANUAL_CHANNEL].vol=volume/127.0f;
 if(opl_enabled)opl_gain(MANUAL_CHANNEL,volume/127.0f);
 set_envelope_attack(MANUAL_CHANNEL,(uint16_t)(attack*15));set_envelope_decay(MANUAL_CHANNEL,(uint16_t)(decay*15));
 set_envelope_sustain(MANUAL_CHANNEL,sustain/127.0f);set_envelope_release(MANUAL_CHANNEL,(uint16_t)(release*15));
}
static void manual_stop(uint8_t note){if(opl_enabled)opl_note_off(MANUAL_CHANNEL,note);else stop_note(MANUAL_CHANNEL,note);}
static void note_on(int note){if(active_note>=0)manual_stop((uint8_t)active_note);active_note=note;if(opl_enabled)opl_note_on(MANUAL_CHANNEL,(uint8_t)note,0.85f);else play_note(MANUAL_CHANNEL,(uint8_t)note,0.85f);}
static void note_off(void){if(active_note>=0){manual_stop((uint8_t)active_note);active_note=-1;}}
static int acquire_audio(void){
 if(audio_acquired)return 1;
 opl_enabled=sys_audio_opl(256,0)==0;
 audio_ready=opl_enabled || sys_audio_init(44100)>=0;
 audio_acquired=audio_ready;
 (void)sys_audio_device_name(audio_device,sizeof(audio_device));
 playback_error=audio_ready?0:"Audio output unavailable";
 pcm_pending=0;ui_dirty=1;
 return audio_ready;
}
static void mouse(int x,int y,int buttons){
 static int previous_buttons;
 int pressed=buttons & ~previous_buttons;previous_buttons=buttons;
 if(!(pressed&1))return;
 if(y>=155&&y<179&&x>=380&&x<580){if(!acquire_audio())return;if(x<475){if(song_playing)song_playing=0;else if(song_index>=song.count)song_start();else song_playing=song_loaded;}else song_start();}
 else if(y>=211&&y<291&&x>=16&&x<16+13*43){int note=60+(x-16)/43;if(note==active_note)note_off();else note_on(note);}
 else if(y>=39&&y<65){if(x>=108&&x<138)set_program(program-1);else if(x>=330&&x<360)set_program(program+1);else if(x>=410&&x<510){note_off();silence();}}
 else if(x>=16&&x<530&&((y>=101&&y<117)||(y>=153&&y<169))){
  int *v=0;int sx=0;
  if(y>=153){v=&release;sx=16;}
  else if(x>=16&&x<116){v=&volume;sx=16;}
  else if(x>=155&&x<255){v=&attack;sx=155;}
  else if(x>=294&&x<394){v=&decay;sx=294;}
  else if(x>=433&&x<533){v=&sustain;sx=433;}
  if(v){int n=(x-sx)*127/100;if(n<0)n=0;if(n>127)n=127;*v=n;set_controls();}
 }
 ui_dirty=1;
}
static void key(int c){
 const char *keys="awsedftgyhujk";
 if(c==1){close_requested=1;return;}
 if(c==' '){note_off();ui_dirty=1;return;}
 for(int i=0;keys[i];i++)if(c==keys[i]){if(active_note==60+i)note_off();else note_on(60+i);ui_dirty=1;break;}
}
static int render_audio(void){
 if(!pcm_pending){
  for(int offset=0;offset<AUDIO_FRAMES;offset+=BUFSIZE_MONO){
   song_step(BUFSIZE_MONO);
   run_lanes();
   for(int j=0;j<BUFSIZE_STEREO;j++){
    float f=stereo_out[j]*32767.0f;
    if(f>32767.0f)f=32767.0f;
    if(f< -32768.0f)f=-32768.0f;
    pcm[offset*2+j]=(int16_t)f;
   }
  }
  pcm_pending=1;pcm_offset=0;pcm_progress_tick=sys_get_ticks();
 }
 uint32_t remaining=sizeof(pcm)-pcm_offset;
 int r=sys_audio_write((const uint8_t*)pcm+pcm_offset,remaining);
 if(r<0 || (uint32_t)r>remaining || (r&3))return -1;
 if(r>0){
  pcm_offset+=(uint32_t)r;pcm_progress_tick=sys_get_ticks();
  if(pcm_offset==sizeof(pcm))pcm_pending=0;
 }else if((uint32_t)(sys_get_ticks()-pcm_progress_tick)>300u)return -1;
 return r;
}
static void playback_failed(void){
 silence();
 if(audio_acquired)sys_audio_shutdown();
 audio_acquired=audio_ready=0;pcm_pending=0;song_playing=0;
 playback_error="Audio stopped: device error or queue timeout";
 ui_dirty=1;
}

void user_main(const char *args){
 song_loaded=0; song_name[0]=0;
 if(args && args[0]) {
  const char *a=args;while(*a==' ')a++;
  char path[128];int i=0;while(a[i] && i<127){path[i]=a[i];i++;}path[i]=0;
  if(i){int r=midi_file_load(path,&song);if(r==0){song_loaded=1;for(int j=0;j<95;j++){song_name[j]=path[j];if(!path[j])break;}song_name[95]=0;}else{static const char err[]="syndig: unable to load MIDI file\n";sys_write(err,sizeof(err)-1);}}
 }
 user_window_desc_t d={0};d.x=120;d.y=90;d.width=W;d.height=H;
 const char *title="Syndig";for(int i=0;title[i];i++)d.title[i]=title[i];
 int32_t h=sys_window_create(&d);if(h<0)return;wh=(uint32_t)h;
 opl_enabled=sys_audio_opl(256,0)==0;
 init_buffer();init_polyphony();init_envelopes();init_oscillators();init_midi();init_lanes();instruments_reset();set_program(4);set_controls();if(song_loaded)song_start();
 audio_ready=opl_enabled || sys_audio_init(44100)>=0;
 audio_acquired=audio_ready;
 (void)sys_audio_device_name(audio_device,sizeof(audio_device));
 if(!audio_ready)playback_error="Audio output unavailable";
 uint32_t opl_tick=sys_get_ticks();
 draw();
 while(!close_requested){
  user_window_event_t e;
  while(sys_window_poll(wh,&e)==1){
   if(e.type==USER_WINDOW_EVENT_CLOSE){close_requested=1;break;}
   if(e.type==USER_WINDOW_EVENT_REDRAW)ui_dirty=1;
   if(e.type==USER_WINDOW_EVENT_MOUSE)mouse(e.x,e.y,e.buttons);
   if(e.type==USER_WINDOW_EVENT_KEY)key(e.key);
  }
  if(close_requested)break;
  if(audio_ready && opl_enabled){
   uint32_t now=sys_get_ticks(),elapsed=now-opl_tick;opl_tick=now;
   if(elapsed)song_step(elapsed*441u);
   sys_yield();
  }
  else if(audio_ready){int r=render_audio();if(r<0){playback_failed();}else if(r==0)sys_yield();}
  else sys_yield();
  if(ui_dirty || (song_loaded && played_frames/VISUAL_FRAME_SAMPLES!=last_draw_tick))draw();
 }
 note_off();silence();if(audio_acquired)sys_audio_shutdown();sys_window_close(wh);
}
