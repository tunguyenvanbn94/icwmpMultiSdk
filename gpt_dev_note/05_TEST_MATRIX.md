# Evidence matrix — SDK × model × capability

**Meaning:** `REPORTED` means prior project records claim success. `NOT RUN HERE` means no independent test was executed in this preparation environment. Update with exact SHA/image/build logs after checkout.

| SDK | Model | Build evidence | Board evidence | BBF/schema | ACS E2E | Production-readiness note |
|---|---|---|---|---|---|---|
| MTK/Airoha | TR-098 | REPORTED PASS | REPORTED parity PASS (full-C) | Product inventory 783 C | REPORTED sessions + selected RPC | Need model-switch/stress regression on new SHA |
| MTK/Airoha | TR-181 | REPORTED PASS | REPORTED PASS | REPORTED T7 734 advertised parameters | REPORTED GenieACS session; active RPC matrix incomplete | 734 ≠ all TR-181; some create/delete/profile limitations |
| Broadcom BDK | TR-098 | REPORTED image build | REPORTED 87/87 selected apply checks across both models | Product-specific TR-098 facade | REPORTED session | Full parity, transactional faults, runtime apply remain |
| Broadcom BDK | TR-181 | REPORTED image build | REPORTED selected board cases | Known MDM-vs-BBF diffs | REPORTED session | Do not equate MDM persisted with runtime applied |
| Generic UCI | TR-098 | Backend source present; evidence incomplete | NOT RUN HERE | NOT RUN HERE | NOT RUN HERE | Reference only |
| Generic UCI | TR-181 | Unverified | NOT RUN HERE | NOT RUN HERE | NOT RUN HERE | Do not market as supported |

## Test capture template

| Commit | SDK | Model | Check | Level | Result | Log/image/source | Outstanding issue |
|---|---|---|---|---|---|---|---|
| *pending real checkout* | — | — | static guard 7 test fixtures | UNIT | PASS locally | `tests/test_check_multisdk_gates.py` | Real code + production sample flags pending |

## PH-G2a acceptance status (11 Oct 2026)

| Commit | SDK | Model | Check | Level | Result | Evidence | Outstanding |
|---|---|---|---|---|---|---|---|
| This PH-G2a commit | any | TR098/TR181 | production `dm_model_policy.c` test | ISOLATED HOST C | **37 assertions PASS** | `gpt_dev_note/tests/run_dm_model_policy.sh` | Integration host / SDK / board still required |
| This PH-G2a commit | MTK/BDK/UCI | both | startup/reload + ACS regression | SDK/BOARD | **NOT RUN** | `07_MODEL_RESOLVER_POLICY.md` | Do not mark phase DONE |
