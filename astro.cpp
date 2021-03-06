#include <Time.h>
#include <math.h>
#include <HardwareSerial.h>
#include "astro.h"

static float Reg[8];

#define DegToRad(d) (d*M_PI/180.0)

time_t Astro::j2k_t;

HardwareSerial *Astro::Serial = NULL;

Astro::Astro(void)
{
  TimeElements j2k_tm={0, 0, 12, 1, 1, 1, 30}; /* 1 Jan 2000 */
  j2k_t = makeTime(j2k_tm);
  /* I'm still cheating here. Should save these in NVM */
  LAT = -25.75 * M_PI / 180.0;
  LON = 28.19 * M_PI / 180.0;
  TZone = 2;
}

/* calculate Greenwich mean sidereal time */
float Astro::get_GMST(time_t ts) /* the UNO doesn't double */
{
  float gmst;
  time_t d = ts - j2k_t - (TZone * 3600);

  d %= 861641; /* retain more significant bits */
  gmst = 0.77905727325 + d/86400.0 + d/31556925.0;
  gmst -= (int)gmst;

  return gmst*2*M_PI; /* everything gets converted to radians */
}

/* Calculate local time from GMST */
time_t Astro::get_LT(float lst)
{
  float lst0;
  int ip,n;
  time_t ts, t0 = now();
  time_t d, d0 = t0 - j2k_t;
  time_t dx;
  float gmst = lst/(2*M_PI);

  n = d0 / 861641;
  d0 %= 861641; /* retain more significant bits */
  lst0 = 0.77905727325 + d0/86400.0 + d0/31556925.0;
  ip = (int)lst0; /* missing integer part */
  gmst += ip;
  d = ( gmst - 0.77905727325 ) * 86164.090531; /* inverse GMST function */
  ts = d + j2k_t + n*861641 + (TZone * 3600);

  return ts;
}

void Astro::eq_to_horz(starpos_s &sp)
{
  float h,d,A,a;
  h = sp.GMST - sp.RA + LON;
  d = sp.dec;
  A = atan2(-sin(h),-cos(h)*sin(LAT) + tan(d)*cos(LAT)); /* E from N */
  a = asin(sin(LAT)*sin(d) + cos(LAT)*cos(d)*cos(h));
  
  if(A<0)A+=2*M_PI; /* Az is positive */
  
  sp.az = A;
  sp.alt = a;
}

void Astro::horz_to_eq(starpos_s &sp)
{
  float A,a,h,d,ra;
  A = sp.az;
  a = sp.alt;
  h = atan2(-sin(A),-cos(A)*sin(LAT) + tan(a)*cos(LAT));
  d = asin(sin(LAT)*sin(a) + cos(LAT)*cos(a)*cos(A));
  ra = sp.GMST - h + LON;

  if(ra<0)ra+=2*M_PI; /* h is positive */
  if(ra>=2*M_PI)ra-=2*M_PI; /* limit to one rev */

  sp.RA = ra;
  sp.dec = d;
}

float Astro::horz_range(starpos_s &s1, starpos_s &s2)
{
  float a1 = s1.alt;
  float a2 = s2.alt;
  float A1 = s1.az;
  float A2 = s2.az;
  float cr = sin(a1)*sin(a2) + cos(a1)*cos(a2)*cos(A2-A1);
  return acos(cr);
}

float Astro::eq_range(starpos_s &s1, starpos_s &s2)
{
  float d1 = s1.dec;
  float d2 = s2.dec;
  float ra1 = s1.RA;
  float ra2 = s2.RA;
  float cr = sin(d1)*sin(d2) + cos(d1)*cos(d2)*cos(ra2-ra1);
  return acos(cr);
}

float Astro::last_latitude(void)
{
  return LAT;
}
  
float Astro::last_longitude(void)
{
  return LON;
}
  

