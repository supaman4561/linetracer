#include "h8-3069-iodef.h"
#include "h8-3069-int.h"

#include "lcd.h"
#include "ad.h"
#include "timer.h"
#include "moter.h"

/* タイマ割り込みの時間間隔[μs] */
#define TIMER0 1000

/* 割り込み処理で各処理を行う頻度を決める定数 */
#define DISPTIME 100
#define ADTIME  5
#define PWMTIME 1
#define CONTROLTIME 10

/* LCD表示関連 */
/* 1段に表示できる文字数 */
#define LCDDISPSIZE 8

/* PWM制御関連 */
/* 制御周期を決める定数 */
#define MAXPWMCOUNT 10

/* モータ制御関連 */
/* モータの最大速度 */
#define MAXSPEED 10
#define SPINSPEED 5
#define TRACE 1
#define FORWARD 2
#define LSPIN 3
#define RSPIN 4

/* A/D変換関連 */
/* A/D変換のチャネル数とバッファサイズ */
#define ADCHNUM   4
#define ADBUFSIZE 8
/* 平均化するときのデータ個数 */
#define ADAVRNUM 3
/* チャネル指定エラー時に返す値 */
#define ADCHNONE -1

/* 割り込み処理に必要な変数は大域変数にとる */
volatile int disp_time, ad_time, pwm_time, control_time;

/* 光検知関係 */
unsigned char leftval, rightval;

/* LCD関係 */
volatile int disp_flag;
volatile char lcd_str_upper[LCDDISPSIZE+1];
volatile char lcd_str_lower[LCDDISPSIZE+1];

/* PWM制御関連 */
int left_speed ,right_speed;

/* モータ制御関係 */
volatile int pwm_count;
int rad_count, rad_control;
int SPINFLAG;
int mode;

/* A/D変換関係 */
volatile unsigned char adbuf[ADCHNUM][ADBUFSIZE];
volatile int adbufdp;

int main(void);
void int_imia0(void);
void int_adi(void);
int  ad_read(int ch);
void pwm_proc(void);
void control_proc(void);


int main(void)
{
  int start_flag = 0;
  
  /* 初期化 */
  ROMEMU();

  /* スタート/ストップ入力ポート(P6) */
  P6DDR &= 0xFC;
  P6DR &= 0xfc;

  /* 割り込みで使用する大域変数の初期化 */
  pwm_time = pwm_count = 0;     /* PWM制御関連 */
  disp_time = 0; disp_flag = 1;
  ad_time = 0;
  control_time = 0;
  left_speed = right_speed = 0;
  rad_count = rad_control = 0;
  SPINFLAG = 0;
  mode = TRACE;
  
  adbufdp = 0;
  lcd_init();
  ad_init();
  timer_init();
  timer_set(0, TIMER0);
  start_flag=0;
  leftval = rightval = 0;

  /* モーター初期化 */
  PBDDR |= 0x0F;

  /* ここでLCDに表示する文字列を初期化しておく */
  strcpy(lcd_str_upper, "        ");
  strcpy(lcd_str_lower, "        ");

  /* モータ */
  while(1){

    if(~P6DR & 0x01){
      start_flag = 1;
      timer_start(0);
      strcpy(lcd_str_upper, "start   ");
      ENINT();
    }
    else if(~P6DR & 0x02){
      start_flag = 0;
      timer_stop(0);
      strcpy(lcd_str_upper, "stop    ");
      lcd_cursor(0,0);
      lcd_printstr(lcd_str_upper);
      brake();
      DISINT();
      control_time = 0;
      left_speed = right_speed = 0;
      rad_count = rad_control = 0;
      SPINFLAG = 0;
      mode = TRACE;
      
      leftval = rightval = 0;

      
    }

    if(start_flag == 1){
      if(disp_flag == 1){
	lcd_str_lower[0] = leftval/100 + '0';
	lcd_str_lower[1] = (leftval/10)%10 + '0';
	lcd_str_lower[2] = leftval%10 + '0';
	lcd_str_lower[4] = rightval/100 + '0';
	lcd_str_lower[5] = (rightval/10)%10 + '0';
	lcd_str_lower[6] = rightval%10 + '0';
	
	lcd_cursor(0,0);
	lcd_printstr(lcd_str_upper);
	lcd_cursor(0,1);
	lcd_printstr(lcd_str_lower);
      }
      
      disp_flag = 0;
    }
    
  }

}

#pragma interrupt
void int_imia0(void)
     /* タイマ0(GRA) の割り込みハンドラ　　　　　　　　　　　　　　　 */
     /* 関数の名前はリンカスクリプトで固定している                   */
     /* 関数の直前に割り込みハンドラ指定の #pragama interrupt が必要 */
     /* タイマ割り込みによって各処理の呼出しが行われる               */
     /*   呼出しの頻度は KEYTIME,ADTIME,PWMTIME,CONTROLTIME で決まる */
     /* 全ての処理が終わるまで割り込みはマスクされる                 */
     /* 各処理は基本的に割り込み周期内で終わらなければならない       */
{
  /* LCD表示の処理 */
  /* 他の処理を書くときの参考 */
  disp_time++;
  if (disp_time >= DISPTIME){
    disp_flag = 1;
    disp_time = 0;
  }
  
  /* ここにPWM処理に分岐するための処理を書く */
  pwm_time++;
  if(pwm_time >= PWMTIME){
    pwm_time = 0;
    pwm_proc();
  }
  
  /* ここにA/D変換開始の処理を直接書く */
  /* A/D変換の初期化・スタート・ストップの処理関数は ad.c にある */
  ad_time++;
  if(ad_time >= ADTIME){
    ad_time = 0;
    ad_scan(0,1);
  }
  
  /* ここに制御処理に分岐するための処理を書く */
  control_time++;
  if(control_time >= CONTROLTIME){
    control_time = 0;
    control_proc();
  }
  
  timer_intflag_reset(0); /* 割り込みフラグをクリア */
  ENINT();                /* CPUを割り込み許可状態に */
}


