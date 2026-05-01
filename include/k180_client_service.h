// k180_client_service.h
#pragma once

namespace k180::client_service {

class ClientService {
public:
    ClientService() = default;
    ~ClientService() = default;

    ClientService(const ClientService&) = delete;
    ClientService& operator=(const ClientService&) = delete;

    bool start();
    void stop();

    void on_mouse_click(float x, float y);
    void on_cmd_set_ai_mode(int mode);
    void on_cmd_stop_tracking();
};

ClientService& service();

} // namespace k180::client_service
