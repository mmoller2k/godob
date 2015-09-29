#include <math.h>
#include "godob.h"

DS1302RTC *godob::RTC;
LiquidCrystal *godob::LCD;
Buttons *godob::BTN;
Encoder *godob::ENCAlt;
Encoder *godob::ENCAz;
Astro *godob::AST;

uint8_t godob::pin_tCE;
uint8_t godob::pin_tIO;
uint8_t godob::pin_tCLK;
uint8_t godob::pin_dRS;
uint8_t godob::pin_dE;
uint8_t godob::pin_d4;
uint8_t godob::pin_d5;
uint8_t godob::pin_d6;
uint8_t godob::pin_d7;
uint8_t godob::pin_BL;

uint8_t godob::Backlight=1;

#define ItoRad(r) (r*M_PI/32768.0)
#define RadToI(r) (r*32768.0/M_PI)

godob::godob(uint8_t tCE, uint8_t tIO, uint8_t tCLK,
	 uint8_t dRS, uint8_t dE, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7, uint8_t dBL)
{
  pin_tCE = tCE;
  pin_tIO = tIO;
  pin_tCLK = tCLK;
  pin_dRS = dRS;
  pin_dE = dE;
  pin_d4 = d4;
  pin_d5 = d5;
  pin_d6 = d6;
  pin_d7 = d7;
  pin_BL = dBL;

}

void godob::begin(void)
{
  RTC = new DS1302RTC(pin_tCE, pin_tIO, pin_tCLK);
  LCD = new LiquidCrystal(pin_dRS,pin_dE,pin_d4,pin_d5,pin_d6,pin_d7);
  BTN = new Buttons(0);
  ENCAz = new Encoder(0x42);
  ENCAlt = new Encoder(0x40);
  AST = new Astro(&Serial);
  
  Serial.print("00000000,00000000#"); /* let stellarium know we are here */

  Connected = false;
  stars = 0;
  reqPending=false;

  dstat = ds_time;
  // Setup LCD to 16x2 characters
  LCD->begin(16, 2);

  digitalWrite(pin_BL, HIGH);
  pinMode(pin_BL, OUTPUT);

  // Check clock oscillation  
  LCD->clear();
  if (RTC->haltRTC()){
    LCD->print("Clock stopped!");
    delay ( 1000 );
  }

#if 0  
  // Check write-protection
  LCD->setCursor(0,1);
  if (RTC->writeEN())
    LCD->print("Write allowed.");
  else
    LCD->print("Write protected.");

  delay ( 1000 );
#endif
  
  // Setup Time library  
  LCD->clear();
  setSyncProvider(RTC->get); // the function to get the time from the RTC
  //setSyncInterval(300);
  if(timeStatus() != timeSet){
    LCD->print("RTC Sync FAIL!");
    delay ( 1000 );
  }

  LCD->print("Godob 1.0");
  delay(1000);
  LCD->clear();
  ENCAlt->reset();
  ENCAz->reset();
  ENCAz->reverse(1);
}

void godob::print(char *str)
{
  LCD->print(str);
}

void godob::println(char *str)
{
  LCD->print(str);
}

