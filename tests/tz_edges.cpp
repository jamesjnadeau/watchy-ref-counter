#include "RefZone.h"
#include <cstdio>
#include <cstring>
#include <ctime>
static void pick(const char*n){for(uint8_t i=0;i<RefZone::count();i++)if(!strcmp(RefZone::name(i),n))RefZone::setIndex(i);}
static time_t mk(int y,int mo,int d,int h,int mi){struct tm t={};t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=mi;return timegm(&t);}
int main(){
  RefZone::begin(); pick("Eastern");
  printf("Set Time round trip through the two US transition hours (Eastern, 2026)\n\n");
  struct {const char*label;int mo,d;} w[] = {{"spring forward: 02:00-02:59 does not exist",3,8},
                                             {"fall back: 01:00-01:59 happens twice",11,1}};
  for (auto &k : w) {
    printf("%s\n", k.label);
    for (int h = 0; h <= 3; h++) for (int mi = 0; mi < 60; mi += 30) {
      time_t want = mk(2026,k.mo,k.d,h,mi);
      time_t utc  = want - RefZone::offsetSecondsForLocal(want);
      time_t back = utc + RefZone::offsetSecondsAt(utc);
      struct tm b; gmtime_r(&back,&b);
      printf("  typed %02d:%02d -> reads back %02d:%02d %s%s\n", h, mi,
             b.tm_hour, b.tm_min, RefZone::abbrevAt(utc),
             back==want ? "" : "   <-- shifted");
    }
    printf("\n");
  }
}
