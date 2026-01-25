#pragma once

// ImGui용 간단 래퍼
// - 라벨을 wide literal(TCHAR / wchar_t*)로 받아서 UTF-8로 변환 후 ImGui에 넘깁니다.
// - 예)
//   Alice::ImGuiCheckbox(TEXT("Fill Light (보조광)"), &flag);
//   Alice::ImGuiSliderFloat(TEXT("Key Intensity (주광)"), &value, 0.0f, 3.0f);
//   Alice::ImGuiSliderFloat3(TEXT("Key Direction (주광)"), &vec.x, -1.0f, 1.0f);
//
// 매 프레임 메모리 할당을 방지하기 위해 thread_local 버퍼를 재사용합니다.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "imgui.h"
#include <string>

namespace Alice
{
    // 매 프레임 할당을 방지하기 위한 변환 헬퍼
    // thread_local을 사용하여 스레드별로 버퍼를 하나만 만들고 계속 재사용합니다.
    inline const char* WideToUtf8Reuse(const wchar_t* wstr)
    {
        // 정적 버퍼 (메모리 재사용) - 스레드당 하나만 생성됨
        static thread_local std::string s_buffer;
        
        // 빈 문자열 처리
        if (!wstr || *wstr == 0) 
            return "";

        // 필요한 UTF-8 길이 계산 (Null terminator 포함)
        int len = ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        
        if (len > 0)
        {
            // 버퍼 크기 조정 (Capacity가 충분하면 메모리 할당 없음)
            // len-1은 std::string 사이즈에서 null char를 뺀 순수 문자열 길이
            s_buffer.resize(len - 1); 
            
            // 변환 수행 (데이터를 버퍼에 직접 기록)
            ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, s_buffer.data(), len, nullptr, nullptr);
        }
        else
        {
            s_buffer.clear();
        }

        return s_buffer.c_str();
    }

    // =========================================================
    // ImGui 래퍼 함수들 (이제 메모리 할당을 하지 않음)
    // =========================================================

    inline void ImGuiText(const wchar_t* text)
    {
        // Utf8(text) 대신 최적화된 함수 사용
        ImGui::TextUnformatted(WideToUtf8Reuse(text));
    }

    inline void ImGuiText(const char* text)
    {
        ImGui::TextUnformatted(text);
    }

    inline bool ImGuiCheckbox(const wchar_t* label, bool* v)
    {
        return ImGui::Checkbox(WideToUtf8Reuse(label), v);
    }

    inline bool ImGuiSliderFloat(const wchar_t* label,
                                 float* v,
                                 float v_min,
                                 float v_max,
                                 const char* format = "%.3f",
                                 ImGuiSliderFlags flags = 0)
    {
        return ImGui::SliderFloat(WideToUtf8Reuse(label), v, v_min, v_max, format, flags);
    }

    inline bool ImGuiSliderFloat3(const wchar_t* label,
                                  float v[3],
                                  float v_min,
                                  float v_max,
                                  const char* format = "%.3f",
                                  ImGuiSliderFlags flags = 0)
    {
        return ImGui::SliderFloat3(WideToUtf8Reuse(label), v, v_min, v_max, format, flags);
    }
}


