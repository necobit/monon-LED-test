# M5AtomS3 LED Ring Controller

M5AtomS3 + WS2812B LEDリング + Joystick2 による調光コントローラ。ジョイスティックで色温度と輝度を直感的に操作できる。

## ハードウェア構成

| 部品 | 仕様 |
|------|------|
| M5AtomS3 | ESP32-S3, 0.85インチLCD (128x128) |
| WS2812B LEDリング | 16個, GPIO5接続 |
| M5Unit-Joystick2 | I2C (SDA=G2, SCL=G1, Addr=0x63) |

## 操作方法

| 操作 | 機能 |
|------|------|
| ジョイスティック X軸 | 色温度 (左=暖色1900K / 右=寒色7000K) |
| ジョイスティック Y軸 | 輝度 (上=明るい / 下=暗い) |
| ジョイスティック ボタン | LED ON/OFF (500msフェード付き) |
| AtomS3 本体ボタン | 編集モード / ライトモード切替 |

### モード

- **EDIT (編集モード)**: ジョイスティックで色温度・輝度をリアルタイム調整
- **LIGHT (ライトモード)**: パラメータ固定。ジョイスティックXY無効

## ディスプレイ表示

AtomS3の画面に現在のパラメータを表示する。

- モード (EDIT=青バー / LIGHT=緑バー)
- 色プレビュー (現在の色温度を矩形で表示)
- 色温度 (K)
- 輝度 (%)
- ON/OFF状態

## 色温度テーブル

FastLEDの色温度定数をアンカーポイントとし、区間線形補間で滑らかに遷移する。

| 色温度 | 用途 |
|--------|------|
| 1900K | Candle |
| 2600K | Tungsten 40W |
| 2850K | Tungsten 100W |
| 3200K | Halogen |
| 5200K | Carbon Arc |
| 6000K | Direct Sunlight |
| 7000K | Overcast Sky |

## ノイズ対策

ジョイスティックのADC値に対して2段階のフィルタを適用。

1. **メディアンフィルタ**: 3回読み取りの中央値でスパイクノイズ除去
2. **EMAフィルタ**: 指数移動平均 (alpha=0.15) で滑らかに追従

## ビルド・書き込み

[PlatformIO](https://platformio.org/) を使用。

```bash
# ビルド
pio run

# 書き込み
pio run --target upload

# シリアルモニタ
pio device monitor
```

## 依存ライブラリ

- [FastLED](https://github.com/FastLED/FastLED)
- [M5Unit-Joystick2](https://github.com/m5stack/M5Unit-Joystick2)
- [M5Unified](https://github.com/m5stack/M5Unified) (M5Unit-Joystick2の依存で自動インストール)
