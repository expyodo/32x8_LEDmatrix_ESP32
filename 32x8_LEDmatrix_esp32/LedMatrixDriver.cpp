#include "LedMatrixDriver.h"

#include "ESP32_SPIFFS_MisakiFNT.h"
#include "ESP32_SPIFFS_UTF8toSJIS.h"
//=============================================================================
// LedMatrixDriver
// 32*8 LEDマトリクスのドライバクラス（実装ファイル）
//-----------------------------------------------------------------------------
// 2019/07/12 処理を分離の上クラス化して新規作成
//-----------------------------------------------------------------------------
//【使い方】
// ①当クラスのヘッダファイルをincludeして、インスタンス変数を定義する
// ②インスタンス生成時に制御ピンを指定していない場合はsetControlPinNoをコール
// ③使いたいメソッドを呼び出す
//-----------------------------------------------------------------------------
//【更新履歴】
// 2019/07/19 制御ピンを指定できるコンストラクタを追加
//            列送りの待機時間と行スキャン待機時間を設定できるメソッドを追加
//            あわせて両定数をメンバ変数化
//=============================================================================

//--- フォント定義ファイル名 ---------------------------------------------------
//フォント関連@mgo-tec
const char* _fontFileName = "/MSKG_13.FNT";
const char* _halfFontFileName = "/mgotec48.FNT";
const char* _utf8toSjisFileName = "/Utf8Sjis.tbl";

//--- マトリクス表示関連定数定義 -----------------------------------------------
const int _MATRIX_HEIGHT = 8;            // マトリクスの高さ（ドット）
const int _MATRIX_WIDTH = 32;           // マトリクスの幅（ドット）
const int _FONT_DOT_HEIGHT = 8;         // フォント１文字の高さ（ドット）
const int _FONT_DOT_WIDTH = 8;          // フォント１文字の幅（ドット）
const int _BIT_MASK = 128;              // ビットマスク（最上位1bitのみ）
const int _MAX_LENGTH_OF_STRING = 400;  // 表示対象文字列の最大長さ

//--- マトリクス制御用ピン番号 -------------------------------------------------
int _serialData;        //シリアルデータ SER
int _shiftClk;          //シフトクロック SRCLK
int _latchClk;          //ラッチクロック RCLK
int _registerClear;     //レジスタクリア SRCLR
int _outputEnable;      //出力切り替え   OE

//--- 制御用変数 ------------------------------------------------------------
int _scrollDelayTime = 40;          // スクロールで列送りする際に待つ時間（ミリ秒）
int _refreashRate = 15;             // 行スキャンの1行あたり待機時間（マイクロ秒）

ESP32_SPIFFS_MisakiFNT _MFNT;       //美咲フォント@mgo-tec 利用クラス
uint8_t _fontBitmapBuffer[_MAX_LENGTH_OF_STRING][_FONT_DOT_HEIGHT]; //フォント変換後のビットマップバッファ

//=============================================================================
// コンストラクタ
//=============================================================================
LedMatrixDriver::LedMatrixDriver() {
    //_stringLength = 0;
    Serial.println("LedMatrixDriver begin");
}

//=============================================================================
// コンストラクタ（制御ピン番号指定版）
//=============================================================================
LedMatrixDriver::LedMatrixDriver(const int &dataPin, const int &shiftClockPin, 
            const int &latchClockPin, const int &clearClockPin, const  int &OEPin)
{
    setControlPinNo(dataPin, shiftClockPin, latchClockPin, clearClockPin, OEPin);               
}

