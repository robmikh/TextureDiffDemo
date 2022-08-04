#include "pch.h"
#include "TexturePixelDiffShader.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
}

namespace util
{
    using namespace robmikh::common::uwp;
    using namespace robmikh::common::desktop;
}

struct DiffInfo
{
    uint32_t NumDifferingColorPixels;
    uint32_t NumDifferingAlphaPixels;
    uint32_t Width;
    uint32_t Height;
};

std::future<winrt::com_ptr<ID3D11Texture2D>> LoadTextureFromPathAsync(winrt::com_ptr<ID3D11Device> device, std::wstring const& path);
template <typename T>
T ReadFromBuffer(
    winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
    winrt::com_ptr<ID3D11Buffer> const& stagingBuffer);
std::future<std::wstring> SaveTextureToPathAsync(winrt::com_ptr<ID3D11Texture2D> texture, std::wstring const& path);

int __stdcall wmain(int argc, wchar_t* argv[])
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // CLI
    std::vector<std::wstring> args(argv + 1, argv + argc);
    if (args.size() != 2)
    {
        wprintf(L"Expected two args: <image path1> <image path2>\n");
        return 1;
    }
    auto inputPath1 = args[0];
    auto inputPath2 = args[1];

    // Initialize D3D
    uint32_t flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    auto d3dDevice = util::CreateD3DDevice(flags);
    winrt::com_ptr<ID3D11DeviceContext> d3dContext;
    d3dDevice->GetImmediateContext(d3dContext.put());

    // Load the input images
    auto inputTexture1 = LoadTextureFromPathAsync(d3dDevice, inputPath1).get();
    auto inputTexture2 = LoadTextureFromPathAsync(d3dDevice, inputPath2).get();

    // Make sure the input textures are the same size
    D3D11_TEXTURE2D_DESC desc1 = {};
    inputTexture1->GetDesc(&desc1);
    D3D11_TEXTURE2D_DESC desc2 = {};
    inputTexture1->GetDesc(&desc2);
    if (desc1.Width != desc2.Width || desc1.Height != desc2.Height)
    {
        wprintf(L"Input images must be the same size! %ix%i vs %ix%i\n", desc1.Width, desc1.Height, desc2.Width, desc2.Height);
        return 1;
    }

    // Create our shader resource views
    winrt::com_ptr<ID3D11ShaderResourceView> inputView1;
    winrt::com_ptr<ID3D11ShaderResourceView> inputView2;
    winrt::check_hresult(d3dDevice->CreateShaderResourceView(inputTexture1.get(), nullptr, inputView1.put()));
    winrt::check_hresult(d3dDevice->CreateShaderResourceView(inputTexture2.get(), nullptr, inputView2.put()));

    // Create our output textures
    desc1.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    winrt::com_ptr<ID3D11Texture2D> colorChannelDiffTexture;
    winrt::com_ptr<ID3D11Texture2D> alphaChannelDiffTexture;
    winrt::check_hresult(d3dDevice->CreateTexture2D(&desc1, nullptr, colorChannelDiffTexture.put()));
    winrt::check_hresult(d3dDevice->CreateTexture2D(&desc1, nullptr, alphaChannelDiffTexture.put()));
    D3D11_UNORDERED_ACCESS_VIEW_DESC textureUav = {};
    textureUav.Format = desc1.Format;
    textureUav.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    textureUav.Texture2D.MipSlice = 0;
    winrt::com_ptr<ID3D11UnorderedAccessView> colorChannelView;
    winrt::com_ptr<ID3D11UnorderedAccessView> alphaChannelView;
    winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(colorChannelDiffTexture.get(), &textureUav, colorChannelView.put()));
    winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(alphaChannelDiffTexture.get(), &textureUav, alphaChannelView.put()));

    // Create the structured buffer that will tell us the number of differing pixels
    auto diffBufferSize = static_cast<uint32_t>(sizeof(DiffInfo));
    D3D11_BUFFER_DESC diffBufferDesc = {};
    diffBufferDesc.ByteWidth = diffBufferSize;
    diffBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    diffBufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    diffBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    diffBufferDesc.StructureByteStride = diffBufferSize;
    DiffInfo initialResult = {};
    initialResult.NumDifferingColorPixels = 0;
    initialResult.NumDifferingAlphaPixels = 0;
    initialResult.Width = desc1.Width;
    initialResult.Height = desc1.Height;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = reinterpret_cast<void*>(&initialResult);
    winrt::com_ptr<ID3D11Buffer> diffResultBuffer;
    winrt::check_hresult(d3dDevice->CreateBuffer(&diffBufferDesc, &initData, diffResultBuffer.put()));
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDiff = {};
    uavDiff.Format = DXGI_FORMAT_UNKNOWN;
    uavDiff.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDiff.Buffer.NumElements = 1;
    winrt::com_ptr<ID3D11UnorderedAccessView> diffResulView;
    winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(diffResultBuffer.get(), &uavDiff, diffResulView.put()));

    // Create a staging texture so we can read back the diff info
    D3D11_BUFFER_DESC diffStagingBufferDesc = {};
    diffStagingBufferDesc.ByteWidth = diffBufferSize;
    diffStagingBufferDesc.Usage = D3D11_USAGE_STAGING;
    diffStagingBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    winrt::com_ptr<ID3D11Buffer> diffStagingBuffer;
    winrt::check_hresult(d3dDevice->CreateBuffer(&diffStagingBufferDesc, nullptr, diffStagingBuffer.put()));

    // Setup our compute pipeline
    winrt::com_ptr<ID3D11ComputeShader> shader;
    winrt::check_hresult(d3dDevice->CreateComputeShader(g_main, ARRAYSIZE(g_main), nullptr, shader.put()));

    std::array<ID3D11UnorderedAccessView*, 3> uavs = { diffResulView.get(), colorChannelView.get(), alphaChannelView.get() };
    d3dContext->CSSetShader(shader.get(), nullptr, 0);
    d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

    std::array<ID3D11ShaderResourceView*, 2> srvs = { inputView1.get(), inputView2.get() };
    d3dContext->CSSetShaderResources(0, 2, srvs.data());

    // Run the compute shader
    d3dContext->Dispatch((desc1.Width / 8) + 1, (desc1.Height / 8) + 1, 1);

    // Get the results
    d3dContext->CopyResource(diffStagingBuffer.get(), diffResultBuffer.get());
    auto diffResult = ReadFromBuffer<DiffInfo>(d3dContext, diffStagingBuffer);

    if (diffResult.NumDifferingColorPixels > 0 || diffResult.NumDifferingAlphaPixels > 0)
    {
        wprintf(L"Diff found!\n");

        if (diffResult.NumDifferingColorPixels > 0)
        {
            auto outputPath = SaveTextureToPathAsync(colorChannelDiffTexture, L"diff.color.png").get();
            wprintf(L"Color channel diff saved to \"%s\"\n", outputPath.c_str());
        }
        else
        {
            wprintf(L"No differences in the color channels found, skipping...\n");
        }

        if (diffResult.NumDifferingAlphaPixels > 0)
        {
            auto outputPath = SaveTextureToPathAsync(alphaChannelDiffTexture, L"diff.alpha.png").get();
            wprintf(L"Alpha channel diff saved to \"%s\"\n", outputPath.c_str());
        }
        else
        {
            wprintf(L"No differences in the alpha channel found, skipping...\n");
        }
    }
    else
    {
        wprintf(L"No difference found between images.\n");
    }

    return 0;
}

