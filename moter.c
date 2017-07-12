#include "h8-3069-iodef.h"
#include "h8-3069-int.h"

#include "moter.h"

void forward(){
  
  PBDR &= 0xf5;
  PBDR |= 0x05;

}

/* 右のタイヤを前に進めると左に曲がる */
void leftCurve(){

  PBDR &= 0xf4;
  PBDR |= 0x04;

}

/* 左のタイヤを前に進めると右に曲がる */
void rightCurve(){

  PBDR &= 0xf1;
  PBDR |= 0x01;
  
}

void moter_brake(){

  PBDR |= 0x0f;

}

void moter_stop(){

  PBDR &= 0xf0;
  
}

void leftSpin(){
  
  PBDR &= 0xf6;
  PBDR |= 0x06;

}

void rightSpin(){

  PBDR &= 0xf9;
  PBDR |= 0x09;

}

