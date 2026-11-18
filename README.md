# widi-key

Yazdığın tariften yapay zekayla ses üreten ve o sesi kendi klavyesinden çalmanı sağlayan Logic Pro (Audio Unit) enstrümanı.

## Kurulum

1. Bu sayfanın sağındaki **Releases** bölümünden en son sürümdeki `widi-key-AU.zip` dosyasını indir.
2. Zip'i aç. Çıkan `widi-key.component` dosyasını şu klasöre taşı:
   `~/Library/Audio/Plug-Ins/Components`
   (Finder'da **Git → Klasöre Git…** menüsüne bu yolu yapıştırabilirsin.)
3. Plugin imzasız olduğu için macOS ilk açılışta engelleyebilir. Terminal'i açıp şunu yapıştır:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/widi-key.component
   ```
4. Logic'i aç. Yeni bir Software Instrument kanalı ekle ve **AU Instruments → wabiblack → widi-key**'i seç.
   Plugin görünmezse Logic'te **Ayarlar → Plug-in Yöneticisi**'nden widi-key için yeniden tarama yap.

## Kullanım

1. **Ayarlar**'a tıkla ve ElevenLabs API anahtarını yapıştır. Ücretsiz planla ayda yaklaşık 50 ses üretebilirsin. Ücretsiz planda üretilen sesler sadece kişisel kullanım içindir.
2. Ortadaki kutuya sesi tarif et. İngilizce tarifler daha iyi sonuç veriyor, örneğin `warm dusty music box`. Sonra **Üret**'e bas.
3. Ses birkaç saniyede gelir ve klavyenin tüm tuşlarına perdeli olarak dağıtılır. Kök nota C3.
4. Attack, Decay, Sustain, Release, Filtre ve Reverb düğmeleriyle sesi şekillendirebilirsin. **Süre** düğmesi bir sonraki üretilecek sesin uzunluğunu belirler.

Üretilen ses Logic projesiyle birlikte kaydedilir. Projeyi tekrar açtığında ses yerinde durur.

## Derleme

`main` dalına yapılan her push'ta GitHub Actions, Plugin'i bulutta bir Mac üzerinde derler ve yeni bir Release yayınlar.

Altyapı olarak [JUCE](https://juce.com) kullanılıyor.
