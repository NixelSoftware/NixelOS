#define IRC_HOST_TEST
#include "../main.c"
#include <assert.h>
#include <stdio.h>

static void command(const char* s) { char buf[384]; copy(buf,sizeof(buf),s); submit(buf); }
int main(void) {
    command("/nick TestNick");
    command("/server irc.example.org 6667");
    network_poll(); network_poll();
    assert(strstr(sent,"NICK TestNick\r\nUSER TestNick 0 * :Nixel IRC\r\n"));
    assert(!strstr(sent,"JOIN"));
    rx = ":server 001 TestNick :Welcome\r\nPING :split";
    network_poll(); assert(phase == 4); assert(receive_len);
    rx = "-token\r\n:alice!u@h PRIVMSG #nixel :hello\r\nPING :two\r\n";
    network_poll(); network_poll(); network_poll();
    assert(strstr(sent,"JOIN #nixel\r\n"));
    assert(strstr(sent,"PONG :split-token\r\n"));
    assert(strstr(sent,"PONG :two\r\n"));
    int found = 0;
    for (unsigned i=0;i<count;i++) if (strstr(lines[(first+i)%HISTORY],"<alice> hello")) found=1;
    assert(found);
    command("/join #test"); network_poll();
    rx = ":TestNick!u@h JOIN :#test\r\n"; network_poll();
    assert(equal(channel,"#test"));
    command("hello world"); network_poll();
    assert(strstr(sent,"PRIVMSG #test :hello world\r\n"));
    command("/msg alice private"); network_poll();
    assert(strstr(sent,"PRIVMSG alice :private\r\n"));
    /* Oversized lines are dropped up to LF; the next line must still parse. */
    char large[700]; memset(large,'x',600); strcpy(large+600,"\nPING :after\r\n");
    rx = large; network_poll(); network_poll();
    assert(strstr(sent,"PONG :after\r\n"));
    /* Raw scan codes keep capital letters separate from navigation keys. */
    key(0x2A); key(0x23); key(0x17); key(0xAA); /* HI */
    assert(equal(input,"HI"));
    key(0xE0); key(0x4B); key(0x2D); assert(equal(input,"HxI"));
    key(0x0E); assert(equal(input,"HI"));
    key(1); assert(!input_len && !cursor);
    for (unsigned i=0;i<300;i++) log_line("history",FG);
    assert(count == HISTORY); key(0x49); assert(scroll); redraw();
    assert(scroll <= count-ROWS);
    ticks = started + 3001; phase = 3; network_poll(); assert(!phase);
    command("/server host 65536"); assert(!phase);
    command("/server host"); assert(resolver && phase == 1);
    command("/disconnect"); assert(!phase && resolver);
    network_poll(); assert(!resolver && !phase);
    assert(!queue("PRIVMSG #test :bad\r\nQUIT"));
    puts("IRC tests passed");
}
