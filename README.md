# thermo-hygrometer
ESP32 + AHT25
(+ ST7789 (OLED display, optional))

Wi-Fi接続し，AHT25から温湿度を取得，AmbientやGoogle Apps Script にロギングしていく．
ディスプレイがある場合は，日時とともに最新の測定値を表示する．

# Wi-Fi/ambient/Google Apps Script接続について
SSIDとパスワードなどのクレデンシャル情報は`wifi_credentials.h`などのクレデンシャル用ヘッダファイルに定義しておく．

このファイルは`.gitignore`で同期しないようになっている．（インシデントは良くないので）

雛形は `wifi_credentials.h.sample` 等の`.sample`に掲示するので，適宜内容を変更の上，`wifi_credentials.h`にリネームすることで使用可能と思われる．
