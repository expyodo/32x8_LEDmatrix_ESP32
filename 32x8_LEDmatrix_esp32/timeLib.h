#ifndef __TIME_LIB_H__
#define __TIME_LIB_H__

//=============================================================================
// 時刻関連処理関数ライブラリ ヘッダ
//=============================================================================
// 指定した時刻の文字列表現を取得する
String getTimeString(struct tm &timeInfo);
// 指定した時刻間の差分を取り、指定した時間（分）が立っているかどうかチェックする
bool isTimePassed(struct tm &newTime, struct tm &oldTime, int &intervalMin);

#endif /* __TIME_LIB_H__ */
