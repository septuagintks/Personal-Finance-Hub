// Personal Finance Hub - Authentication Controller

#pragma once

#include "pfh/application/services/auth_service.h"
#include "pfh/presentation/http/http_types.h"

namespace pfh::presentation {

class AuthController {
public:
    explicit AuthController(application::AuthService& auth) : auth_(auth) {}

    [[nodiscard]] HttpResponse register_user(const HttpRequest& request);
    [[nodiscard]] HttpResponse login(const HttpRequest& request);
    [[nodiscard]] HttpResponse refresh(const HttpRequest& request);
    [[nodiscard]] HttpResponse logout(const HttpRequest& request);

private:
    application::AuthService& auth_;
};

} // namespace pfh::presentation
