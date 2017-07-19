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
#define RIGHTSPEED 8
#define LEFTSPEED 7
/* 左右のスピンスピード */
#define RIGHTSPINSPEED 6
#define LEFTSPINSPEED 6
volatile int MAXSPEED = 8;


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

/* モード宣言 */
typedef enum {LEFTSPIN,RIGHTSPIN,RUN}Status; 
volatile Status mode = RUN;
volatile Status spin_mode = LEFTSPIN; //RIGHTSPIN or LEFTSPIN

/* LCD関係 */
volatile int disp_flag;
volatile char lcd_str_upper[LCDDISPSIZE+1];
volatile char lcd_str_lower[LCDDISPSIZE+1];

/* PWM制御関連 */
int left_speed ,right_speed;

/* モータ制御関係 */
volatile int pwm_count;

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
    /* 停止スイッチを押した場合 */
    else if(~P6DR & 0x02){
      start_flag = 0;
      /* チャタリング除去(念のため) */
      timer_stop(0);
      /* チャタリング */
      wait1ms(500);

      /* spin_modeを変更 */
      spin_mode = (spin_mode+1) %2;

      /* 表示 */
      if(spin_mode == LEFTSPIN) {
        strcpy(lcd_str_upper, "L_Spin");
        MAXSPEED = 6;
      }
      else if(spin_mode == RIGHTSPIN) {
        strcpy(lcd_str_upper, "R_Spin");
        MAXSPEED = 8;
      }
      else if(spin_mode == RUN) strcpy(lcd_str_upper, "RUN     ");
      lcd_cursor(0,0);
      lcd_printstr(lcd_str_upper);
      brake();
      DISINT();
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
{
  /* LCD表示の処理 */
  disp_time++;
  if (disp_time >= DISPTIME){
    disp_flag = 1;
    disp_time = 0;
  }
  
  /* PWM処理 */
  pwm_time++;
  if(pwm_time >= PWMTIME){
    pwm_time = 0;
    pwm_proc();
  }
  
  /* A/D変換 */
  ad_time++;
  if(ad_time >= ADTIME){
    ad_time = 0;
    ad_scan(0,1);
  }
  
  /* 制御処理 */
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
{
  int i,ad,bp;

  if ((ch > ADCHNUM) || (ch < 0)) ad = ADCHNONE; /* チャネル範囲のチェック */
  else {
    ad = 0;
    bp = adbufdp + ADBUFSIZE;
    
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
  
  /* スピンモードなら */
  if(mode == spin_mode){
    /* Spin */
      if(mode == LEFTSPIN) {
        if(pwm_count <= LEFTSPINSPEED-1) leftSpin();
        else brake();
      }
      else if(mode == RIGHTSPIN){ 
        if(pwm_count <= RIGHTSPINSPEED) rightSpin();
        else brake();
      }
  }

  
  /* 直進モードなら */
  else{ 
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
  }

  if(pwm_count >= MAXPWMCOUNT) pwm_count = 0;
  
}

void control_proc(void)
{
  leftval = (unsigned char)ad_read(1);
  rightval = (unsigned char)ad_read(2);

  /* 両センサーが黒になったら */
  if(leftval >= 150 && rightval >= 150){
    /* スピンモードに変更 */
    mode = spin_mode;
    brake();
    wait1ms(1);
  }
  /* 片方でも白 */
  else{
    mode = RUN;
	
    if(leftval > rightval){
      left_speed = MAXSPEED;
      right_speed = ((rightval + (leftval-rightval) / 2 ) * MAXSPEED) /leftval;
    }else{
      right_speed = MAXSPEED;
      left_speed = ((leftval + (rightval-leftval) / 2 ) * MAXSPEED) /rightval;
    }
  }
}

