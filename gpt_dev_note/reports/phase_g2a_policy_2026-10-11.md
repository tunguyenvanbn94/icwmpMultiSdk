# PH-G2a — Verification ledger

- Base head checked through connected GitHub: `29d314a1cab95928177901ed20e607263c89f71c`.
- Product source reviewed: `dmentry.c`, MTK/BDK/UCI app hooks, BDK proxy, model registry, app config/reload and host runner.
- Standalone **production** policy built using local C compiler: `-std=c11 -Wall -Wextra -Werror -pedantic`.
- Output: `PH-G2a production model policy: 37 assertions PASS` (11 Oct 2026).
- All 10 existing-source patch anchors checked against the exact `dev_gpt` checkout content provided by the GitHub connection.
- Host build of actual `libtr098` + `icwmpd`: **NOT RUN** (SDK/host dependencies unavailable in this runner).
- Cross compiler MTK/BDK SDK build: **NOT RUN**.
- MTK/BDK board + ACS, 24h soak: **NOT RUN**.
- This patch must not be labelled PH-G2a DONE or production-approved until these gates pass.
