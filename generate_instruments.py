import numpy as np
from scipy import signal
import os

sr = 8000
freqs = [261.626, 293.665, 329.628, 349.228, 391.995, 440.000, 493.883]
names = ['C4', 'D4', 'E4', 'F4', 'G4', 'A4', 'B4']

def generate_guitar(f0, dur=1.8):
    N = int(dur * sr)
    L = int(round(sr / f0))
    # Hann-windowed pick excitation
    window = np.sin(np.pi * np.linspace(0, 1, L))
    noise = np.random.uniform(-1, 1, L) * window
    buf = np.zeros(N)
    buf[:L] = noise
    
    # Higher notes decay slightly faster
    decay = 0.9965 - (f0 - 260) / 240 * 0.003
    for i in range(L, N):
        buf[i] = 0.5 * (buf[i - L] + buf[i - L - 1]) * decay

    # Acoustic body resonance: 2nd-order IIR bandpass around 200 Hz
    b, a = signal.iirpeak(200, 2.5, fs=sr)
    body = signal.lfilter(b, a, buf)
    out = buf + 0.35 * body
    
    # Smooth fade out at the very end (last 50ms) to prevent any click
    fade_len = int(0.05 * sr)
    out[-fade_len:] *= np.linspace(1, 0, fade_len)
    
    # Normalize to 8-bit unsigned PCM
    peak = np.max(np.abs(out)) + 1e-6
    u8 = np.clip(128.0 + (out / peak) * 122.0, 0, 255).astype(np.uint8)
    return u8

def generate_harmonium(f0, dur=1.8):
    N = int(dur * sr)
    t = np.linspace(0, dur, N, endpoint=False)
    
    # Free-reed Indian harmonium: dual reeds (male + female / samvad detuned by ~1.2 Hz)
    detune = 1.0 + (1.2 / f0)
    # Odd & even harmonics typical of free brass reeds
    harmonics = [
        (1, 1.00),
        (2, 0.68),
        (3, 0.48),
        (4, 0.35),
        (5, 0.22),
        (6, 0.15),
        (7, 0.09),
        (8, 0.05)
    ]
    
    sig = np.zeros(N)
    for h, amp in harmonics:
        # Reed 1
        sig += amp * np.sin(2 * np.pi * (f0 * h) * t)
        # Reed 2 (detuned chorus)
        sig += (amp * 0.75) * np.sin(2 * np.pi * (f0 * detune * h) * t + 0.4 * h)
    
    # Subtle bellows pressure pulsation (5 Hz tremolo, 2% depth)
    bellows = 1.0 + 0.02 * np.sin(2 * np.pi * 5.0 * t)
    sig *= bellows
    
    # Attack envelope: quick 20ms smooth attack
    att_len = int(0.02 * sr)
    env = np.ones(N)
    env[:att_len] = np.sin(np.pi / 2 * np.linspace(0, 1, att_len))
    # Natural exponential decay
    env *= np.exp(-t / 1.05)
    
    # Last 50ms fade
    fade_len = int(0.05 * sr)
    env[-fade_len:] *= np.linspace(1, 0, fade_len)
    
    out = sig * env
    peak = np.max(np.abs(out)) + 1e-6
    u8 = np.clip(128.0 + (out / peak) * 120.0, 0, 255).astype(np.uint8)
    return u8

