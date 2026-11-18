#!/bin/bash
# widi-key yerel motor kurulumu (Apple Silicon Mac)
# Kullanım:  curl -fsSL https://raw.githubusercontent.com/wabiblack/widi-key/main/engine/install.sh | bash
set -euo pipefail

APP_DIR="$HOME/Library/Application Support/widi-key"
ENGINE_DIR="$APP_DIR/engine"
ENV_DIR="$ENGINE_DIR/env"
LABEL="com.wabiblack.widikey.engine"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
RAW="https://raw.githubusercontent.com/wabiblack/widi-key/main/engine"

say() { printf "\n\033[1;36m==> %s\033[0m\n" "$1"; }

say "widi-key yerel motoru kuruluyor"
mkdir -p "$ENGINE_DIR" "$HOME/Library/LaunchAgents"

# 1) Python ortamı -------------------------------------------------------------
CONDA=""
for c in "$(command -v conda 2>/dev/null || true)" \
         "$HOME/miniconda3/bin/conda" "$HOME/anaconda3/bin/conda" "$HOME/miniforge3/bin/conda" \
         "$HOME/mambaforge/bin/conda" "/opt/homebrew/Caskroom/miniforge/base/bin/conda" \
         "/opt/miniconda3/bin/conda" "/opt/anaconda3/bin/conda"; do
  if [ -n "$c" ] && [ -x "$c" ]; then CONDA="$c"; break; fi
done

if [ ! -x "$ENV_DIR/bin/python" ]; then
  if [ -n "$CONDA" ]; then
    say "Python 3.10 ortamı oluşturuluyor (conda)"
    "$CONDA" create -y -q -p "$ENV_DIR" python=3.10 >/dev/null
  else
    BASE=""
    for p in python3.11 python3.10 python3; do
      if command -v "$p" >/dev/null 2>&1; then BASE="$p"; break; fi
    done
    if [ -z "$BASE" ]; then
      echo "Python bulunamadı. Önce conda (Miniforge) kur, sonra bu komutu tekrar çalıştır."
      exit 1
    fi
    say "Python ortamı oluşturuluyor ($BASE)"
    "$BASE" -m venv "$ENV_DIR"
  fi
fi
PY="$ENV_DIR/bin/python"

# 2) Kütüphaneler -------------------------------------------------------------
say "Kütüphaneler kuruluyor (birkaç dakika sürebilir)"
"$PY" -m pip install -q --upgrade pip
"$PY" -m pip install -q stable-audio-tools sentencepiece sacremoses "huggingface_hub>=0.23"
# stable-audio-tools numpy 1.x kullanıyor; PyWavelets'in yeni sürümleri numpy 2 ile derlendiği için uyumlu sürümü sabitle
"$PY" -m pip install -q "PyWavelets<1.6"
"$PY" -c "import numpy, pywt; print('numpy', numpy.__version__, '· pywt', pywt.__version__)"

# 3) Motor dosyası ------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || echo "")"
if [ -n "$SCRIPT_DIR" ] && [ -f "$SCRIPT_DIR/server.py" ]; then
  cp "$SCRIPT_DIR/server.py" "$ENGINE_DIR/server.py"
else
  curl -fsSL "$RAW/server.py" -o "$ENGINE_DIR/server.py"
fi

# 4) Hugging Face girişi ------------------------------------------------------
if ! "$PY" -c "from huggingface_hub import whoami; whoami()" >/dev/null 2>&1; then
  say "Hugging Face girişi"
  echo "Önce huggingface.co/stabilityai/stable-audio-open-small sayfasında koşulları kabul etmiş olmalısın."
  echo "Token'ı huggingface.co/settings/tokens adresinden al (Read yetkisi yeterli)."
  printf "Token'ı yapıştır ve Enter'a bas (yazdıkların görünmez): "
  read -rs HF_TOKEN < /dev/tty
  echo
  HF_TOKEN="$HF_TOKEN" "$PY" -c "import os; from huggingface_hub import login; login(token=os.environ['HF_TOKEN'])"
fi

# 5) Modelleri indir ----------------------------------------------------------
say "Modeller indiriliyor (tek seferlik, internet hızına göre sürer)"
"$PY" "$ENGINE_DIR/server.py" --download

# 6) Arka planda otomatik başlat ---------------------------------------------
say "Motor arka planda başlatılıyor"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PY</string>
    <string>$ENGINE_DIR/server.py</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict><key>PYTORCH_ENABLE_MPS_FALLBACK</key><string>1</string></dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$APP_DIR/engine.log</string>
  <key>StandardErrorPath</key><string>$APP_DIR/engine.log</string>
</dict>
</plist>
EOF

launchctl bootout "gui/$(id -u)/$LABEL" >/dev/null 2>&1 || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"

for _ in $(seq 1 30); do
  if curl -fs "http://127.0.0.1:7865/health" >/dev/null 2>&1; then
    say "Kurulum tamam! widi-key'de 'Yerel' modu artık yeşil yanacak."
    exit 0
  fi
  sleep 1
done

echo "Motor başlatıldı ama henüz cevap vermiyor. Birkaç saniye sonra plugin'de Yerel'e bak."
echo "Sorun olursa kayıt dosyası: $APP_DIR/engine.log"