//-----------------------------------------------------------------------------
// setControlPinNo
// マトリクス制御用のピン番号を設定する
//-----------------------------------------------------------------------------
//【引数】
//      int &dataPin        SER     シリアルデータ
//      int &shiftClockPin  SRCLK   シフトクロック
//      int &latchClockPin  RCLK    ラッチクロック
//      int &clearClockPin  SRCLR   レジスタクリア
//      int &OEPin          OE      出力切り替え
//-----------------------------------------------------------------------------
void LedMatrixDriver::setControlPinNo(const int &dataPin, const int &shiftClockPin,
        const int &latchClockPin, const int &clearClockPin, const int &OEPin) {

    // 引数をローカルへセット
    _serialData = dataPin;
    _shiftClk = shiftClockPin;
    _latchClk = latchClockPin;
    _registerClear = clearClockPin;
    _outputEnable = OEPin;

    Serial.println("SER: " + String(_serialData));
    Serial.println("SRCLK: " + String(_shiftClk));
    Serial.println("RCLK: " + String(_latchClk));
    Serial.println("SRCLR: " + String(_registerClear));
    Serial.println("OE: " + String(_outputEnable));

    // 各ピンを出力設定
    pinMode(_serialData, OUTPUT);
    pinMode(_shiftClk, OUTPUT);
    pinMode(_latchClk, OUTPUT);
    pinMode(_registerClear, OUTPUT);
    pinMode(_outputEnable, OUTPUT);
    
}

//-----------------------------------------------------------------------------
// マトリクス内の全LEDの点灯チェック
//-----------------------------------------------------------------------------
void LedMatrixDriver::checkAllLED() {
    uint32_t colData = 0X80000000;
    uint32_t buffer = 0;

    // スキャン中の行の全ての列を一つずつ点灯し、行を一つずつ送っていく
    for ( int row = 0; row < _MATRIX_HEIGHT; row++ ) {
        for ( int col = 0; col < _MATRIX_WIDTH; col++ ) {
            buffer = colData >> col;
            LedMatrixDriver::totalColShiftOut(buffer);

            rowShiftOut(_BIT_MASK >> row);

            LedMatrixDriver::updateStorage();
            delay(15);
        }
    }
}

//=============================================================================
// scrollString
// 指定した文字列をスクロール表示する
//-----------------------------------------------------------------------------
//【引数】
//      String  &targetString   表示対象文字列
//-----------------------------------------------------------------------------
// スクロール方向は右→左固定    
//=============================================================================
void LedMatrixDriver::scrollString(const String &targetString) {
    int bitPos = 0;         //フォントビットマップのビット位置（0→7）
    int charPos =0;         //byteBuffer基準の文字位置
    int shiftCount = 0;     //シフトカウンタ（文字列読込完了後のサプレス用）

    uint32_t buf = 0;       //行単位でのビットマップ退避用
    boolean isScrollEnd = false;    //スクロール終了フラグ
    boolean isEndOfBuffer = false;  //バッファ終端フラグ
    uint32_t dotMatrix[_MATRIX_HEIGHT]; //LEDマトリクスのドット編集領域

    //スクロール処理前に編集領域をクリアする
    clearMatrixBitmap(dotMatrix);

    //表示対象文字列バッファをフォント変換する
    getFontToBuffer(targetString);

    // スクロールが完了するまで無限ループ
    while ( !isScrollEnd ) {

        if (Serial.available() ) {
            break;
        }

        //制御対象行を選択
        for ( int row = 0; row < _MATRIX_HEIGHT; row++ ) {
            //一旦退避
            buf = dotMatrix[row];
            //1列左へシフト
            buf = buf << 1;

            if ( !isEndOfBuffer ) {
                //まだフォントバッファを全部読んでなければ、
                //フォントバッファの現在位置から一列読み取って末尾へセット
                bitWrite(buf, 0,
                        bitRead(_fontBitmapBuffer[charPos][row], (7 - bitPos)));
            }
            // 編集後のパターンをマトリクスへセット
            dotMatrix[row] = buf;
        }

        if (Serial.available() ) {
            break;
        }

        //実際にマトリクスへ出力
        outputToMatrix(dotMatrix);

        //終了判定
        isEndOfBuffer = endOfString(charPos, targetString);
        if ( isEndOfBuffer ) {
            //ビットマップを全て読み込んだので、単純にシフトする
            shiftCount++;
            if ( shiftCount >= _MATRIX_WIDTH ) {
                // シフトし終わったのでスクロール終了
                isScrollEnd = true;
            }

        } else {
            //読込開始位置を一つ後ろへ
            bitPos++;
            if ((bitPos % 8) == 0) {
                //一文字読んだので次の文字へ
                charPos++;
                bitPos = 0;
            }
        }
    }
}

