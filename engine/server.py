#!/usr/bin/env python3
"""widi-key yerel motor.

Mac üzerinde arka planda çalışır ve plugin'e iki hizmet verir:
  * /translate : Türkçe tarifi İngilizceye çevirir (kullanıcı görmez)
  * /generate  : Stable Audio Open Small ile tarife göre ses üretir

Sadece 127.0.0.1 üzerinden dinler; dışarıdan erişilemez.
"""

import base64
import io
import json
import os
import re
import sys
import threading
import traceback
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")

PORT = int(os.environ.get("WIDIKEY_PORT", "7865"))
AUDIO_MODEL = "stabilityai/stable-audio-open-small"
TRANSLATE_MODEL = "Helsinki-NLP/opus-mt-tc-big-tr-en"
MAX_SECONDS = 11.0

def _patch_pywt():
    """PyWavelets numpy sürümüyle uyumsuz derlenmiş olabilir. Bu model dalgacık
    dönüşümü kullanmadığı için, içe aktarılamıyorsa yerine boş bir modül koy."""
    try:
        import pywt  # noqa: F401
    except Exception:  # noqa: BLE001
        import types

        for name in [m for m in sys.modules if m == "pywt" or m.startswith("pywt.")]:
            del sys.modules[name]

        def _unavailable(*_args, **_kwargs):
            raise RuntimeError("PyWavelets bu ortamda kullanılamıyor")

        def _stub_getattr(name):
            if name.startswith("__"):
                raise AttributeError(name)
            return _unavailable

        stub = types.ModuleType("pywt")
        stub.__file__ = __file__
        stub.__getattr__ = _stub_getattr
        sys.modules["pywt"] = stub


_patch_pywt()

_work_lock = threading.Lock()
_load_lock = threading.Lock()
_state = {"audio": None, "translator": None, "device": None, "error": None}


# --------------------------------------------------------------------------- #
# Modeller
# --------------------------------------------------------------------------- #
def _device():
    import torch

    return "mps" if torch.backends.mps.is_available() else "cpu"


_mps_patched = False


def _patch_mps_float64():
    """MPS float64 desteklemiyor. Tensor.to() içinde float64 hatası çıkarsa
    tensörü float32'ye çevirip tekrar dene."""
    global _mps_patched
    if _mps_patched:
        return
    import torch

    original_to = torch.Tensor.to

    def safe_to(self, *args, **kwargs):
        try:
            return original_to(self, *args, **kwargs)
        except (TypeError, RuntimeError) as exc:
            if "float64" not in str(exc):
                raise
            args = tuple(torch.float32 if a is torch.float64 else a for a in args)
            if kwargs.get("dtype") is torch.float64:
                kwargs["dtype"] = torch.float32
            source = self.float() if self.dtype == torch.float64 else self
            return original_to(source, *args, **kwargs)

    torch.Tensor.to = safe_to

    # stable-audio-tools'un APG hesabı .double() çağırıyor; MPS'te float32 kullan
    original_double = torch.Tensor.double

    def safe_double(self, *args, **kwargs):
        if self.device.type == "mps":
            return self.float()
        return original_double(self, *args, **kwargs)

    torch.Tensor.double = safe_double
    _mps_patched = True


def load_audio():
    with _load_lock:
        if _state["audio"] is None:
            from stable_audio_tools import get_pretrained_model

            _patch_mps_float64()
            model, config = get_pretrained_model(AUDIO_MODEL)
            dev = _device()
            model = model.to(dev).eval()
            _state["audio"] = (model, config)
            _state["device"] = dev
    return _state["audio"]


def load_translator():
    with _load_lock:
        if _state["translator"] is None:
            from transformers import MarianMTModel, MarianTokenizer

            tokenizer = MarianTokenizer.from_pretrained(TRANSLATE_MODEL)
            model = MarianMTModel.from_pretrained(TRANSLATE_MODEL).eval()
            _state["translator"] = (tokenizer, model)
    return _state["translator"]


