# Third-party notices

## Shipped inside winmm.dll

### MinHook 1.3.4

<https://github.com/TsudaKageyu/minhook>. Used to install the mod's one per-frame hook.
Source is vendored in `third_party/minhook`.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

MinHook includes the Hacker Disassembler Engine 64 C, Copyright (c) 2008-2009,
Vyacheslav Patkov, under the same two-clause BSD terms. The full text is in
`third_party/minhook/LICENSE.txt`.

### llvm-mingw runtime libraries

<https://github.com/mstorsjo/llvm-mingw>. The compiler's runtime (mingw-w64, compiler-rt,
libc++, libunwind) is linked statically into `winmm.dll`. mingw-w64 runtime: Zope Public
License 2.1 and public domain parts. compiler-rt, libc++, libunwind: Apache License 2.0
with LLVM exceptions.

## Research the mod builds on

- **norfmare and contributors, Miles Morales First-Person Camera**,
  <https://github.com/norfmare/miles-morales-first-person-camera>, MIT. Its documented
  actor layout, handle resolver, and the look and body-group functions for this exact
  game build were the starting point for this project's research. No source from it is
  included; the facts it documents are recorded as rows in `sheets/`.

## Development tools, not shipped

- **rehan-remade/universal-modder**, <https://github.com/rehan-remade/universal-modder>,
  MIT. Recon, backups, input automation and capture.
- **Frida**, <https://frida.re/>. Live probing of the running game during research.
- **Ghidra**, National Security Agency and contributors,
  <https://github.com/NationalSecurityAgency/ghidra>, Apache-2.0, with Eclipse Temurin.
- **pefile**, **Capstone**, **NumPy**, **Pillow**. Static inspection and image handling.
- **Tkachov, Overstrike and ALERT**, <https://github.com/Tkachov/Overstrike>,
  <https://github.com/Tkachov/ALERT>, GPL-3.0. Used as documentation of the asset
  formats; no code from them is included.
- **Claude** (Anthropic) and, in the first research session, **Codex**. Design, original
  code and testing.

## What is not here

No game content, extracted model, texture, animation or sound, no decompiled game code,
and no part of any other multiplayer mod is included in this project or its releases.
The mod reads what it needs from each player's own installed copy of the game while it
runs.
