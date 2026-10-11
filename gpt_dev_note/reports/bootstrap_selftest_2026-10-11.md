# Bootstrap importer self-test — 2026-10-11

**Environment:** isolated synthetic Git repository and local bare remote; NOT the user's GitHub repository.

## Test procedure

1. Created a minimal Git repository with `dev_181` commit and matching expected SDK folder structure.
2. Configured `origin` to expected GitHub URL, redirecting transport to isolated local bare remote only inside test checkout.
3. Ran `bin/bootstrap_dev_gpt.sh <test-checkout> --push` with the prepared bundle.
4. Verified branch `dev_gpt` HEAD descends directly from synthetic `dev_181` SHA.
5. Verified exactly two new commits with expected phase messages and no changes to synthetic `dev_181`.
6. Verified 7/7 preflight fixture tests PASS and advisory report generated.
7. Verified the temporary local bare remote received `dev_gpt`.

**Outcome:** importer PASS. This proves the *workflow script* works on a controlled Git fixture. It does **not** prove the real repo is accessible, the current `dev_181` HEAD matches earlier source review, real SDK builds, or board behavior.
