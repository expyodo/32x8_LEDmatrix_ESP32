# 32x8_LEDmatrix_ESP32
自作ドライブ回路による 8x8LEDマトリクス４連の電光掲示板もどきプロジェクトです。

・マイコンはESP32 DevKit Cを使用

・開発環境は Linux Mint 1.9 (MATE Edition) + Arduino IDE + Arduino Core for ESP32

・LAN上に時刻表データを保持するWEBサーバが必要です。CGIが使えるものならなんでもいいと思います。
  
  当方は　Linux Mint 1.9 (MATE Edition) + Apache2 + Perl 5　で構成しました。
  
・以下のライブラリを使用させていただきました。

  https://github.com/mgo-tec/ESP32_SPIFFS_UTF8toSJIS
  
  https://github.com/mgo-tec/ESP32_SPIFFS_MisakiFNT
  
・列車表示機能については、

   ・名鉄の駅別時刻表（携帯版）をソースとして必要な情報を抽出し、カンマ区切りテキストファイルで保存する perlスクリプト
   
   ・先のファイルをもとにサーバ時間直近の１列車をシンプルなhtmlで出力する perlスクリプト
   
   の２つのスクリプトにより、サーバ側で元ネタを保持・抽出し、
   ESP32からはサーバに設置した1列車表示スクリプトにアクセスして得たデータを表示しています。
   1列車表示スクリプトについては、単にカンマをスペースで置換して表示文字列にしているだけですので、
   カンマ区切りテキストファイル作成スクリプトの方を作り直せば、他社線にも対応します。
