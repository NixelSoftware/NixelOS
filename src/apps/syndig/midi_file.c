/* Standard MIDI file parser for Syndig: formats 0/1, PPQN timing. */
#include "midi_file.h"
#include "../../system/user/sys_api.h"
/* Parsing finishes before sorting starts, so these buffers can share memory.
 * Stable merging only needs a copy of the left half of the event range. */
static union {
 uint8_t data[MIDI_FILE_CAPACITY];
 midi_event_t scratch[MIDI_MAX_EVENTS/2];
} workspace;
#define data workspace.data
#define scratch workspace.scratch
static uint32_t be16(const uint8_t *p){return ((uint32_t)p[0]<<8)|p[1];}
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static int vlq(uint32_t *pos,uint32_t end,uint32_t *v){*v=0;for(int i=0;i<4;i++){if(*pos>=end)return -1;uint8_t b=data[(*pos)++];*v=(*v<<7)|(b&127);if(!(b&128))return 0;}return -1;}
static void merge(midi_event_t *a,uint32_t l,uint32_t m,uint32_t r){
 uint32_t left=m-l;
 for(uint32_t i=0;i<left;i++)scratch[i]=a[l+i];
 uint32_t i=0,j=m,k=l;
 while(i<left&&j<r)a[k++]=(scratch[i].tick<=a[j].tick)?scratch[i++]:a[j++];
 while(i<left)a[k++]=scratch[i++];
}
static void sort(midi_event_t *a,uint32_t l,uint32_t r){if(r-l<2)return;uint32_t m=l+(r-l)/2;sort(a,l,m);sort(a,m,r);merge(a,l,m,r);}
int midi_file_load(const char *path,midi_song_t *song){
 song->count=0;song->duration_frames=0;
 int32_t sz=sys_fs_read(path,0,0);if(sz<14||sz>MIDI_FILE_CAPACITY)return -1;
 for(uint32_t off=0;off<(uint32_t)sz;){uint32_t n=(uint32_t)sz-off;if(n>16384)n=16384;int32_t got=sys_fs_read_at(path,data+off,n,off);if(got!=(int32_t)n)return -2;off+=n;}
 uint32_t size=(uint32_t)sz;
 if(be32(data)!=0x4d546864||be32(data+4)<6)return -3;
 uint32_t fmt=be16(data+8),tracks=be16(data+10),ppqn=be16(data+12);
 if(fmt>1||tracks==0||ppqn==0||(ppqn&0x8000))return -4;
 uint32_t pos=8+be32(data+4);if(pos>size)return -5;
 for(uint32_t tr=0;tr<tracks;tr++){
  if(pos+8>size||be32(data+pos)!=0x4d54726b)return -6;
  uint32_t len=be32(data+pos+4);pos+=8;if(len>size-pos)return -7;
  uint32_t end=pos+len,tick=0;uint8_t running=0;
  while(pos<end){
   uint32_t delta;if(vlq(&pos,end,&delta))return -8;if(delta>0xffffffffu-tick)return -9;tick+=delta;
   if(pos>=end)return -10;uint8_t status=data[pos];
   if(status&0x80){pos++;if(status<0xf0)running=status;else running=0;}
   else {if(!running)return -11;status=running;}
   if(status==0xff){if(pos>=end)return -12;uint8_t kind=data[pos++];uint32_t n;if(vlq(&pos,end,&n)||n>end-pos)return -13;
    if(kind==0x51&&n==3){if(song->count>=MIDI_MAX_EVENTS)return -14;midi_event_t *e=&song->events[song->count++];e->tick=tick;e->type=0xff;e->channel=data[pos];e->note=data[pos+1];e->velocity=data[pos+2];}
    pos+=n;if(kind==0x2f){pos=end;break;}continue;
   }
   if(status==0xf0||status==0xf7){uint32_t n;if(vlq(&pos,end,&n)||n>end-pos)return -15;pos+=n;continue;}
   if(status>=0xf0)return -16;
   uint8_t type=status&0xf0,ch=status&15;uint32_t n=(type==0xc0||type==0xd0)?1:2;
   if(n>end-pos)return -17;uint8_t a=data[pos++],b=n==2?data[pos++]:0;
   if(type==0xb0 && (a==91||a==93) && b==255)b=127;
   if(a>127||b>127)return -18;
   if(type==0x90||type==0x80||type==0xb0||type==0xc0){if(song->count>=MIDI_MAX_EVENTS)return -14;midi_event_t *e=&song->events[song->count++];e->tick=tick;e->type=(type==0x90&&b==0)?0x80:type;e->channel=ch;e->note=a;e->velocity=b;}
  }
  pos=end;
 }
 sort(song->events,0,song->count);
 uint32_t last_tick=0,tempo=500000;uint64_t us=0,rem=0;
 for(uint32_t i=0;i<song->count;i++){
  midi_event_t *e=&song->events[i];uint32_t tick=e->tick,dt=tick-last_tick;
  uint64_t n=(uint64_t)dt*tempo+rem;us+=n/ppqn;rem=n%ppqn;
  e->frame=us*44100u/1000000u;last_tick=tick;
  if(e->type==0xff){tempo=((uint32_t)e->channel<<16)|((uint32_t)e->note<<8)|e->velocity;if(!tempo)tempo=500000;}
  if(e->frame>song->duration_frames)song->duration_frames=e->frame;
 }
 return 0;
}
