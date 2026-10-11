/* Standalone tests compile the production dm_model_policy.c without SDK deps. */
#include "dm_model_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned int assertions;
#define CHECK(c) do { assert(c); assertions++; } while (0)
#define CASE(value, def, caps, prior, expected, result) do {             \
    enum dm_model_policy_choice output = (prior);                        \
    enum dm_model_policy_status status = dm_model_policy_resolve(         \
        (value), (def), (caps), &output);                                 \
    CHECK(status == (expected));                                         \
    CHECK(output == (result));                                           \
} while (0)

int main(void)
{
    const unsigned int dual = DM_MODEL_CAP_TR098 | DM_MODEL_CAP_TR181;
    const unsigned int only98 = DM_MODEL_CAP_TR098;
    const unsigned int only181 = DM_MODEL_CAP_TR181;
    enum dm_model_policy_choice choice = DM_MODEL_POLICY_TR181;

    CASE("tr098", DM_MODEL_POLICY_TR181, dual, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_OK, DM_MODEL_POLICY_TR098);
    CASE("tr181", DM_MODEL_POLICY_TR098, dual, DM_MODEL_POLICY_TR098,
         DM_MODEL_POLICY_OK, DM_MODEL_POLICY_TR181);
    CASE("TR098", DM_MODEL_POLICY_TR181, dual, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_OK, DM_MODEL_POLICY_TR098);
    CASE("Tr181", DM_MODEL_POLICY_TR098, dual, DM_MODEL_POLICY_TR098,
         DM_MODEL_POLICY_OK, DM_MODEL_POLICY_TR181);
    CASE(NULL, DM_MODEL_POLICY_TR098, only98, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_OK, DM_MODEL_POLICY_TR098);
    CASE(NULL, DM_MODEL_POLICY_TR181, only181, DM_MODEL_POLICY_TR098,
         DM_MODEL_POLICY_OK, DM_MODEL_POLICY_TR181);
    CASE(NULL, DM_MODEL_POLICY_TR098, only181, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_UNAVAILABLE, DM_MODEL_POLICY_TR181);
    CASE("tr181", DM_MODEL_POLICY_TR098, only98, DM_MODEL_POLICY_TR098,
         DM_MODEL_POLICY_UNAVAILABLE, DM_MODEL_POLICY_TR098);
    CASE("tr098", DM_MODEL_POLICY_TR181, only181, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_UNAVAILABLE, DM_MODEL_POLICY_TR181);
    CASE("", DM_MODEL_POLICY_TR098, dual, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_INVALID_VALUE, DM_MODEL_POLICY_TR181);
    CASE("tr182", DM_MODEL_POLICY_TR098, dual, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_INVALID_VALUE, DM_MODEL_POLICY_TR181);
    CASE("tr181 ", DM_MODEL_POLICY_TR098, dual, DM_MODEL_POLICY_TR098,
         DM_MODEL_POLICY_INVALID_VALUE, DM_MODEL_POLICY_TR098);
    CASE("tr098\n", DM_MODEL_POLICY_TR098, dual, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_INVALID_VALUE, DM_MODEL_POLICY_TR181);
    CASE("tr098;rm", DM_MODEL_POLICY_TR098, dual, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_INVALID_VALUE, DM_MODEL_POLICY_TR181);
    CASE(NULL, DM_MODEL_POLICY_UNSET, dual, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_INVALID_VALUE, DM_MODEL_POLICY_TR181);
    CASE("tr098", DM_MODEL_POLICY_TR098, 0, DM_MODEL_POLICY_TR181,
         DM_MODEL_POLICY_NO_MODELS, DM_MODEL_POLICY_TR181);
    CHECK(dm_model_policy_resolve("tr098", DM_MODEL_POLICY_TR098, dual, NULL)
          == DM_MODEL_POLICY_INVALID_ARGUMENT);
    CHECK(dm_model_policy_resolve("tr098", DM_MODEL_POLICY_TR098, dual, &choice)
          == DM_MODEL_POLICY_OK);
    CHECK(choice == DM_MODEL_POLICY_TR098);
    CHECK(strcmp(dm_model_policy_status_name(DM_MODEL_POLICY_UNAVAILABLE),
                 "UNAVAILABLE") == 0);
    CHECK(strcmp(dm_model_policy_status_name((enum dm_model_policy_status)100),
                 "UNKNOWN") == 0);

    printf("PH-G2a production model policy: %u assertions PASS\n", assertions);
    return 0;
}
