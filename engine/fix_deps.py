"""stable-audio-tools'un eksik bıraktığı bağımlılıkları bulup kurar.

Modelin ihtiyaç duyduğu modülleri tek tek içe aktarmayı dener; eksik bir
modül çıkarsa onu pip ile kurar ve tekrar dener.
"""

import importlib
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server  # noqa: E402  (pywt yamasını uygular)

MODULES = [
    "stable_audio_tools",
    "stable_audio_tools.models.factory",
    "stable_audio_tools.models.diffusion",
    "stable_audio_tools.models.autoencoders",
    "stable_audio_tools.models.conditioners",
    "stable_audio_tools.inference.generation",
    "einops",
    "transformers",
    "sentencepiece",
]

PIP_NAMES = {
    "pytorch_lightning": "pytorch_lightning>=2.1,<3",
    "lightning": "lightning>=2.1,<3",
    "k_diffusion": "k-diffusion",
    "einops_exts": "einops-exts",
    "alias_free_torch": "alias-free-torch",
    "ema_pytorch": "ema-pytorch",
    "vector_quantize_pytorch": "vector-quantize-pytorch",
    "x_transformers": "x-transformers",
    "local_attention": "local-attention",
    "dac": "descript-audio-codec",
    "laion_clap": "laion-clap",
    "sklearn": "scikit-learn",
    "yaml": "pyyaml",
    "PIL": "pillow",
    "cv2": "opencv-python-headless",
}

SKIP = {"flash_attn"}


def try_imports():
    for name in MODULES:
        for key in [m for m in sys.modules if m == name or m.startswith(name + ".")]:
            if key.startswith("stable_audio_tools"):
                del sys.modules[key]
        importlib.invalidate_caches()
        try:
            importlib.import_module(name)
        except ModuleNotFoundError as exc:
            missing = (exc.name or "").split(".")[0]
            if missing and missing not in SKIP:
                return missing
            raise
    return None


def main():
    installed = set()
    for _ in range(15):
        missing = try_imports()
        if missing is None:
            print("Bağımlılıklar tamam.", flush=True)
            return 0
        if missing in installed:
            print(f"{missing} kuruldu ama hâlâ yüklenemiyor.", flush=True)
            return 1
        package = PIP_NAMES.get(missing, missing.replace("_", "-"))
        print(f"Eksik paket kuruluyor: {package}", flush=True)
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", package])
        installed.add(missing)
    return 1


if __name__ == "__main__":
    sys.exit(main())