void godob::run(void)
{
  static unsigned long tmin = 0;
  unsigned long t0 = millis();
  float lst;
  starpos_s mount;
  float errf;
  
  if(t0-tmin>100){ /* update LCD every 100ms */
    tmin = t0;
    if(connect_time>5)Connected=false;
    connect_time++;

    switch(dstat){
    case ds_time:
      printtime();
      printdate();
      show_adjust(t_idx);    
      if(BTN->poll()){
	handle_time_keys(BTN->lastkey());
      }
      break;
    case ds_lst:
      LCD->setCursor(0, 0);
      LCD->print("LST ");
      lst = AST->get_LST(now());
      show_hms(lst);
      LCD->setCursor(0, 1);
      LCD->print(" LT ");
      show_hms( (AST->get_LT(lst)%86400)*M_PI/43200 );
      if(BTN->poll()){
	handle_main_keys(BTN->lastkey());
      }
      break;
    case ds_loc:
      LCD->setCursor(0, 0);
      LCD->print("Lat  ");
      show_dms(AST->last_latitude());
      LCD->setCursor(0, 1);
      LCD->print("Long ");
      show_dms(AST->last_longitude());
      if(BTN->poll()){
	handle_main_keys(BTN->lastkey());
      }
      break;
    case ds_horz:
      show_altaz();
      if(BTN->poll()){
	handle_main_keys(BTN->lastkey());
      }
      break;
    case ds_eq:
      LCD->setCursor(0, 0);
      mount.az = ItoRad(ENCAz->lastpos()*4);
      mount.alt = ItoRad(ENCAlt->lastpos()*4);
      mount.LST = AST->get_LST(now());
      AST->horz_to_eq(mount);
      LCD->print(" RA  ");
      show_hms(mount.RA);
      LCD->setCursor(0, 1);
      LCD->print("Dec");
      show_dms(mount.dec);
      if(BTN->poll()){
	handle_main_keys(BTN->lastkey());
      }
      break;
    case ds_targ:
      LCD->setCursor(0, 0);
      if(!reqPending){
	LCD->print("Designate a star");
      }
      else{
	star[0].LST=AST->get_LST(now());
	AST->eq_to_horz(star[0]);
	LCD->print("AltErr:");
	errf = ItoRad(ENCAlt->lastpos()*4) - star[0].alt;
	if(errf>M_PI)errf-=2*M_PI;
	if(errf<-M_PI)errf+=2*M_PI;
	show_rad(errf);
	LCD->setCursor(0, 1);
	LCD->print(" AzErr:");
	errf = ItoRad(ENCAz->lastpos()*4) - star[0].az;
	if(errf>M_PI)errf-=2*M_PI;
	if(errf<-M_PI)errf+=2*M_PI;
	show_rad(errf);
      }
      if(BTN->poll()){
	handle_main_keys(BTN->lastkey());
      }
      break;
    default:
      dstat = ds_time;
      break;
    }/* switch */

    ENCAlt->readpos();
    ENCAz->readpos();

  }
  handle_serial();
}

void godob::show_hms(double rad)
{
  char str[16];
  unsigned h,m,s;

  while(rad<0)rad+=M_PI*2;
  h = rad*12/M_PI;
  m = rad*720/M_PI - h*60;
  s = rad*43200/M_PI - h*3600 - m*60 + 0.5;
  sprintf(str,"%2uh%02um%02ds",h,m,s);
  LCD->print(str);
}

void godob::show_dms(float rad)
{
  char str[16];
  int d,sign=1;
  unsigned m,s;

  if(rad<0){
    sign=-1;
    rad = -rad;
  }
  d = rad*180/M_PI;
  m = rad*10800/M_PI - d*60;
  s = rad*648000/M_PI - d*3600 - m*60 + 0.5;
  sprintf(str,"%4d%c%02u'%02d\"",d*sign,0xdf,m,s);
  LCD->print(str);
}

/* convert radians angle to floating point degrees and display */
void godob::show_rad(float rad)
{
  float deg;
  int ip, fp, sign=1;
  if(rad<0){
    sign = -1;
    rad = -rad;
  }
  deg = rad * 180 / M_PI;
  ip = deg; /* integer part */
  fp = (deg-ip) * 100; /* fractional part: 2 decimals */
  printndigits(' ',4,ip*sign);
  LCD->write('.');
  print2digits(fp);
  LCD->write(0xdf); /* degree symbol */
}

void godob::show_altaz(void)
{
  int err = ENCAlt->lasterr();
  float alt;
  LCD->setCursor(0, 0);
  if(err==7){
    LCD->print("Check Magnet");
  }
  else if(err){
    LCD->print("Error ");
    LCD->print(err);
  }else{
    LCD->print("Alt: ");
    alt = ItoRad(ENCAlt->lastpos()*4);
    if(alt>M_PI)alt-=2*M_PI;
    show_rad(alt);
  }

  LCD->setCursor(0, 1);
  err = ENCAz->lasterr();
  if(err==7){
    LCD->print("Check Magnet");
  }
  else if(err){
    LCD->print("Error ");
    LCD->print(err);
  }else{
    LCD->print(" Az: ");
    show_rad(ItoRad(ENCAz->lastpos()*4));
  }
}

