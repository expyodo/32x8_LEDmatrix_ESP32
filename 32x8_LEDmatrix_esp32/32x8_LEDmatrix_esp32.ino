#include "WiFi.h"
#include "HTTPClient.h"
#include "LedMatrixDriver.h"
#include "timeLib.h"
#include "meitetsuLib.h"

//=============================================================================
// LEDマトリクスによる電光掲示板もどき 8*32ドット
//-----------------------------------------------------------------------------
// 2019/07/12 LEDマトリクス関連処理を別ファイルに分離
//-----------------------------------------------------------------------------
// 電光掲示板もどきに現在時刻と名鉄の運行情報を表示する
//-----------------------------------------------------------------------------
//【予約ピン】
// SDカードをSPI接続で使用するため、以下のピンをSPIライブラリで使用する。
// 4:CS
// 11:MOSI
// 12:MISO
// 13:CLK
// LEDマトリクス制御部への接続ピン
// 19    :SER  (Data)
// 18   :SRCLK(Shift)
// 5    :RCLK (Latch)
// 17   :SRCLR(Clear)
// 10   :OE  (Enable)※論理反転なので注意 (基板の作り直しが必要なため未対応)
//-----------------------------------------------------------------------------
// 更新履歴
// 2019/06/29 スクロール処理を見直して作り直した。
// 2019/07/12 LEDマトリクス関連処理をクラス化して分離
//            併せて名鉄運行情報関連処理とNTP関連処理を単純にファイル分離
//-----------------------------------------------------------------------------
// 制限事項 2019/06/29 更新
// ESP32用美咲フォントライブラリ@mgo-tecに対応
// 表示のちらつきが気になる(Arduino Unoでは発生しない)
//     →5v接続で発生。3.3v接続は発生しない
// digitalWriteの引数がuint_8tで宣言されているため、1送信8bitまでの制限がある
// 割り込みを使用していないため、シリアルから受信すると表示抑止となって何も表示されない。
// 受信が完了すると受信した文字列を表示する
//-----------------------------------------------------------------------------
// Stringの扱いについて
// 長めのStringを順次差し替えて表示するような場合にはヒープが足りなくなるため、
// 一旦Stringをクリアするとよいが、ヒープが徐々に減っていくようである。
// Stringを明示的に開放する方法はないだろうか？
//=============================================================================
//#define DEBUG

// 定数定義
const int serialData = 19;      // シリアルデータ
const int shiftClk = 18;        // データシフトクロック
const int latchClk = 5;         // ラッチクロック
const int regClear = 17;        // シフトレジスタクリア
const int outputEnable = 16;    // 出力切り替え
bool isFirstTime = true;        //メイン処理初回実行
//String trainInfo = "";
//String buf = "";

// 表示関連
LedMatrixDriver LMD(serialData, shiftClk,
        latchClk, regClear, outputEnable); //LEDマトリクスドライバ

//コア温度取得関数定義（実験用）
extern "C" {
    uint8_t temprature_sens_read();
}


//*****************************************************************************
// WiFi関連 実験用
//*****************************************************************************
//const char* ssid = "Your SSID";
//const char* password = "Your password";
const char* meitetsuOpeHost = "http://top.meitetsu.co.jp/em/";
const char* localTrainHost = "http://192.168.11.10/cgi-bin/getTrainTime";
struct tm oldTm;
const int intervalMinutes = 30;
String meitetsuInfo = "";

//-----------------------------------------------------------------------------
// WiFiに接続する
//-----------------------------------------------------------------------------
void connectWiFi() {
    
    String buf = "ネットへ接続します";
    LMD.scrollString(buf);
    
    WiFi.mode(WIFI_STA);
    //WiFi.begin(ssid, password);
    WiFi.begin();

    while ( WiFi.waitForConnectResult() != WL_CONNECTED ) {
        //接続失敗→リブート
        buf = "接続失敗！ESP32を再起動します...";
        LMD.scrollString(buf);
        delay(5000);
        ESP.restart();
    }

    buf = "接続成功！";
    LMD.scrollString(buf);
    // buf = "";
    
}


//-----------------------------------------------------------------------------
// HTTP経由で指定URLのページを取得する
// 当然HTTPソースが返ってくるので、加工して表示に必要な情報のみ抜き出す必要あり
//-----------------------------------------------------------------------------
String getHTTPString(const char* hostURL) {
    HTTPClient http;
    String ret = "";

#ifdef DEBUG
    Serial.println(esp_get_free_heap_size());
#endif

    //取得開始
    http.begin(hostURL);
    int httpCode = http.GET();

    if ( httpCode > 0 )  {
        if ( httpCode == HTTP_CODE_OK ) {
            ret = http.getString();
        }
        
    } else {
        ret = http.errorToString(httpCode);
    }

#ifdef DEBUG
    Serial.println("getHttpString.ret ===>>");
    Serial.println(ret);
    Serial.println("<<<=== end of ret.");
    
#endif
    //取得終了
    http.end();
    
    return ret;
}


