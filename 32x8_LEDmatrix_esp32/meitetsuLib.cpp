#include "Arduino.h"
#include "meitetsuLib.h"

//=============================================================================
// 名鉄運行情報関連ライブラリ
//=============================================================================
//-----------------------------------------------------------------------------
// 名鉄運行情報が更新されたかどうかチェックする
//-----------------------------------------------------------------------------
// HTTP経由で取得する前に、提供時間内で、かつ毎時0分・30分かどうかチェックして
// 条件を満たす場合はtrue、条件を満たさない場合はfalseを返す。
//-----------------------------------------------------------------------------
bool isMeitetsuUpdate(struct tm &timeInfo) {
    // 名鉄運行情報 提供時間外は常にfalse
    if ( timeInfo.tm_hour >= 1 && timeInfo.tm_hour < 5 ) {
        //01:00～04:59は問答無用で提供時間外
        return false;
    } else if ( timeInfo.tm_hour == 0 && timeInfo.tm_min > 30 ) {
        //0:30より後は提供時間外
        return false;
    }
    
    // 毎時0分と30分に更新される
    if ( timeInfo.tm_min == 0 || timeInfo.tm_min == 30 ) {
        return true;
    } else {
        return false;
    }
}

//-----------------------------------------------------------------------------
// 名鉄運行情報サイトを解析する
// 不要な文字列を除きつつ強引に解析している。
// 運行支障が生じた時の戻りがわからないため、異常がないときのみ動作する（笑）
//-----------------------------------------------------------------------------
// 【現時点で動作確認取れたもの】
// 通常運行
// 運行情報提供時間外 0:30～5:00
//-----------------------------------------------------------------------------
String parseMeitetsuInfo(String &meitetsuHP) {
    int findPos = 0;
    int endPos = 0;
    String operationInfo = "";
    String buf = "";

#ifdef DEBUG
    Serial.println("---------------------------");
    Serial.println(meitetsuHP);
    Serial.println("---------------------------");
#endif

    //bodyタグを探す
    findPos = meitetsuHP.indexOf("<body>");
    meitetsuHP = meitetsuHP.substring(findPos, meitetsuHP.length());

    //名鉄運行情報を探す
    findPos = meitetsuHP.indexOf("名鉄電車運行情報");
    meitetsuHP = meitetsuHP.substring(findPos, meitetsuHP.length());
    
    // 名鉄運行情報を表示文字に退避
    findPos = meitetsuHP.indexOf("<br>");
    operationInfo = meitetsuHP.substring(0, findPos);
    meitetsuHP = meitetsuHP.substring(findPos, meitetsuHP.length());

    // 運行情報自体の先頭は&ndspで表示位置を調整されているので、
    // &ndspで探しに行くと運行情報が記載された行が取得できる
#ifdef DEBUG
    Serial.println("nbsp find...");
#endif

    findPos = meitetsuHP.indexOf("&nbsp;");
    meitetsuHP = meitetsuHP.substring(findPos, meitetsuHP.length());
    endPos = meitetsuHP.indexOf("\n");
    buf = meitetsuHP.substring(0, endPos);

#ifdef DEBUG
    Serial.print("buf ===>>>");
    Serial.println(buf);
#endif
    
    //不要文字をひたすら削除

#ifdef DEBUG
    Serial.println("repalce some word...");
#endif

    buf.replace("&nbsp;&nbsp;&nbsp;", "");
    buf.replace("<br></p><p>", " ");
    buf.replace("</p>", "");
    
    operationInfo = operationInfo + ":" + buf;
    buf = "";
    
#ifdef DEBUG
    Serial.println(opreationInfo);
    Serial.println("---------------------------");
#endif

    return operationInfo;
    
}

//-----------------------------------------------------------------------------
// １列車時刻取得スクリプトの実行結果を解析する
//-----------------------------------------------------------------------------
// getTrainTimeスクリプトの戻り値から余分な情報を削除して、必要な情報のみ返す
// 終電に未対応
//-----------------------------------------------------------------------------
String parseTimeTable(String &localCgi) {
    int findPos = 0;
    String buf = "";

    findPos = localCgi.indexOf("<body>");
    buf = localCgi.substring(findPos, localCgi.length());
    findPos = buf.indexOf("</body>");
    buf = buf.substring(0, findPos);
    
    buf.replace("</body>", "");
    buf.replace("<body>", "");

    if ( buf.indexOf("Train") >= 0 ) {
        //終電と思われる
        buf = "本日の運転は終了しました。";
    } else {
        buf = "次の電車は" + buf + "行です。";
    }

    return buf;
}