def generate_metallic(f0, dur=1.5):
    N = int(dur * sr)
    t = np.linspace(0, dur, N, endpoint=False)
    
    # Tokyo Drift metallic synth bell / FM strike
    # Inharmonic metallic ratios
    partials = [
        (1.000, 1.00, 1.3),   # fundamental
        (1.414, 0.85, 2.2),   # tritone metallic
        (2.000, 0.60, 2.5),   # 2nd harmonic
        (2.756, 0.70, 3.2),   # bell chime
        (3.414, 0.40, 4.0),   # upper ring
        (4.180, 0.30, 4.8),   # high shimmer
    ]
    
    sig = np.zeros(N)
    for ratio, amp, decay_rate in partials:
        sig += amp * np.sin(2 * np.pi * (f0 * ratio) * t) * np.exp(-decay_rate * t)
    
    # Sharp metallic transient strike at t=0 (first 12ms)
    trans_len = int(0.012 * sr)
    trans = np.sin(2 * np.pi * 3200 * t[:trans_len]) * np.linspace(1, 0, trans_len)
    sig[:trans_len] += 0.5 * trans
    
    # Last 50ms fade
    fade_len = int(0.05 * sr)
    sig[-fade_len:] *= np.linspace(1, 0, fade_len)
    
    peak = np.max(np.abs(sig)) + 1e-6
    u8 = np.clip(128.0 + (sig / peak) * 122.0, 0, 255).astype(np.uint8)
    return u8

print("Synthesizing instruments...")
g_samples = [generate_guitar(f) for f in freqs]
h_samples = [generate_harmonium(f) for f in freqs]
m_samples = [generate_metallic(f) for f in freqs]

output_path = "esp32-fw/src/instruments_data.h"
print(f"Writing {output_path}...")

with open(output_path, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("#include <Arduino.h>\n")
    f.write("#include \"notes_data.h\"\n\n")
    f.write("// Auto-generated high-fidelity instrument samples for Laser Harp\n")
    f.write("// 8000 Hz, 8-bit unsigned PCM, 128 = silence\n\n")
    
    # 1. Guitar samples
    f.write("// ==================== GUITAR (C Major) ====================\n")
    for name, arr in zip(names, g_samples):
        f.write(f"const uint32_t guitar{name}Len = {len(arr)}u;\n")
        f.write(f"const uint8_t guitar{name}[] PROGMEM = {{\n")
        # Write 24 numbers per line
        for i in range(0, len(arr), 24):
            chunk = arr[i:i+24]
            f.write("  " + ",".join(map(str, chunk)) + ",\n")
        f.write("};\n\n")
        
    # 2. Harmonium samples
    f.write("// ==================== HARMONIUM (C Major) ====================\n")
    for name, arr in zip(names, h_samples):
        f.write(f"const uint32_t harmonium{name}Len = {len(arr)}u;\n")
        f.write(f"const uint8_t harmonium{name}[] PROGMEM = {{\n")
        for i in range(0, len(arr), 24):
            chunk = arr[i:i+24]
            f.write("  " + ",".join(map(str, chunk)) + ",\n")
        f.write("};\n\n")

    # 3. Metallic samples
    f.write("// ==================== METALLIC (Tokyo Drift Bell, C Major) ====================\n")
    for name, arr in zip(names, m_samples):
        f.write(f"const uint32_t metallic{name}Len = {len(arr)}u;\n")
        f.write(f"const uint8_t metallic{name}[] PROGMEM = {{\n")
        for i in range(0, len(arr), 24):
            chunk = arr[i:i+24]
            f.write("  " + ",".join(map(str, chunk)) + ",\n")
        f.write("};\n\n")

    # Table definitions
    f.write("// ==================== INSTRUMENT LOOKUP TABLES ====================\n")
    
    f.write("const NoteSample GUITAR_TABLE[7] = {\n")
    for name in names:
        f.write(f"  {{ guitar{name}, guitar{name}Len }},\n")
    f.write("};\n\n")
    
    f.write("const NoteSample HARMONIUM_TABLE[7] = {\n")
    for name in names:
        f.write(f"  {{ harmonium{name}, harmonium{name}Len }},\n")
    f.write("};\n\n")

    f.write("const NoteSample METALLIC_TABLE[7] = {\n")
    for name in names:
        f.write(f"  {{ metallic{name}, metallic{name}Len }},\n")
    f.write("};\n\n")

print("instruments_data.h generated successfully!")
