#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options final {
    std::wstring source;
    std::string entry;
    std::string target;
    std::wstring output;
    std::vector<std::string> defineNames;
    std::vector<std::string> defineValues;
};

[[nodiscard]] std::string narrowAscii(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character < 0 || character > 0x7F) return {};
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] bool parse(int argc, wchar_t** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument = argv[index];
        if ((argument == L"--source" || argument == L"--entry"
                || argument == L"--target" || argument == L"--output"
                || argument == L"--define")
            && index + 1 < argc) {
            const std::wstring_view value = argv[++index];
            if (argument == L"--source") options.source = value;
            else if (argument == L"--entry") options.entry = narrowAscii(value);
            else if (argument == L"--target") options.target = narrowAscii(value);
            else if (argument == L"--output") options.output = value;
            else {
                const std::string definition = narrowAscii(value);
                const std::size_t separator = definition.find('=');
                if (definition.empty() || separator == 0) return false;
                options.defineNames.push_back(definition.substr(0, separator));
                options.defineValues.push_back(separator == std::string::npos
                    ? "1" : definition.substr(separator + 1U));
            }
        } else {
            return false;
        }
    }
    return !options.source.empty() && !options.entry.empty()
        && !options.target.empty() && !options.output.empty();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        std::fputs(
            "usage: HeniaUIShaderCompiler --source file --entry name "
            "--target profile --output file [--define NAME=VALUE]\n",
            stderr);
        return 2;
    }

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(options.defineNames.size() + 1U);
    for (std::size_t index = 0; index < options.defineNames.size(); ++index) {
        macros.push_back({
            options.defineNames[index].c_str(),
            options.defineValues[index].c_str(),
        });
    }
    macros.push_back({nullptr, nullptr});

    Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
    Microsoft::WRL::ComPtr<ID3DBlob> diagnostics;
    constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS
        | D3DCOMPILE_OPTIMIZATION_LEVEL3
        | D3DCOMPILE_IEEE_STRICTNESS
        | D3DCOMPILE_WARNINGS_ARE_ERRORS;
    const HRESULT result = D3DCompileFromFile(
        options.source.c_str(),
        options.defineNames.empty() ? nullptr : macros.data(),
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        options.entry.c_str(),
        options.target.c_str(),
        flags,
        0,
        &bytecode,
        &diagnostics);
    if (diagnostics != nullptr && diagnostics->GetBufferPointer() != nullptr) {
        std::fwrite(
            diagnostics->GetBufferPointer(),
            1,
            diagnostics->GetBufferSize(),
            FAILED(result) ? stderr : stdout);
    }
    if (FAILED(result) || bytecode == nullptr) {
        std::fprintf(stderr, "HLSL compilation failed with HRESULT 0x%08lX\n", result);
        return 1;
    }

    FILE* output = nullptr;
    if (_wfopen_s(&output, options.output.c_str(), L"wb") != 0 || output == nullptr) {
        std::fputs("Unable to open the HLSL bytecode output file\n", stderr);
        return 1;
    }
    const std::size_t written = std::fwrite(
        bytecode->GetBufferPointer(),
        1,
        bytecode->GetBufferSize(),
        output);
    const int closeResult = std::fclose(output);
    if (written != bytecode->GetBufferSize() || closeResult != 0) {
        std::fputs("Unable to write the complete HLSL bytecode output\n", stderr);
        return 1;
    }
    return 0;
}