//=============================================================================
// レジスタ（シフト・ストレージ共）クリア
//=============================================================================
void LedMatrixDriver::clearRegister() {
    digitalWrite(_registerClear, LOW);
    digitalWrite(_registerClear, HIGH);
    LedMatrixDriver::updateStorage();
}

//=============================================================================
// シフトレジスタの内容をストレージレジスタにラッチする
//=============================================================================
void LedMatrixDriver::updateStorage() {
    digitalWrite(_latchClk, LOW);
    digitalWrite(_latchClk, HIGH);
}

//=============================================================================
// シフトレジスタからの出力を有効にする
//=============================================================================
void LedMatrixDriver::enableOutput() {
    //負論理
    digitalWrite(_outputEnable, LOW);
}

//=============================================================================
// シフトレジスタからの出力を無効にする
//=============================================================================
void LedMatrixDriver::disableOutput() {
    //負論理
    digitalWrite(_outputEnable, HIGH);  
}

//=============================================================================
// １列あたりの表示時間を設定する（=スクロールのスピード）
//=============================================================================
void LedMatrixDriver::setScrollDelayTime(int milliSec) {
    _scrollDelayTime = milliSec;
}

//=============================================================================
// 行スキャンの１行あたりの表示時間
//=============================================================================
void LedMatrixDriver::setRefreashRate(int microSec) {
    _refreashRate = microSec;
}
/******************************************************************************
 * これよりprivateメンバ
 *****************************************************************************/
 
//=============================================================================
// LEDマトリクスへビットマップパターンを出力する
//-----------------------------------------------------------------------------
// 【引数】
//      uint32_t    *matrixBitMap 実際のLEDマトリクスに対応する編集領域
//=============================================================================
void LedMatrixDriver::outputToMatrix(uint32_t *matrixBitmap) {
    unsigned long startTime = millis(); //表示開始時間

    //指定時間経過するまで同じデータを表示する。
    while( millis() < startTime + _scrollDelayTime ) {

        for ( int row = 0; row < _FONT_DOT_HEIGHT; row++ ) {
            
            LedMatrixDriver::totalColShiftOut(matrixBitmap[row]);   //  列表示パターン出力
            LedMatrixDriver::rowShiftOut(_BIT_MASK >> row);         //  行スキャン

            LedMatrixDriver::updateStorage(); //データ送信が終わったのでストレージへラッチ
            delayMicroseconds(_refreashRate); //次の行へ移る前に表示状態で待機
        }
    }
}
//=============================================================================
// マトリクス出力用ビットマップ編集領域をクリアする
//-----------------------------------------------------------------------------
// 【引数】
//      uint32_t    *matrixBitMap 実際のLEDマトリクスに対応する編集領域
//=============================================================================
void LedMatrixDriver::clearMatrixBitmap(uint32_t *matrixBitmap) {
    for ( int i = 0; i < _FONT_DOT_HEIGHT; i++ ) {
        matrixBitmap[i] = 0;
    }
}

//=============================================================================
// 指定された文字列のフォントを取得してバッファに格納する
//-----------------------------------------------------------------------------
//【引数】
//      String &targetString    表示対象文字列
//=============================================================================
void LedMatrixDriver::getFontToBuffer(const String &targetString) {
    LedMatrixDriver::clearFontBuffer();

#ifdef DEBUG    
    Serial.println(" clearBitMapBuffer() Done.");
#endif

    _MFNT.SPIFFS_Misaki_Init3F(_utf8toSjisFileName, _halfFontFileName, _fontFileName);
    _MFNT.StrDirect_MisakiFNT_readALL(targetString, _fontBitmapBuffer);
    _MFNT.SPIFFS_Misaki_Close3F();
}