# --------------------------------------------------------------------------- #
# Çeviri
# --------------------------------------------------------------------------- #
_TR_CHARS = re.compile(r"[çğıöşüÇĞİÖŞÜ]")
_TR_WORDS = {
    "bir", "ve", "ile", "gibi", "ses", "sesi", "nota", "tek", "sicak", "eski", "tozlu",
    "yumusak", "sert", "derin", "parlak", "karanlik", "uzun", "kisa", "davul", "piyano",
    "gitar", "bas", "can", "ruzgar", "su", "yagmur", "kutusu", "muzik", "icin", "cok",
    "az", "hafif", "agir", "metal", "cam", "tahta", "zil", "dalga", "yankili", "boguk",
    "tiz", "kalin", "ince", "vurus", "efekt", "korkunc", "neseli", "huzunlu", "uzay",
}


def looks_turkish(text):
    if _TR_CHARS.search(text):
        return True
    words = re.findall(r"[a-z]+", text.lower())
    return any(w in _TR_WORDS for w in words)


def translate(text):
    text = (text or "").strip()
    if not text or not looks_turkish(text):
        return text

    import torch

    tokenizer, model = load_translator()
    with torch.no_grad():
        batch = tokenizer([text], return_tensors="pt", padding=True, truncation=True, max_length=256)
        output = model.generate(**batch, num_beams=5, max_new_tokens=128)
    return tokenizer.decode(output[0], skip_special_tokens=True).strip()


# --------------------------------------------------------------------------- #
# Ses üretimi
# --------------------------------------------------------------------------- #
def _trim_and_normalize(audio, sample_rate):
    import torch

    peak = audio.abs().max().item()
    if peak > 0:
        audio = audio / peak * 0.95

    # Baştaki sessizliği kırp ki tuşa basınca ses hemen gelsin
    envelope = audio.abs().max(dim=0).values
    loud = (envelope > 0.02).nonzero()
    if len(loud):
        start = max(0, int(loud[0]) - int(0.003 * sample_rate))
        audio = audio[:, start:]

    # Sonda tık sesi olmasın diye kısa bir fade
    fade = min(int(0.02 * sample_rate), audio.shape[1])
    if fade > 0:
        audio[:, -fade:] *= torch.linspace(1.0, 0.0, fade)
    return audio


def _to_wav(audio, sample_rate):
    import torch

    pcm = (audio.clamp(-1, 1) * 32767).to(torch.int16).t().contiguous().numpy().tobytes()
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as w:
        w.setnchannels(audio.shape[0])
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm)
    return buffer.getvalue()


def _from_wav(data):
    """WAV baytlarını (kanal, örnek) float32 tensörüne çevirir."""
    import numpy as np
    import torch

    with wave.open(io.BytesIO(data), "rb") as w:
        channels = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        frames = w.readframes(w.getnframes())

    if width == 2:
        pcm = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
    elif width == 4:
        pcm = np.frombuffer(frames, dtype=np.int32).astype(np.float32) / 2147483648.0
    else:
        raise ValueError("Desteklenmeyen WAV biçimi")

    audio = torch.from_numpy(pcm.reshape(-1, channels).T.copy())
    return rate, audio


