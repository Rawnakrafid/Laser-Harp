import os
import subprocess
import urllib.request
import numpy as np

BASE_URL = "https://raw.githubusercontent.com/nbrosowsky/tonejs-instruments/master/samples/"
CACHE_DIR = "dl_samples"
os.makedirs(CACHE_DIR, exist_ok=True)

NOTES = ['C4', 'D4', 'E4', 'F4', 'G4', 'A4', 'B4']
DUR_SAMPLES = 12800  # 1.6s at 8000 Hz

# Source mapping for each instrument: (folder, source_note, semitone_shift)
SPECS = {
    'guitar_acoustic': {
        'folder': 'guitar-acoustic',
        'notes': {
            'C4': ('C4.mp3', 0),
            'D4': ('D4.mp3', 0),
            'E4': ('E4.mp3', 0),
            'F4': ('F4.mp3', 0),
            'G4': ('G4.mp3', 0),
            'A4': ('A4.mp3', 0),
            'B4': ('B4.mp3', 0),
        }
    },
    'guitar_electric': {
        'folder': 'guitar-electric',
        'notes': {
            'C4': ('C4.mp3', 0),
            'D4': ('Ds4.mp3', -1),
            'E4': ('Ds4.mp3', 1),
            'F4': ('Fs4.mp3', -1),
            'G4': ('Fs4.mp3', 1),
            'A4': ('A4.mp3', 0),
            'B4': ('C5.mp3', -1),
        }
    },
    'harmonium': {
        'folder': 'harmonium',
        'notes': {
            'C4': ('C4.mp3', 0),
            'D4': ('D4.mp3', 0),
            'E4': ('E4.mp3', 0),
            'F4': ('F4.mp3', 0),
            'G4': ('G4.mp3', 0),
            'A4': ('A4.mp3', 0),
            'B4': ('B4.mp3', 0),
        }
    },
    'saxophone': {
        'folder': 'saxophone',
        'notes': {
            'C4': ('C4.mp3', 0),
            'D4': ('D4.mp3', 0),
            'E4': ('E4.mp3', 0),
            'F4': ('F4.mp3', 0),
            'G4': ('G4.mp3', 0),
            'A4': ('A4.mp3', 0),
            'B4': ('B4.mp3', 0),
        }
    },
    'violin': {
        'folder': 'violin',
        'notes': {
            'C4': ('C4.mp3', 0),
            'D4': ('C4.mp3', 2),
            'E4': ('E4.mp3', 0),
            'F4': ('E4.mp3', 1),
            'G4': ('G4.mp3', 0),
            'A4': ('A4.mp3', 0),
            'B4': ('A4.mp3', 2),
        }
    }
}

def download_file(folder, filename):
    local_path = os.path.join(CACHE_DIR, f"{folder}_{filename}")
    if not os.path.exists(local_path):
        url = f"{BASE_URL}{folder}/{filename}"
        print(f"Downloading {url}...")
        urllib.request.urlretrieve(url, local_path)
    return local_path

def process_audio(mp3_path, shift_semitones):
    # Convert and pitch shift to 8000 Hz raw PCM
    raw_path = mp3_path + f"_shift_{shift_semitones}.raw"
    if not os.path.exists(raw_path):
        filter_str = f"asetrate=44100*2^({shift_semitones}/12),aresample=8000" if shift_semitones != 0 else "aresample=8000"
        cmd = [
            'ffmpeg', '-y', '-i', mp3_path,
            '-af', filter_str,
            '-ac', '1',
            '-f', 'u8',
            '-acodec', 'pcm_u8',
            raw_path
        ]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    data = np.fromfile(raw_path, dtype=np.uint8)
    
    # 1. Detect start of sound (trim lead-in silence for instant 0ms attack)
    thresh = 4
    start_idx = 0
    for i, v in enumerate(data):
        if abs(int(v) - 128) > thresh:
            start_idx = max(0, i - 16) # keep 2ms buffer
            break
            
    trimmed = data[start_idx:start_idx + DUR_SAMPLES]
    # Pad with 128 if shorter than DUR_SAMPLES
    if len(trimmed) < DUR_SAMPLES:
        pad = np.full(DUR_SAMPLES - len(trimmed), 128, dtype=np.uint8)
        trimmed = np.concatenate([trimmed, pad])
        
    # 2. Normalize volume: centered at 128, max amplitude = 115 -> range [13..243]
    dev = trimmed.astype(np.float32) - 128.0
    peak = np.max(np.abs(dev))
    if peak > 1.0:
        dev = (dev / peak) * 115.0
    
    # 3. Apply smooth fade out to last 400 samples (50ms) to prevent clicks
    fade_len = 400
    fade_curve = np.linspace(1.0, 0.0, fade_len)
    dev[-fade_len:] *= fade_curve
    
    out_u8 = np.clip(128.0 + dev, 0, 255).astype(np.uint8)
    return out_u8

print("Processing all instrument samples...")
all_instruments = {}

for inst_key, spec in SPECS.items():
    print(f"\n--- Building {inst_key} ---")
    all_instruments[inst_key] = {}
    folder = spec['folder']
    for note in NOTES:
        src_file, shift = spec['notes'][note]
        mp3 = download_file(folder, src_file)
        arr = process_audio(mp3, shift)
        all_instruments[inst_key][note] = arr
        print(f"  {note}: {len(arr)} samples, min {arr.min()}, max {arr.max()}")

output_header = "esp32-fw/src/instruments_data.h"
print(f"\nWriting {output_header}...")

with open(output_header, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("#include <Arduino.h>\n")
    f.write("#include \"notes_data.h\"\n\n")
    f.write("// Studio-quality real instrument recordings for Laser Harp\n")
    f.write("// 8000 Hz, 8-bit unsigned PCM, 128 = silence\n\n")
    
    for inst_key in SPECS.keys():
        f.write(f"// ==================== {inst_key.upper()} ====================\n")
        for note in NOTES:
            arr = all_instruments[inst_key][note]
            var_name = f"{inst_key}_{note}"
            f.write(f"const uint32_t {var_name}_len = {len(arr)}u;\n")
            f.write(f"const uint8_t {var_name}[] PROGMEM = {{\n")
            for i in range(0, len(arr), 24):
                chunk = arr[i:i+24]
                f.write("  " + ",".join(map(str, chunk)) + ",\n")
            f.write("};\n\n")
            
        f.write(f"const NoteSample {inst_key.upper()}_TABLE[7] = {{\n")
        for note in NOTES:
            var_name = f"{inst_key}_{note}"
            f.write(f"  {{ {var_name}, {var_name}_len }},\n")
        f.write("};\n\n")

print(f"Done! {output_header} generated successfully.")