void godob::printtime(void)
{
  tmElements_t tm;
  breakTime(now(), tm);
  LCD->setCursor(1, 0);
  print2digits(tm.Hour);
  LCD->print(":");
  print2digits(tm.Minute);
  LCD->print(":");
  print2digits(tm.Second);
  LCD->print(" GMT+2");
}

void godob::printdate(void)
{
  char ch;
  // Warning!
  if(timeStatus() != timeSet) {
    LCD->setCursor(0, 1);
    LCD->print(F("RTC ERROR: SYNC!"));
  }
  else{
    tmElements_t tm;
    breakTime(now(), tm);
    // Display abbreviated Day-of-Week in the lower left corner
    LCD->setCursor(0, 1);
    if(!Connected){
      ch = '.';
    }
    else if(reqPending){
      ch = '+';
    }
    else{
      ch = '*';
    }
    LCD->write(ch);
    LCD->write(' ');
    LCD->print(dayShortStr(tm.Wday));
    LCD->print(" ");
    print2digits(tm.Day);
    LCD->print("/");
    print2digits(tm.Month);
    LCD->print("/");
    LCD->print(tm.Year+1970);
  }  
}

inline void godob::print2digits(int k)
{
  return printndigits('0', 2, k);
}

void godob::print_quad(unsigned u)
{
  if(u<0x10)Serial.print("000");
  else if(u<0x100)Serial.print("00");
  else if(u<0x1000)Serial.print("0");
  Serial.print(u,HEX);
}

/* add leading pad characters */
void godob::printndigits(char pad, char len, int number)
{
  int n=0,i,sign=1;
  if(number<0){
    sign=-1;
    number = -number;
  }
  for(i=number;i>0;i/=10)n++;
  n=n?n:1; /* zero has length 1 */
  if(sign<0){
    n++;
  }
  for(i=0;i<len-n;i++){
    LCD->write(pad);
  }
  if(sign<0){
    LCD->write('-');
  }    
  LCD->print(number);
}

void godob::handle_main_keys(btnval_e lcd_key)
{
  switch (lcd_key)
    {
    case btnRIGHT:
      break;
    case btnLEFT:
      break;
    case btnUP:
      dstat = (dstat_e)((dstat+ds_last-1)%ds_last);
      LCD->clear();
      break;
    case btnDOWN:
      dstat = (dstat_e)((dstat+1)%ds_last);
      LCD->clear();
      break;
    case btnSELECT:
      if(reqPending == false){
	Backlight=!Backlight;
	digitalWrite(10, Backlight);
	//AST->dump();
      }
      else{
	star[1] = star[0];
	star[0].az = ItoRad(ENCAz->lastpos()*4);
	star[0].alt = ItoRad(ENCAlt->lastpos()*4);
	star[0].LST = AST->get_LST(now());
	if(stars==0){
	  stars = 1;
	  AST->latitude1(star[0]);
	  setTime(AST->get_LT(star[0].LST));
	  AST->eq_to_horz(star[0]);
	  ENCAz->set(star[0].az/M_PI*8192);
	  reqPending = false;
	  LCD->clear();
	  LCD->print("First star set");
	  delay(1000);
	}
	else if(stars>0){
	  stars = 2;
	  star[1].LST=star[0].LST;
	  AST->eq_to_horz(star[1]);
	  AST->latitude2(star[0],star[1]);
	  setTime(AST->get_LT(star[0].LST));
	  AST->eq_to_horz(star[0]);
	  ENCAz->set(star[0].az/M_PI*8192);
	  ENCAlt->set(star[0].alt/M_PI*8192);
	  reqPending = false;
	  LCD->clear();
	  LCD->print("New star set");
	  delay(1000);
	}
      }
      break;
    default:
      break;
    }
}

