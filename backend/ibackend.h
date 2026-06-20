#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

struct ImDrawData;

namespace ext::overlay {

class IBackend
{
public:
    virtual ~IBackend() = default;
    virtual bool init(HWND hwnd) = 0;
    virtual void shutdown() = 0;
    virtual void resize(UINT w, UINT h) = 0;
    virtual void new_frame() = 0;
    virtual void render(ImDrawData* draw_data) = 0;
};

}
