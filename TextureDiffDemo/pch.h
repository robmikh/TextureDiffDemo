#pragma once

// Windows
#include <windows.h>
#include <unknwn.h>

// WinRT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3d11.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>

// WIL
#include <wil/resource.h>

// DirectX
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>
#include <wincodec.h>

// STL
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <algorithm>
#include <mutex>
#include <cwctype>

// robmikh.common
#include <robmikh.common/composition.interop.h>
#include <robmikh.common/direct3d11.interop.h>
#include <robmikh.common/d3dHelpers.h>
#include <robmikh.common/d3dHelpers.desktop.h>
#include <robmikh.common/imaging.h>
#include <robmikh.common/storage.desktop.h>