#pragma interrupt
void int_adi(void)
{
  ad_stop();

  adbufdp++;
  adbufdp %= ADBUFSIZE;

  adbuf[0][adbufdp] = ADDRAH;
  adbuf[1][adbufdp] = ADDRBH;
  adbuf[2][adbufdp] = ADDRCH;
  adbuf[3][adbufdp] = ADDRDH;
  
  ENINT();
}


int ad_read(int ch)
     /* A/Dチャネル番号を引数で与えると, 指定チャネルの平均化した値を返す関数 */
     /* チャネル番号は，0〜ADCHNUM の範囲 　　　　　　　　　　　             */
     /* 戻り値は, 指定チャネルの平均化した値 (チャネル指定エラー時はADCHNONE) */
{
  int i,ad,bp;

  if ((ch > ADCHNUM) || (ch < 0)) ad = ADCHNONE; /* チャネル範囲のチェック */
  else {
    ad = 0;
    bp = adbufdp + ADBUFSIZE;
    
    /* ここで指定チャネルのデータをバッファからADAVRNUM個取り出して平均する */
    /* データを取り出すときに、バッファの境界に注意すること */
    /* 平均した値が戻り値となる */
    for(i=0; i<ADAVRNUM; i++){
      ad += adbuf[ch][(bp-i)%ADBUFSIZE];
    }
    ad /= ADAVRNUM;
    
  }
  return ad; /* データの平均値を返す */
}

void pwm_proc(void)
{
  pwm_count++;

  if(mode == TRACE){

    if(pwm_count <= left_speed){
      PBDR &= 0xfd;
      PBDR |= 0x01;
    }else{
      PBDR &= 0xfc;
    }

    if(pwm_count <= right_speed){
      PBDR &= 0xf7;
      PBDR |= 0x04;
    }else{
      PBDR &= 0xf3;
    }
  }else if(mode == FORWARD){
    if(pwm_count <= SPINSPEED-2){
      forward();
    }else{
      brake();
      //moter_stop();
    }
  }else if(mode == LSPIN){
    if(pwm_count <= SPINSPEED){
      leftSpin();
    }else{
      brake();
      //moter_stop();
    }
  }else if(mode == RSPIN){
    if(pwm_count <= SPINSPEED+1){
      rightSpin();
    }else{
      moter_stop();
    }
  }

  if(pwm_count >= MAXPWMCOUNT) pwm_count = 0;
  
}

void control_proc(void)
{
  leftval = (unsigned char)ad_read(1);
  rightval = (unsigned char)ad_read(2);

  if(leftval > 100 && rightval > 100 && SPINFLAG == 0){
    //moter_stop();
    //usleep(100);
    mode = LSPIN;
    SPINFLAG = 1;
  }else if((leftval < 100 || rightval < 100) && SPINFLAG == 1){
    SPINFLAG = 2;
    moter_stop();
    rad_count = rad_count / 3;
    DISINT();
    wait1ms(1);
    ENINT();
    //usleep(100);
  }else if(SPINFLAG == 1){
    rad_count += 1;
  }else if(SPINFLAG == 2){
    mode = RSPIN;
    rad_count -= 1;
    if(rad_count <= 0){
      //moter_stop();
      //usleep(100);
      DISINT();
      wait1ms(1);
      ENINT();
      
      SPINFLAG = 3;
    }
  }else if(SPINFLAG == 3){
    mode = FORWARD;
    if (leftval < 100 || rightval < 100){
      //moter_stop();
      //usleep(100);
      mode = TRACE;
      SPINFLAG == 4;
    }
      
  }else if(SPINFLAG == 4){
    mode = TRACE;
    if(leftval > rightval){
      left_speed = MAXSPEED;
      right_speed = ((rightval + (leftval-rightval) / 2 ) * MAXSPEED) /leftval;
    }else{
      right_speed = MAXSPEED;
      left_speed = ((leftval + (rightval-leftval) / 2 ) * MAXSPEED) /rightval;
    }
    if(leftval < 100 || rightval < 100) SPINFLAG = 5;
    
  }else if(SPINFLAG == 5){
    mode = LSPIN;
    if(leftval < 100 || rightval < 100) SPINFLAG = 6;
    
  }else if(SPINFLAG == 6){
    mode = TRACE;
    if(leftval > rightval){
      left_speed = MAXSPEED;
      right_speed = ((rightval + (leftval-rightval) / 2 ) * MAXSPEED) /leftval;
    }else{
      right_speed = MAXSPEED;
      left_speed = ((leftval + (rightval-leftval) / 2 ) * MAXSPEED) /rightval;
    }
    if(leftval < 100 || rightval < 100) SPINFLAG = 7;
    
  }else if(SPINFLAG == 7){
    mode = RSPIN;
    if(leftval < 100 || rightval < 100) SPINFLAG = 8;
  }
  
  else{  
    
    mode = TRACE;
    
    if(leftval > rightval){
      left_speed = MAXSPEED;
      right_speed = ((rightval + (leftval-rightval) / 2 ) * MAXSPEED) /leftval;
    }else{
      right_speed = MAXSPEED;
      left_speed = ((leftval + (rightval-leftval) / 2 ) * MAXSPEED) /rightval;
    }
   
  }
}