//*****************************************************************************
// シリアル通信関連
//*****************************************************************************
//-----------------------------------------------------------------------------
// 文字列受信
//-----------------------------------------------------------------------------
String receiveString() {
    String buffer = "No received data.";
        
    if ( Serial.available() ) {
        buffer = "";
        // なにがしか受信しているのであれば、受信文字列を受け取る
        
        //終端文字が受信されるまで無限ループ
        while (1) {
            if ( Serial.available() ) {
                char temp = char(Serial.read());
                buffer += temp;

                //改行コードを１送信分の終端として処理
                if ( temp == '\r' ) {
                    //受信文字列の前後の空白を取り除く
                    buffer.trim();
                    break;
                }
            }
        }
    }

    return buffer;
}

void showNextTrain() {
    // 直近の列車情報を取得
    String trainInfo = getHTTPString(localTrainHost);

    //取得文字列から列車の情報のみを抽出
    trainInfo = parseTimeTable(trainInfo);

    //表示
    LMD.scrollString(trainInfo); 
}


void showCoreTemprature() {
    //コア温度を取得
    float coreTemp = (temprature_sens_read() -32) /1.8;
    String temp = "Core 温度=";
    temp += String(coreTemp);
    temp += "℃";
    LMD.scrollString(temp);
}


//*****************************************************************************
// ヒープメモリサイズを表示する
//*****************************************************************************
void showFreeHeapSize() {
    // ヒープメモリサイズを取得
    String heap = "Free Heap Size = ";
    heap += String(esp_get_free_heap_size());
    LMD.scrollString(heap);
}



//*****************************************************************************
// 初期処理
//*****************************************************************************
void setup()
{
    // シリアル通信開始
    Serial.begin(115200);
    
    // レジスタクリア
    LMD.disableOutput();
    LMD.clearRegister();
    LMD.enableOutput();

    // LED全数チェック
    LMD.checkAllLED();


    // レジスタクリア
    LMD.disableOutput();
    LMD.clearRegister();
    LMD.enableOutput();

    // WiFi接続→NTPで現在時刻ゲット→切断
    connectWiFi();
    
    configTzTime("JST-9","ntp.nict.jp", "time.google.com", "ntp.jst.mfeed.ad.jp");

    //WiFi.disconnect();
    //あえてdisconnectしないでみよう
    

    

}

//*****************************************************************************
// メイン処理
//*****************************************************************************
void loop()
{
    String buf = "";
    struct tm timeInfo;
    
    LMD.clearRegister();

    delay(1000);
    
    getLocalTime(&timeInfo);
    
    //なにがしか受信しているなら文字列を取得
    if (Serial.available()) {
        buf = receiveString();
    } else {
        // シリアル受信なしなら現在時刻を表示する。
        buf = "只今:";
        buf += getTimeString(timeInfo);
    }
    
    // 現在時刻もしくはシリアル経由受信文字列を表示する
    LMD.scrollString(buf);
    buf = "";
    delay(1000);

    /*
    buf = "名鉄さん動いてますか～";
    LMD.scrollString(buf);

    if ( isFirstTime || isMeitetsuUpdate(timeInfo) ) {
    
        // 一定間隔にてHTTP経由で運行情報を取得
        buf = "運行情報を更新します";
        LMD.scrollString(buf);
        
        String htmlStr = getHTTPString(meitetsuOpeHost);
        meitetsuInfo = "";
        meitetsuInfo = parseMeitetsuInfo(htmlStr);
        htmlStr="";

        oldTm = timeInfo;
        isFirstTime = false;
    }
    
    LMD.scrollString(meitetsuInfo);
    */

    showNextTrain();
    showCoreTemprature();
    showFreeHeapSize();
    
    delay(100);

}

#ifdef DEBUG
/******************************************************************************
 * デバッグ用
 *****************************************************************************/
//-----------------------------------------------------------------------------
// デバッグ用出力 byteBuffer用
// byteBufferの現在位置のビットマップパターンをシリアルに出力する
//-----------------------------------------------------------------------------
void debugOutFontPos(uint8_t *data, int &charPos, int &bitPos) {
    Serial.print("charPos=");
    Serial.print(charPos);
    Serial.print(", bitPos=");
    Serial.println(bitPos);

    String pos = "　　　　　　　　";

    String cursor = "∨";
    pos.setCharAt(bitPos, cursor.charAt(0));
    Serial.println(pos);

    for ( int i = 0; i < 8; i++ ) {
        for ( int j = 0; j < 8; j++ ) {
            Serial.print(data[i] & bitMask >> j?"●":"〇");
        }
        Serial.println();
    }
    Serial.println();

}

//-----------------------------------------------------------------------------
// デバッグ用出力 マトリクス出力の編集領域用
// １行のビットマップパターンをシリアルに出力する
//-----------------------------------------------------------------------------
void debugOut32(uint32_t &data) {


    for ( int i = 0; i < 32; i++ ) {
        Serial.print(data & 0x80000000 >> i?"●":"〇");
    }
    Serial.println();
}
#endif