void godob::handle_time_keys(btnval_e lcd_key)
{
  switch (lcd_key)
    {
    case btnRIGHT:
      if(t_set==true){
	t_idx++;
	t_idx %= 7;
      }
      t_set = true;
      t_adjust=show_adjust(t_idx);
      break;
    case btnLEFT:
      if(t_set==true){
	t_idx+=6;
	t_idx %= 7;
      }
      t_set = true;
      t_adjust=show_adjust(t_idx);
      break;
    case btnUP:
      if(t_set == true){
	//adjustTime(t_adjust); /* doesn't work when t_adjust>0. weird! */
	setTime(now() + t_adjust);
      }
      else{
	handle_main_keys(lcd_key);
      }
      break;
    case btnDOWN:
      if(t_set == true){
	adjustTime(-t_adjust);
      }
      else{
	handle_main_keys(lcd_key);
      }
      break;
    case btnSELECT:
      if(t_set==false){
	handle_main_keys(lcd_key);
      }
      else{
	setRTC();
	LCD->noCursor();
      }
      t_set=false;
      break;
    case btnNONE:
      break;
    }
}

/* set RTC to the current system time */
void godob::setRTC(void)
{
  LCD->setCursor(0, 1);
  LCD->print("                ");
  LCD->setCursor(6, 1);
  if(RTC->set(now())==0){
    LCD->print("Okay");
    delay(1000);  
  }
}

/* show the cursor a the specified index.
   return the adjustment step size for the index
 */
long godob::show_adjust(uint8_t xidx)
{
  const uint8_t cpos[]={2,5,8,15,27,30,35};
  const long adj[]={3600,60,5,0,86400,86400*31,86400*365};
  if(t_set == false){
    LCD->noCursor();
    return 0;
  }
  else{
    LCD->blink();
    LCD->cursor();
    if(cpos[xidx]<20){
      LCD->setCursor(cpos[xidx], 0);
    }
    else{
      LCD->setCursor(cpos[xidx]-20, 1);
    }
    return adj[xidx];
  }
}


int godob::parse_hex(char c)
{
  int r=c-'0';
  if(r>9)r=c-'A'+10;
  return r;
}

unsigned godob::read_quad(void)
{
  unsigned long t0 = millis();
  unsigned r=0;
  while(Serial.available()<4 && millis()-t0<100);
  if(Serial.available()>=4){
    r  = parse_hex(Serial.read())*0x1000;
    r += parse_hex(Serial.read())*0x100;
    r += parse_hex(Serial.read())*0x10;
    r += parse_hex(Serial.read())*0x1;
  }
  return r;
}

void godob::handle_serial(void)
{
  unsigned long t0;
  int ch;
  starpos_s mount;
  unsigned RAq;
  int Decq;
  
    if (Serial.available() > 0) {
        // read the incoming byte:
        ch = Serial.read();
        switch(ch){
	case 'e':
	  Connected = true;
	  connect_time = 0;
	  mount.az = ItoRad(ENCAz->lastpos()*4);
	  mount.alt = ItoRad(ENCAlt->lastpos()*4);
	  mount.LST = AST->get_LST(now());
	  AST->horz_to_eq(mount);
	  print_quad(RadToI(mount.RA));
	  Serial.print("0000,");
	  print_quad(RadToI(mount.dec));
	  Serial.print("0000#");
	  break;
	case 'r':
	  RAq = read_quad();
	  read_quad(); /* ignore the 16 LSBs */
	  while(Serial.read()<1); /* drop comma */
	  Decq = read_quad();
	  read_quad();
	  Serial.print("#");
	  LCD->clear();
	  LCD->print(" RA:");
	  //LCD->print(RAq);
	  show_hms(ItoRad(RAq));
	  LCD->setCursor(0, 1);
	  LCD->print("Dec:");
	  //LCD->print(Decq);
	  show_dms(ItoRad(Decq));
	  delay(3000);
	  if(stars>0){
	    if(!reqPending){
	      star[1] = star[0];
	    }
	    stars=2;
	  }
	  star[0].RA = ItoRad(RAq);
	  star[0].dec = ItoRad(Decq);
	  reqPending = true;
	  dstat = ds_targ;
	  break;
	default: /* display anything else on the LCD */
	  LCD->clear();
	  LCD->write(ch);
	  t0 = millis();
	  do{
	    if(Serial.available()){
	      ch = Serial.read();
	      LCD->write(ch);
	      t0 = millis();
	    }
	  }while(millis()-t0<100);
	  Serial.print("#");
	  delay(2000);
	  break;
        }
    }
}