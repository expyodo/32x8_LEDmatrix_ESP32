#ifndef __MEITETSU_LIB_H__
#define __MEITETSU_LIB_H__

//=============================================================================
// 名鉄運行情報関連ライブラリ ヘッダ
//=============================================================================
// プロトタイプ宣言
// 名鉄運行情報更新チェック
bool isMeitetsuUpdate(struct tm &timeInfo);
// 名鉄運行情報解析（簡易版）
String parseMeitetsuInfo(String &meitetsuHP);
// 時刻表一行表示用
String parseTimeTable(String &localCgi);

#endif /* __MEITESTU_LIB_H__ */
