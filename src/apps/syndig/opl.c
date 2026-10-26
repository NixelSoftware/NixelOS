/* Nine two-operator FM voices, usable with both OPL2 and OPL3 hardware. */
#include "opl.h"
#include "../../system/user/sys_api.h"
int opl_enabled;
static const uint8_t operators[9]={0,1,2,8,9,10,16,17,18};
/* Equal-tempered F-numbers for octave 4 (C through B). */
static const uint16_t frequencies[12]={343,363,385,408,432,458,485,514,544,577,611,647};
static struct { uint8_t ch,note,on,velocity; uint32_t age; } voices[9];
static uint8_t programs[17];
static float gains[17];
static uint32_t age;
static void wr(uint8_t reg,uint8_t value){sys_audio_opl(reg,value);}
static void level(int v){
 float gain=gains[voices[v].ch]*voices[v].velocity/127.0f;
 if(gain>1)gain=1;if(gain<0)gain=0;
 /* TL is attenuation in 0.75 dB steps, not a linear amplitude slider.
  * Each step multiplies amplitude by 10^(-0.75/20). Round to the
  * nearest step; the half-step threshold is 10^(-0.375/20). */
 uint8_t attenuation=0;
 float amplitude=1.0f;
 while(attenuation<63 && gain<amplitude*0.95774524f){
  amplitude*=0.91727594f;
  attenuation++;
 }
 wr(0x43+operators[v],attenuation);
}
void opl_gain(uint8_t ch,float gain){
 if(ch>16)return;gains[ch]=gain;
 for(int v=0;v<9;v++)if(voices[v].on&&voices[v].ch==ch)level(v);
}
void opl_program(uint8_t ch,uint8_t program){if(ch<17)programs[ch]=program;}
void opl_note_off(uint8_t ch,uint8_t note){
 for(int v=0;v<9;v++)if(voices[v].on&&voices[v].ch==ch&&voices[v].note==note){
  wr(0xB0+v,0);voices[v].on=0;
 }
}
void opl_stop_channel(uint8_t ch){
 for(int v=0;v<9;v++)if(voices[v].on&&voices[v].ch==ch){wr(0xB0+v,0);voices[v].on=0;}
}
void opl_stop_all(void){for(int ch=0;ch<17;ch++)opl_stop_channel(ch);}
void opl_note_on(uint8_t ch,uint8_t note,float velocity){
 if(ch>16||note>127)return;
 opl_note_off(ch,note);
 int v=0;for(int i=0;i<9;i++){if(!voices[i].on){v=i;break;}if(voices[i].age<voices[v].age)v=i;}
 wr(0xB0+v,0);
 voices[v].ch=ch;voices[v].note=note;voices[v].on=1;
 voices[v].velocity=(uint8_t)(velocity*127);voices[v].age=++age;
 uint8_t op=operators[v],family=programs[ch]/8;
 /* Compact GM-family approximations: percussive envelopes for piano,
  * guitar and drums; sustained envelopes for organ, strings and winds. */
 int percussive=ch==9||family<2||family==3||family==4||family>=12;
 wr(0x20+op,percussive?0x01:0x21);wr(0x23+op,percussive?0x01:0x21);
 wr(0x40+op,16+(family%4)*8);level(v);
 wr(0x60+op,0xF3);wr(0x63+op,percussive?0xF5:0xA2);
 wr(0x80+op,percussive?0x85:0x24);wr(0x83+op,percussive?0x85:0x24);
 wr(0xE0+op,family%4);wr(0xE3+op,0);wr(0xC0+v,0x06);
 int block=(int)note/12-1;uint32_t f=frequencies[note%12];
 while(block<0){f>>=1;block++;}while(block>7){f<<=1;block--;}
 if(f>1023)f=1023;
 wr(0xA0+v,f&255);wr(0xB0+v,0x20|(block<<2)|(f>>8));
}
