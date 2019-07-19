#include "Arduino.h"
#include "timeLib.h"

//-----------------------------------------------------------------------------
// 現在時刻の文字列表現を取得する
//-----------------------------------------------------------------------------
String getTimeString(struct tm &timeInfo) {
    char s[20];
    sprintf(s, "%04d/%02d/%02d %02d:%02d:%02d",
          timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
          timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

    return String(s);
}

//-----------------------------------------------------------------------------
// 指定時間経過したかチェックする
//-----------------------------------------------------------------------------
//【引数】struct tm &newTime 時刻情報（新）
//        struct tm &oldTime 時刻情報（旧）
//        int   intervalMin  間隔（分）
//【戻り値】 newTime-oldTimeがintervalMinを超えていたらtrue, そうでなければfalse
//-----------------------------------------------------------------------------
bool isTimePassed(struct tm &newTime, struct tm &oldTime, int &intervalMin) {
    int timeLapse = ((newTime.tm_hour * 60) + newTime.tm_min) 
            - ((oldTime.tm_hour * 60) + oldTime.tm_min);

    if ( timeLapse >= intervalMin ) {
        return true;
    } else {
        return false;
    }
    
}