def generate(request):
    import torch
    from einops import rearrange
    from stable_audio_tools.inference.generation import generate_diffusion_cond

    model, config = load_audio()
    sample_rate = config["sample_rate"]
    sample_size = config["sample_size"]

    seconds = max(0.5, min(MAX_SECONDS, float(request.get("seconds", 3.0))))
    steps = max(4, min(50, int(request.get("steps", 8))))
    cfg_scale = max(0.5, min(7.0, float(request.get("cfg", 1.0))))
    seeds = [int(s) % 4294967000 for s in request.get("seeds", [])][:4] or [12345]

    prompt_en = translate(request.get("prompt", ""))
    full_prompt = prompt_en + request.get("suffix", "")

    # "İstemediğin" kutusu: negatif tarif yalnızca CFG > 1 iken etkili
    negative_en = translate(request.get("negative", ""))
    if negative_en and cfg_scale < 2.0:
        cfg_scale = 2.0

    # "Buna benzer üret": seçili sesi başlangıç olarak kullan
    init_audio = None
    if request.get("init_audio"):
        try:
            init_audio = _from_wav(base64.b64decode(request["init_audio"]))
        except Exception as exc:  # noqa: BLE001
            print(f"Başlangıç sesi okunamadı, atlanıyor: {exc}", flush=True)
    similarity = max(0.0, min(1.0, float(request.get("similarity", 0.5))))
    init_noise_level = 1.0 - 0.75 * similarity

    def run(current_model, seed):
        kwargs = dict(
            steps=steps,
            cfg_scale=cfg_scale,
            conditioning=[{"prompt": full_prompt, "seconds_total": seconds}],
            sample_size=sample_size,
            sampler_type="pingpong",
            device=_state["device"],
            seed=seed,
        )
        if negative_en:
            kwargs["negative_conditioning"] = [{"prompt": negative_en, "seconds_total": seconds}]
        if init_audio is not None:
            kwargs["init_audio"] = init_audio
            kwargs["init_noise_level"] = init_noise_level

        with torch.no_grad():
            try:
                return generate_diffusion_cond(current_model, **kwargs)
            except TypeError as exc:
                # Eski bir stable-audio-tools sürümü bu seçenekleri tanımıyorsa onlarsız dene
                if "unexpected keyword" not in str(exc):
                    raise
                for key in ("negative_conditioning", "init_audio", "init_noise_level"):
                    kwargs.pop(key, None)
                return generate_diffusion_cond(current_model, **kwargs)

    clips = []
    for seed in seeds:
        try:
            output = run(model, seed)
        except (TypeError, RuntimeError) as exc:
            if _state["device"] != "mps":
                raise
            print(f"MPS hatası, CPU'ya geçiliyor: {exc}", flush=True)
            with _load_lock:
                model = model.to("cpu")
                _state["audio"] = (model, config)
                _state["device"] = "cpu"
            output = run(model, seed)

        audio = rearrange(output, "b d n -> d (b n)").to(torch.float32).cpu()
        audio = audio[:, : int(seconds * sample_rate)]
        audio = _trim_and_normalize(audio, sample_rate)
        clips.append(base64.b64encode(_to_wav(audio, sample_rate)).decode("ascii"))

    return {"audio": clips, "seeds": seeds, "prompt_en": prompt_en, "device": _state["device"]}


# --------------------------------------------------------------------------- #
# HTTP
# --------------------------------------------------------------------------- #
class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, payload):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith("/health"):
            self._send(200, {
                "ok": True,
                "device": _state["device"],
                "audio_loaded": _state["audio"] is not None,
                "translator_loaded": _state["translator"] is not None,
                "error": _state["error"],
            })
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length) or b"{}")

            if self.path.startswith("/translate"):
                with _work_lock:
                    self._send(200, {"text": translate(request.get("text", ""))})
            elif self.path.startswith("/generate"):
                with _work_lock:
                    self._send(200, generate(request))
            else:
                self._send(404, {"error": "not found"})
        except Exception as exc:  # noqa: BLE001
            traceback.print_exc()
            self._send(500, {"error": str(exc)})


def _warm_up():
    try:
        load_translator()
        load_audio()
        print(f"widi-key motoru hazır ({_state['device']})", flush=True)
    except Exception as exc:  # noqa: BLE001
        _state["error"] = str(exc)
        traceback.print_exc()


def main():
    if "--download" in sys.argv:
        print("Modeller indiriliyor (tek seferlik)...", flush=True)
        load_translator()
        print("  çeviri modeli tamam", flush=True)
        load_audio()
        print("  ses modeli tamam", flush=True)
        print("Test çevirisi:", translate("tozlu bir müzik kutusu, tek nota"), flush=True)
        return

    threading.Thread(target=_warm_up, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"widi-key motoru 127.0.0.1:{PORT} adresinde dinliyor", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
