#ifndef NH_NIXEL_NETWORK_H
#define NH_NIXEL_NETWORK_H
#include <stddef.h>
int nh_nixel_net_connect(const char *,const char *,int,char *,size_t);
long nh_nixel_net_send(int,const void *,size_t);
long nh_nixel_net_receive(int,void *,size_t);
void nh_nixel_net_close(int);
#endif
