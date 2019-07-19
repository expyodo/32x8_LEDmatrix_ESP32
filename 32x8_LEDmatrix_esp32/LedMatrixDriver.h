#ifndef __LED_MATRIX_DRIVER__
#define __LED_MATRIX_DRIVER__

#include "ESP32_SPIFFS_MisakiFNT.h"
#include "ESP32_SPIFFS_UTF8toSJIS.h"
//=============================================================================
// LedMatrixDriver
// 32*8 LEDマトリクスのドライバクラス（ヘッダファイル）
//-----------------------------------------------------------------------------
// 2019/07/12 処理を分離の上クラス化して新規作成
//-----------------------------------------------------------------------------
//【使い方】
// ①当クラスのヘッダファイルをincludeして、インスタンス変数を定義する
// ②インスタンス生成時に制御ピンを指定していない場合はsetControlPinNoをコール
// ③使いたいメソッドを呼び出す
//-----------------------------------------------------------------------------
//【制限事項】
// 8*8を横に4つつなげた32*8マトリクスを、列側４つ・行側１つの74HC595で
// ドライブする回路専用のドライバクラス
// データの送り順は一番左側の列レジスタ～右側末尾の列レジスタ→行側レジスタで固定
// 送り方が共通であればほかのドライバICの回路でも動くかもしれない。
//=============================================================================

class LedMatrixDriver {
public:
    // コンストラクタ
    LedMatrixDriver();
    // コンストラクタ（ピン番号指定版）
    LedMatrixDriver(const int &dataPin, const int &shiftClockPin, 
            const int &latchClockPin, const int &clearClockPin, const  int &OEPin);
    // 制御ピン番号設定
    void setControlPinNo(const int &dataPin, const int &shiftClockPin,
            const int &latchClockPin, const int &clearClockPin, const int &OEPin);
    // マトリクスの全LEDの点灯チェック
    void checkAllLED(); 
    // 文字列スクロール
    void scrollString(const String &targetString);
    // シフトレジスタクリア
    void clearRegister();   
    // ストレージレジスタ更新
    void updateStorage();   
    // 出力イネーブル
    void enableOutput();
    // 出力ディセーブル
    void disableOutput();
    // スクロールの１列あたりの表示時間設定
    void setScrollDelayTime(int milliSec);
    // 行スキャンの１行あたりの表示時間設定
    void setRefreashRate(int microSec);

private:
    // フォントビットマップをマトリクスへ出力
    void outputToMatrix(uint32_t *matrixBitmap);
    // マトリクス用バッファのクリア
    void clearMatrixBitmap(uint32_t *matrixBitmap);
    // フォント用バッファのクリア
    void clearFontBuffer();
    // 指定文字列に対応したビットマップフォントを取得してバッファに格納する
    void getFontToBuffer(const String &targetString);
    // 文字列終端を判定する
    bool endOfString(const int &charPos, const String &pStr);
    // 列側データをHWへ出力する
    void totalColShiftOut(const uint32_t &data);
    // 行側データをHWへ出力する
    void rowShiftOut(const uint8_t &data);

    void debugOut32(uint32_t &data);

};

#endif /* __LED_MATRIX_DRIVER__ */
