# Checking Eglantine's helper-load patcher against ptcap

`nx2_helper_loads.cpp` (fork, patches/0016) ports ptcap's `PT_HELPER_LOADS` without a disassembler. To check it
against ptcap + nvdisasm on a capture's pixel programs (nvdisasm on PATH):

    python3 -I ref.py <capture dir> <out dir>          # images (<va>.img), ptcap's patch (<va>.py.out), nvdisasm text
    g++ -std=c++20 -O1 -I <eglantine>/src -o harness harness.cpp <eglantine>/src/video_core/nx2_helper_loads.cpp
    ./harness <out dir>                                 # Eglantine's patch (<va>.cpp.out)

Then compare per program: the same patched slots and load count, the predicate Eglantine chose is one nvdisasm
never shows (`<va>.free`), and the bytes are identical when both chose the same one. mkw21 (eleventh pass): 210 of
210 programs with loads byte-identical (2745 loads), 105 without loads, the same 5 without an end in 32 KiB.
