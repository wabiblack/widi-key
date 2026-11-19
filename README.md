# widi-key

*made in wabi*

Türkçe tarif ettiğin sesi yapay zekayla üreten ve kendi klavyesinden çalmanı sağlayan Logic Pro (Audio Unit) enstrümanı.

## Plugin kurulumu

1. **Releases** bölümünden en son `widi-key-AU.zip` dosyasını indir ve aç.
2. Finder'da **Cmd+Shift+G**'ye bas, `~/Library/Audio/Plug-Ins/Components` yaz. Çıkan `widi-key.component` dosyasını bu klasöre sürükle (eskisi varsa üzerine yaz).
3. Terminal'de şunu çalıştır:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/widi-key.component
   ```
4. Logic'te **AU Instruments → wabiblack → widi-key**'i seç.

## Yerel motor kurulumu (internetsiz, limitsiz)

Yerel motor Mac'inde arka planda çalışır. İki işi var: Türkçe tarifini İngilizceye çevirmek ve Stable Audio Open Small ile sesi üretmek.

1. Önce [stable-audio-open-small](https://huggingface.co/stabilityai/stable-audio-open-small) sayfasında giriş yap ve koşulları kabul et.
2. [huggingface.co/settings/tokens](https://huggingface.co/settings/tokens) adresinden **Read** yetkili bir token oluştur.
3. Terminal'e şunu yapıştır:
   ```
   curl -fsSL https://raw.githubusercontent.com/wabiblack/widi-key/main/engine/install.sh | bash
   ```
   Kurulum token'ı soracak. Modeller tek seferlik iner, toplam birkaç GB tutabilir.

Kurulum bitince plugin'deki **Yerel** düğmesinin yanındaki nokta yeşil yanar. Motor Mac her açıldığında kendiliğinden başlar.
Kaldırmak için: `curl -fsSL https://raw.githubusercontent.com/wabiblack/widi-key/main/engine/uninstall.sh | bash`

## Kullanım

**v0.3 yenilikleri:** gerçek nota tespiti (akort), Davul modu, döngü, velocity, glide, filtre zarfı, "istemediğin" kutusu, "benzerini üret", tarif geçmişi ve favoriler, kütüphane, Logic'e sürükle-bırak.

- Tarifi Türkçe yaz, örneğin `tozlu bir müzik kutusu, tek nota`. **Üret**'e basınca tarif arka planda İngilizceye çevrilir. Çeviri sende görünmez, model daha doğru tarif almış olur. Bulut modunda bu çeviri için de yerel motorun açık olması gerekir.
- Her üretimde 1–4 varyasyon gelir (A/B/C/D). Beğendiğine tıkla, klavyeye o yayılır.
- **Seed kilidi** açıkken her üretim yepyeni sonuçlar verir. Kilitliyken aynı sesin üzerinde kalite, süre ve tarif değiştirerek oynayabilirsin.
- **Kalite** (adım sayısı) yükseldikçe ses temizleşir ama üretim yavaşlar. **Tarife bağlılık** yükseldikçe sonuç tarife daha sıkı uyar.
- Zarf (Attack/Decay/Sustain/Release), filtre ve reverb notaya basınca anında etkiler, yeniden üretmek gerekmez.
- **Çoklu (en gerçekçisi, varsayılan):** Varyasyonlar kalından inceye farklı notalarda üretilir, her tuş kendisine en yakın kaydı çalar. Gerçek sample kütüphaneleri gibi; ses fazla gerilmediği için akustik enstrümanlar çok daha doğal duyulur. Klavyedeki yeşil noktalar kayıtların yerini gösterir.
- **Gerçekçilik:** Keman, piyano, flüt gibi akustik enstrümanlarda tarife otomatik olarak "gerçek kayıt, doğal oda, yakın mikrofon" eklenir.
- **Melodik / Davul:** Melodik modda ses gerçek notası bulunup klavyeye akortlu yayılır. Davul modunda 4 varyasyon C, D, E, F tuşlarına (her oktavda) oturur ve perdesi değişmez.
- **İstemediğin kutusu:** "davul, gürültü" gibi istemediğin şeyleri yaz.
- **Benzerini üret:** Seçili sesi temel alıp benzer yeni varyasyonlar üretir (Yerel). Ne kadar benzeyeceğini "Benzerlik" belirler.
- **Geçmiş ve favoriler:** Tarif kutusunun solundaki saat ikonu geçmişi, sağındaki yıldız favoriyi açar.
- **Kütüphane:** Sağ üstteki klasör ikonu. Sesler `~/Music/widi-key/Kütüphane` klasörüne kaydedilir.
- **Logic'e sürükle:** Dalga formunu tutup Logic'teki bir kanala sürükle, WAV olarak düşer. Tıklarsan ses çalar.
- **Döngü:** Pad gibi uzun seslerde tuş basılıyken ses bitmez.
- Pencereyi sağ alt köşesinden büyütüp küçültebilirsin.
- Bulut modu ElevenLabs API anahtarı ister ve her varyasyon ayrı kredi harcar.

Üretilen tüm varyasyonlar Logic projesiyle birlikte kaydedilir.

## Derleme

`main` dalına yapılan her push'ta GitHub Actions plugin'i bulutta bir Mac üzerinde derler ve yeni bir Release yayınlar.

Altyapı: [JUCE](https://juce.com), [Stable Audio Open Small](https://huggingface.co/stabilityai/stable-audio-open-small), [OPUS-MT tr-en](https://huggingface.co/Helsinki-NLP/opus-mt-tc-big-tr-en). Yazı tipi: Instrument Serif (SIL Open Font License).
