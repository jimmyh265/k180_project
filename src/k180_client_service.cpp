// k180_client_service.cpp
#include "k180_client_service.h"

#include "k180_ai_runtime.h"
#include "k180_nvtracker.h"

namespace k180::client_service {

bool ClientService::start()
{
    return true;
}

void ClientService::stop()
{
}

void ClientService::on_mouse_click(float x, float y)
{
    using namespace k180::ai;

    if (g_ai_rt.mode.load(std::memory_order_acquire) != AiMode::DETECT_TO_TRACK) {
        return;
    }

    if (g_ai_rt.state.load(std::memory_order_acquire) != AiRuntimeState::DETECT) {
        return;
    }

    k180::ai::on_mouse_click(k180::ai::g_ai_rt, x, y);
}

void ClientService::on_cmd_set_ai_mode(int mode)
{
    using namespace k180::ai;

    AiMode m = cfg_to_ai_mode(mode);
    ai_apply_mode(g_ai_rt, m);

    if (m != AiMode::DETECT_TO_TRACK) {
        k180::nvtracker::service().stop_tracking();
    }
}

void ClientService::on_cmd_stop_tracking()
{
    k180::nvtracker::service().stop_tracking();
    k180::ai::ai_switch_to_detect(k180::ai::g_ai_rt);
}

ClientService& service()
{
    static ClientService s;
    return s;
}

} // namespace k180::client_service
