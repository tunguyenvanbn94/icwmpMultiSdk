#include "dm_model_policy.h"

#include <strings.h>

static unsigned int model_capability(enum dm_model_policy_choice model)
{
    switch (model) {
    case DM_MODEL_POLICY_TR098: return DM_MODEL_CAP_TR098;
    case DM_MODEL_POLICY_TR181: return DM_MODEL_CAP_TR181;
    default: return 0;
    }
}

enum dm_model_policy_status dm_model_policy_resolve(
    const char *requested, enum dm_model_policy_choice product_default,
    unsigned int compiled_models, enum dm_model_policy_choice *selected)
{
    enum dm_model_policy_choice target;
    unsigned int wanted;

    if (!selected)
        return DM_MODEL_POLICY_INVALID_ARGUMENT;
    if (!(compiled_models & (DM_MODEL_CAP_TR098 | DM_MODEL_CAP_TR181)))
        return DM_MODEL_POLICY_NO_MODELS;

    if (!requested) {
        target = product_default;
    } else if (strcasecmp(requested, "tr098") == 0) {
        target = DM_MODEL_POLICY_TR098;
    } else if (strcasecmp(requested, "tr181") == 0) {
        target = DM_MODEL_POLICY_TR181;
    } else {
        return DM_MODEL_POLICY_INVALID_VALUE;
    }

    wanted = model_capability(target);
    if (!wanted)
        return DM_MODEL_POLICY_INVALID_VALUE;
    if (!(compiled_models & wanted))
        return DM_MODEL_POLICY_UNAVAILABLE;

    *selected = target;
    return DM_MODEL_POLICY_OK;
}

const char *dm_model_policy_status_name(enum dm_model_policy_status status)
{
    switch (status) {
    case DM_MODEL_POLICY_OK: return "OK";
    case DM_MODEL_POLICY_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case DM_MODEL_POLICY_INVALID_VALUE: return "INVALID_VALUE";
    case DM_MODEL_POLICY_UNAVAILABLE: return "UNAVAILABLE";
    case DM_MODEL_POLICY_NO_MODELS: return "NO_MODELS";
    default: return "UNKNOWN";
    }
}