std::future<winrt::com_ptr<ID3D11Texture2D>> LoadTextureFromPathAsync(winrt::com_ptr<ID3D11Device> device, std::wstring const& path)
{
    auto file = co_await util::GetStorageFileFromPathAsync(path);
    auto stream = co_await file.OpenReadAsync();
    auto texture = co_await util::LoadTextureFromStreamAsync(stream, device);
    co_return texture;
}

template <typename T>
T ReadFromBuffer(
    winrt::com_ptr<ID3D11DeviceContext> const& d3dContext,
    winrt::com_ptr<ID3D11Buffer> const& stagingBuffer)
{
    D3D11_BUFFER_DESC desc = {};
    stagingBuffer->GetDesc(&desc);

    assert(sizeof(T) <= desc.ByteWidth);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    d3dContext->Map(stagingBuffer.get(), 0, D3D11_MAP_READ, 0, &mapped);

    T result = {};
    result = *reinterpret_cast<T*>(mapped.pData);

    d3dContext->Unmap(stagingBuffer.get(), 0);

    return result;
}

std::future<std::wstring> SaveTextureToPathAsync(winrt::com_ptr<ID3D11Texture2D> texture, std::wstring const& path)
{
    auto file = co_await util::CreateStorageFileFromPathAsync(path);
    std::wstring extension(file.FileType());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t c) { return std::towlower(c); });
    util::BitmapEncoding encoding;
    if (extension == L".png")
    {
        encoding = util::BitmapEncoding::Png;
    }
    else if (extension == L".jpeg" || extension == L".jpg")
    {
        encoding = util::BitmapEncoding::Jpeg;
    }
    else if (extension == L".bmp")
    {
        encoding = util::BitmapEncoding::Bmp;
    }
    else
    {
        throw std::runtime_error("Invalid extension!");
    }
    auto stream = co_await file.OpenAsync(winrt::FileAccessMode::ReadWrite);
    co_await util::SaveTextureToStreamAsync(texture, stream, encoding);
    co_return std::wstring(file.Path());
}