#pragma once

namespace pf_web {

struct AccessContext {
    bool provisioning_ap = false;
    bool initial_bootstrap = false;
    bool password_bootstrap = false;
    bool management_password_configured = false;
    bool authenticated = false;
    bool csrf_valid = false;
};

inline bool bootstrap_access_allowed(
    const AccessContext& context)
{
    return context.provisioning_ap &&
           context.initial_bootstrap &&
           !context.management_password_configured;
}

inline bool password_setup_allowed(
    const AccessContext& context)
{
    return context.provisioning_ap &&
           context.password_bootstrap &&
           !context.management_password_configured;
}

inline bool wifi_scan_allowed(const AccessContext& context)
{
    return bootstrap_access_allowed(context) ||
           context.authenticated;
}

inline bool wifi_config_allowed(const AccessContext& context)
{
    return bootstrap_access_allowed(context) ||
           (context.authenticated && context.csrf_valid);
}

}  // namespace pf_web
