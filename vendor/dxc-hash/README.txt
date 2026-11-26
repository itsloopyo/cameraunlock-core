DirectX Shader Compiler v1.8.2505.1
Commit: b106a961d09221b3c5bdb37be45b679257da08b8 (2025-07-01)
Source: https://github.com/microsoft/DirectXShaderCompiler/blob/b106a961d09221b3c5bdb37be45b679257da08b8/lib/DxilHash/DxilHash.cpp
DxilHash.cpp is unmodified. dxc/WinAdapter.h supplies the two integer aliases
needed by this file on non-Windows platforms. The C++ adapter places the
upstream symbols in cameraunlock::graphics::detail.
DxilHash.cpp is Copyright (C) Microsoft Corporation under the University of
Illinois Open Source License in LICENSE.TXT. Its hash is derived from the RSA
Data Security, Inc. MD5 Message-Digest Algorithm.
