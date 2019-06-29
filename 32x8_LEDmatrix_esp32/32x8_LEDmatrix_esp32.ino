//#include "Arduino.h"
#include "ESP32_SPIFFS_MisakiFNT.h"
#include "ESP32_SPIFFS_UTF8toSJIS.h"

//-----------------------------------------------------------------------------
// LEDマトリクスによる電光掲示板もどき 8*32ドット
//-----------------------------------------------------------------------------
// 2019/06/29 更新
// ESP32-WROOM-32への移植作業ひとまず完了
//=============================================================================
// 8*8マトリクスを横に四つ繋げた電光掲示板もどきを制御するプログラム
//-----------------------------------------------------------------------------
//【予約ピン】
// SDカードをSPI接続で使用するため、以下のピンをSPIライブラリで使用する。
// 4:CS
// 11:MOSI
// 12:MISO
// 13:CLK
// LEDマトリクス制御部への接続ピン
// 19	:SER  (Data)
// 18	:SRCLK(Shift)
// 5	:RCLK (Latch)
// 17	:SRCLR(Clear)
// 10	:OE  (Enable)※論理反転なので注意
//-----------------------------------------------------------------------------
// 更新履歴
// 2019/06/29 スクロール処理を見直して作り直した。
//-----------------------------------------------------------------------------
// 制限事項 2019/06/29 更新
// ESP32用美咲フォントライブラリ@mgo-tecに対応
// 表示のちらつきが気になる(Arduino Unoでは発生しない)
//     →5v接続で発生。3.3v接続は発生しない
// digitalWriteの引数がuint_8tで宣言されているため、1送信8bitまでの制限がある
// 割り込みを使用していないため、シリアルから受信すると表示抑止となって何も表示されない。
// 受信が完了すると受信した文字列を表示する
// 文字列先頭とマトリクス先頭が同一なので先頭をいきなり表示した状態からスクロールが開始される
//-----------------------------------------------------------------------------

// 定数定義
const int serialData = 19;		// シリアルデータ
const int shiftClk = 18;  		// データシフトクロック
const int latchClk = 5;			// ラッチクロック
const int regClear = 17;    	// シフトレジスタクリア
const int outputEnable = 16;	// 出力切り替え

// 表示関連
const int cursorHeight = 8;		// カーソルの高さ
const int cursorWidth = 32; 	// カーソルの幅
const int colsInRow = cursorWidth - 1;
const int bitMask = 128;		// ビットマスク（上位１bitのみ）

const int scrollDelayTime = 100;	// 次に1列スクロールするまでの待機時間
uint32_t dispCursor[cursorHeight];	// 表示用カーソル
String strBuffer = "";				// 文字列バッファ
uint8_t byteBuffer[400][cursorHeight]; // フォント変換後の文字列バッファ領域（max:120文字)
int bufferLength = 0;			// バッファの長さ（=シリアル受信した文字列の文字列長）
const int fontDotHeight = 8;	// フォント1文字のドット高
const int fontDotWidth = 8;		// フォント1文字のドット幅

//フォント関連
ESP32_SPIFFS_MisakiFNT MFNT;
const char* fontFileName = "/MSKG_13.FNT";
const char* halfFontFileName = "/mgotec48.FNT";
const char* utf8toSjisFileName = "/Utf8Sjis.tbl";