/* One-star latitude calculation */
float Astro::latitude1(starpos_s &s1)
{
  float h,r,b,ha,hb,h0;
  float e1, e2, lst;
  float latx;

  float a = s1.alt;
  float d = s1.dec;
  float A = s1.az;
  float ra = s1.RA;
  float sa = sin(a);
  float ca = cos(a);
  float sd = sin(d);
  float cd = cos(d);
  float sA = sin(A);
  float cA = cos(A);


  /* h = gst - ra + lon */
  h0 = s1.GMST - ra + LON; /* calculate h0 using the initial LST (derive from RTC) */
  /* LON might change but the exact value of h0 is not critical */
  ha = -asin(ca*sA/cd); 
  hb = M_PI - ha; /* h-angle is ambiguous - check both answers */
  e1 = fabs(h0-ha);
  while(e1>M_PI)e1-=2*M_PI;
  e1=fabs(e1);
  e2 = fabs(h0-hb);
  while(e2>M_PI)e2-=2*M_PI;
  e2 = fabs(e2);

  r = sqrt( (sa*sa) + ((ca*cA)*(ca*cA)) );
  b = atan2(ca*cA,sa);

  if(e1 < e2){ /* pick the one closest to the LST */
    latx = asin(sd/r) - b;
  }
  else{
    latx = -asin(sd/r) - b - M_PI;
  }
  
  h = atan2(-sA,-cA*sin(latx)+tan(a)*cos(latx));
  lst = ra + h; /* update LST to the calculated time */
  LON = lst - s1.GMST;
  while(LON<0)LON+=2*M_PI;
  while(LON>=2*M_PI)LON-=2*M_PI;

  LAT = latx;
  Reg[0] = ra;
  Reg[1] = d;
  Reg[2] = A;
  Reg[3] = latx;
  Reg[4] = h0;
  Reg[5] = ha;
  Reg[6] = hb;
  Reg[7] = h;
  return latx;
}

/* Two-star latitude calculation */
float Astro::latitude2(starpos_s &s1, starpos_s &s2)
{
  float cr,sr2;
  float sq1,cq1,sq2,cq2,cq,sq;
  float slat;
  float h1,sh,ch;
  float lst;
  float latx;

  float d1 = s1.dec;
  float d2 = s2.dec;
  float ra1 = s1.RA;
  float ra2 = s2.RA;
  float a1 = s1.alt;
  float a2 = s2.alt;
  float A1 = s1.az;
  float A2 = s2.az;

  cr = sin(d1)*sin(d2) + cos(d1)*cos(d2)*cos(ra2-ra1);
  sr2 = 1-cr*cr; /* sin^2(r) */
  /* prevent divide by zero when
     two stars are too close (0deg) or too far (180deg) appart
     or too close to the pole or zenith */
  if(sr2<0.0004 || fabs(a1)>1.56 || fabs(d1)>1.56)return LAT;

  /* Inside triangle */
  sq1 = cos(a2)*sin(A2-A1); /* SIN rule times sin(r) */
  sq2 = cos(d2)*sin(ra2-ra1);
  cq1 = (sin(a2)-sin(a1)*cr)/cos(a1); /* COS rule times sin(r) */
  cq2 = (sin(d2)-sin(d1)*cr)/cos(d1); /* d1 NOT on the pole! */

  /* cos(A) = cos(2pi-A) */
  cq = (cq1*cq2 - sq1*sq2)/sr2; /* cos(A+B) = cosA.cosB - sinA.sinB */
  sq = -(sq1*cq2 + cq1*sq2)/sr2; /* sin(A+B) = sinA.cosB + cosA.sinB */

  slat = sin(d1)*sin(a1) + cos(d1)*cos(a1)*cq;

  latx = asin(slat);

  sh = -cos(a1)*sq; /* sine rule times cos(lat) */
  ch = (sin(a1)-sin(d1)*slat)/cos(d1); /* cos rule times cos(lat) */
  h1 = atan2(sh,ch); /* cos(lat)s cancel anyway */
  
  //h1 = atan2(-sin(A1),-cos(A1)*sin(latx)+tan(a1)*cos(latx));
  lst = ra1 + h1; /* update LST to the calculated time */
  LON = lst - s1.GMST;
  while(LON<0)LON+=2*M_PI;
  while(LON>=2*M_PI)LON-=2*M_PI;

  LAT = latx;
  return latx;
}