//=============================================================================
// フォント格納用バッファをクリアする
//=============================================================================
void LedMatrixDriver::clearFontBuffer() {
    for ( int i = 0; i < _MAX_LENGTH_OF_STRING; i++ ) {
        for ( int j = 0; j < _FONT_DOT_HEIGHT; j++ ) {
            _fontBitmapBuffer[i][j] = 0;
        }
    }
}

//=============================================================================
// ビットマップ文字列の終了判定
// byteBuffer基準で文字列の終端を判定する
//-----------------------------------------------------------------------------
// 【引数】 &charPos byteBuffer内での文字位置
//          &pStr 表示文字列のString表現
// 【戻り値】終端に達していたらtrue、達していなかったらFalse
//-----------------------------------------------------------------------------
// 【備考】
//UTF-8文字列が元ネタなのでlength=文字数ではない。（Ascii=1,非Ascii=3)
//フォント変換時に半角文字は１文字分の領域の半分に収まる様展開されるので、
//文字列終端を文字数でハンドリングできない
//そのため、便宜的に中央付近の行が０の場合にデータなし＝空白で、
//かつ次の文字も同じ判定で空白あるいはlengthを超えたならば文字列終端とする
//=============================================================================
bool LedMatrixDriver::endOfString(const int &charPos, const String &pStr) {
    boolean ret = false;

    if ( charPos >= pStr.length() ) {
        //UTF-8文字列終端に達したら終端（まずありえない）
        ret = true;
    }
    if ( _fontBitmapBuffer[charPos][4] == 0
            && _fontBitmapBuffer[charPos+1][4] == 0 ) {
        //現在位置と現在位置の一文字後ろが空白ならば終端。
        ret = true;
    }

    return ret;
}

//=============================================================================
// 列側データ一括出力
// digitalWriteのval引数がuint_8tなので、8bitずつ送信しなければならない
//=============================================================================
void LedMatrixDriver::totalColShiftOut(const uint32_t &data) {
    int sendLimit = (_MATRIX_WIDTH / 8) - 1; // 全送信回数 カーソルのドット幅/8
    uint8_t targetByte = 0;     // 送信対象のデータを格納するバッファ

    // 送信回数分送信処理を行う
    for ( int sendCount = sendLimit; sendCount >= 0; sendCount-- ) {
        // 送信できるビット幅分のデータをバッファに格納
        targetByte = data >> (sendCount * 8);

        // バッファ内のデータを送信
        for ( int i = 0; i < 8; i++ ) {
            digitalWrite(_shiftClk, LOW);
            digitalWrite(_serialData, targetByte &_BIT_MASK >> i);
            digitalWrite(_shiftClk, HIGH);
        }
    }
}

//=============================================================================
// 行側データ出力
//=============================================================================
void LedMatrixDriver::rowShiftOut(const uint8_t &data) {
    for ( int i = 0; i < _MATRIX_HEIGHT; i++ ) {
        digitalWrite(_shiftClk, LOW);
        digitalWrite(_serialData, data & _BIT_MASK >> i);
        digitalWrite(_shiftClk, HIGH);
    }
}


//-----------------------------------------------------------------------------
// デバッグ用出力 マトリクス出力の編集領域用
// １行のビットマップパターンをシリアルに出力する
//-----------------------------------------------------------------------------
void LedMatrixDriver::debugOut32(uint32_t &data) {


    for ( int i = 0; i < 32; i++ ) {
        Serial.print(data & 0x80000000 >> i?"●":"〇");
    }
    Serial.println();
}
