/*
 * Model-selection policy shared by all icwmp SDKs. No UCI, platform or
 * data-model tree dependencies: callers supply discovered model capabilities.
 */
#ifndef ICWMP_DM_MODEL_POLICY_H
#define ICWMP_DM_MODEL_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

enum dm_model_policy_choice {
    DM_MODEL_POLICY_UNSET = -1,
    DM_MODEL_POLICY_TR098 = 0,
    DM_MODEL_POLICY_TR181 = 1
};

enum dm_model_policy_status {
    DM_MODEL_POLICY_OK = 0,
    DM_MODEL_POLICY_INVALID_ARGUMENT,
    DM_MODEL_POLICY_INVALID_VALUE,
    DM_MODEL_POLICY_UNAVAILABLE,
    DM_MODEL_POLICY_NO_MODELS
};

#define DM_MODEL_CAP_TR098 (1U << 0)
#define DM_MODEL_CAP_TR181 (1U << 1)

/*
 * requested == NULL means option absent: only then may product_default be used.
 * An explicitly present but empty/unknown option is an error. Model selection
 * never falls back to another compiled model. On failure *selected is unchanged.
 */
enum dm_model_policy_status dm_model_policy_resolve(
    const char *requested, enum dm_model_policy_choice product_default,
    unsigned int compiled_models, enum dm_model_policy_choice *selected);

const char *dm_model_policy_status_name(enum dm_model_policy_status status);

#ifdef __cplusplus
}
#endif
#endif