//*****************************************************************************
// 表示関連関数群
//*****************************************************************************
//-----------------------------------------------------------------------------
// 文字列をスクロールする
// 本関数はbyteBufferの先頭より取得した文字列を順次右→左へスクロールする。
//-----------------------------------------------------------------------------
void newScrollString() {
	int bitPos = 0;			//フォントビットマップのビット位置（0→7）
	int charPos =0;			//byteBuffer基準の文字位置
	int shiftCount = 0;		//シフトカウンタ（文字列読込完了後のサプレス用）

	uint32_t buf = 0;		//行単位でのビットマップ退避用
	boolean isScrollEnd = false;	//スクロール終了フラグ
	boolean isEndOfBuffer = false;	//バッファ終端フラグ
	uint32_t dotMatrix[fontDotHeight];	//LEDマトリクスのドット編集領域


	//スクロール処理前に編集領域をクリアする
	clearDotMatrix(dotMatrix);

	// スクロールが完了するまで無限ループ
	while ( !isScrollEnd ) {

		if (Serial.available() ) {
			break;
		}

		//制御対象行を選択
		for ( int row = 0; row < 8; row++ ) {
			//一旦退避
			buf = dotMatrix[row];
			//1列左へシフト
			buf = buf << 1;

			if ( !isEndOfBuffer ) {
				//まだフォントバッファを全部読んでなければ、
				//フォントバッファの現在位置から一列読み取って末尾へセット
				bitWrite(buf, 0,
						bitRead(byteBuffer[charPos][row], (7 - bitPos)));
			}
			// 編集後のパターンをマトリクスへセット
			dotMatrix[row] = buf;
		}

		if (Serial.available() ) {
			break;
		}

		//実際にマトリクスへ出力
		outputMatrix(dotMatrix);

		//終了判定
		isEndOfBuffer = endOfBuffer(charPos);
		if ( isEndOfBuffer ) {
			//ビットマップを全て読み込んだので、単純にシフトする
			shiftCount++;
			if ( shiftCount >= cursorWidth ) {
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

//-----------------------------------------------------------------------------
// ビットマップ文字列の終了判定
// byteBuffer基準で文字列の終端を判定する
//-----------------------------------------------------------------------------
// 【引数】 charPos byteBuffer内での文字位置
// 【戻り値】終端に達していたらtrue、達していなかったらFalse
//-----------------------------------------------------------------------------
// 【備考】
//UTF-8文字列が元ネタなのでlength=文字数ではない。（Ascii=1,非Ascii=3)
//フォント変換時に半角文字は１文字分の領域の半分に収まる様展開されるので、
//文字列終端を文字数でハンドリングできない
//そのため、便宜的に中央付近の行が０の場合にデータなし＝空白で、
//かつ次の文字も同じ判定で空白あるいはlengthを超えたならば文字列終端とする
//-----------------------------------------------------------------------------
boolean endOfBuffer(int &charPos) {
	boolean ret = false;


	if ( charPos >= strBuffer.length() ) {
		//UTF-8文字列終端に達したら終端（まずありえない）
		ret = true;
		Serial.println("--- end of buffer. ---");
	}
	if ( byteBuffer[charPos][4] == 0
			&& byteBuffer[charPos+1][4] == 0 ) {
		//現在位置と現在位置の一文字後ろが空白ならば終端。
		ret = true;
		Serial.println("--- end of buffer. ---");
	}

	return ret;
}

//-----------------------------------------------------------------------------
// LEDマトリクスへビットマップパターンを出力する
//-----------------------------------------------------------------------------
// 【引数】matrix 実際のLEDマトリクスに対応する要素数を確保した編集領域
//-----------------------------------------------------------------------------
void outputMatrix(uint32_t *matrix) {

	unsigned long startTime = millis(); //表示開始時間

	//指定時間経過するまで同じデータを表示する。
	while( millis() < startTime + scrollDelayTime ) {

		for ( int row = 0; row < fontDotHeight; row++ ) {

			totalColShiftOut(matrix[row]);	//	列表示パターン出力
			rowShiftOut(bitMask >> row);	//	行スキャン

			updateStorage(); //データ送信が終わったのでストレージへラッチ
			delayMicroseconds(85); //次の行へ移る前に表示状態で待機
		}
	}
}

//-----------------------------------------------------------------------------
// マトリクス出力用ビットマップ編集領域をクリアする
//-----------------------------------------------------------------------------
// 【引数】matrix 実際のLEDマトリクスに対応する要素数を確保した編集領域
//-----------------------------------------------------------------------------
void clearDotMatrix(uint32_t *matrix) {
	for ( int i = 0; i < fontDotHeight; i++ ) {
		matrix[i] = 0;
	}
}

//------------------------------------------------------------------------------------
// 指定された文字列のフォントを取得してバッファに格納する
// 引数：表示する文字列のString表現
//------------------------------------------------------------------------------------
void getFontToBuffer( const String &originalString ) {

	Serial.println("----------- getFontToBuffer Start. -------------");
	clearBitMapBuffer();
	Serial.println(" clearBitMapBuffer() Done.");

	MFNT.SPIFFS_Misaki_Init3F(utf8toSjisFileName, halfFontFileName, fontFileName);
	MFNT.StrDirect_MisakiFNT_readALL(originalString, byteBuffer);
	MFNT.SPIFFS_Misaki_Close3F();

	Serial.println("----------- getFontToBuffer End. -------------");
}

void clearBitMapBuffer() {
	for ( int i = 0; i < 400; i++ ) {
		for ( int j = 0; j < fontDotHeight; j++ ) {
			byteBuffer[i][j] = 0;
		}
	}
}

//------------------------------------------------------------------------------------
// 起動時LEDマトリクスチェック。LEDを順番に一つずつ全て点灯する。
//------------------------------------------------------------------------------------
void checkAllLED() {
	uint32_t colData = 0X80000000;
	uint32_t buffer = 0;

	// スキャン中の行の全ての列を一つずつ点灯し、行を一つずつ送っていく
	for ( int row = 0; row < cursorHeight; row++ ) {
		for ( int col = 0; col < cursorWidth; col++ ) {
			buffer = colData >> col;
			totalColShiftOut(buffer);

			rowShiftOut(bitMask >> row);

			updateStorage();
			delay(15);
		}
	}
}
//-----------------------------------------------------------------------------
// 列側データ一括出力
// digitalWriteのval引数がuint_8tなので、8bitずつ送信しなければならない
//-----------------------------------------------------------------------------
void totalColShiftOut( const uint32_t &data) {
	int sendLimit = (cursorWidth / 8) - 1; // 全送信回数 カーソルのドット幅/8
	uint8_t targetByte = 0;		// 送信対象のデータを格納するバッファ

	// 送信回数分送信処理を行う
	for ( int sendCount = sendLimit; sendCount >= 0; sendCount-- ) {
		// 送信できるビット幅分のデータをバッファに格納
		targetByte = data >> (sendCount * 8);

		// バッファ内のデータを送信
		for ( int i = 0; i < 8; i++ ) {
			digitalWrite(shiftClk, LOW);
			digitalWrite(serialData, targetByte & bitMask >> i);
			digitalWrite(shiftClk, HIGH);
		}
	}

}

//-----------------------------------------------------------------------------
// 行側データ出力
//-----------------------------------------------------------------------------
void rowShiftOut( const uint8_t &data ) {

	for ( int i = 0; i < 8; i++ ) {
		digitalWrite(shiftClk, LOW);
		digitalWrite(serialData, data & bitMask >> i);
		digitalWrite(shiftClk, HIGH);
	}
}
//*****************************************************************************
// シリアル通信関連
//*****************************************************************************
//-----------------------------------------------------------------------------
// 文字列受信
//-----------------------------------------------------------------------------
void receiveString() {
	if ( Serial.available() ) {
		// なにがしか受信しているのであれば、受信文字列を受け取る
		String buffer;

		//終端文字が受信されるまで無限ループ
		while (1) {
			if ( Serial.available() ) {
				char temp = char(Serial.read());
				buffer += temp;

				//改行コードを１送信分の終端として処理
				if ( temp == '\r' ) {
					//受信文字列の前後の空白を取り除く
					buffer.trim();
					strBuffer = buffer;
					bufferLength = buffer.length();

					//文字列をフォントに変換する
					getFontToBuffer(buffer);

					Serial.print(strBuffer);
					break;
				}
			}
		}
	}
}

//*****************************************************************************
// シフトレジスタ制御関連関数群
//*****************************************************************************
//-----------------------------------------------------------------------------
// レジスタ（シフト・ストレージ共）クリア
//-----------------------------------------------------------------------------
void clearRegister() {
	digitalWrite(regClear, LOW);
	digitalWrite(regClear, HIGH);
	updateStorage();
}

//-----------------------------------------------------------------------------
// シフトレジスタをストレージレジスタにラッチする
//-----------------------------------------------------------------------------
void updateStorage() {
	digitalWrite(latchClk, LOW);
	digitalWrite(latchClk, HIGH);
}

//-----------------------------------------------------------------------------
// シフトレジスタからの出力を有効にする
//-----------------------------------------------------------------------------
void enableOutput() {
	digitalWrite(outputEnable, LOW);
}

//-----------------------------------------------------------------------------
// シフトレジスタからの出力を無効にする
//-----------------------------------------------------------------------------
void disableOutput() {
	digitalWrite(outputEnable, HIGH);
}

//*****************************************************************************
// 初期処理
//*****************************************************************************
void setup()
{
	// GPIOピンの設定
	pinMode(serialData, OUTPUT);
	pinMode(shiftClk, OUTPUT);
	pinMode(latchClk, OUTPUT);
	pinMode(regClear, OUTPUT);
	pinMode(outputEnable, OUTPUT);

	// レジスタクリア
	disableOutput();
	clearRegister();
	enableOutput();

	// LED全数チェック
	checkAllLED();


	// レジスタクリア
	disableOutput();
	clearRegister();
	enableOutput();

	// シリアル通信開始
	Serial.begin(115200);


	// フォント関連初期化
	strBuffer = "春はあけぼの。やうやう白くなりゆく、山ぎはすこしあかりて、むらさきだちたる雲のほそくたなびきたる。夏は夜。月の頃はさらなり、やみもなほ、ほたるの多く飛びちがひたる。また、ただ一つ二つなど、ほのかにうち光りて行くもをかし。";
	bufferLength = strBuffer.length();
	getFontToBuffer(strBuffer);

}

//*****************************************************************************
// メイン処理
//*****************************************************************************
void loop()
{

	clearRegister();

	newScrollString();

	//なにがしか受信しているなら文字列を取得
	if (Serial.available()) {
		receiveString();
	}
	delay(100);

}

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

	pos.setCharAt(bitPos, '∨');
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
