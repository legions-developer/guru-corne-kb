# Local firmware workflow

- After changing firmware code or configuration, build both Corne halves
  locally with `python3 scripts/build-local.py` and refresh `latest/left.uf2`
  and `latest/right.uf2` before reporting success.
- Publish a matching pair only after both builds succeed. If the build
  environment is unavailable or a build fails, report that clearly and keep
  the previous successful files; never describe them as including new changes.
- Keep the generated UF2 files ignored by Git. Firmware delivery must not
  depend on committing, pushing, or waiting for GitHub Actions.
- Keep `latest/README.md` accurate if the build workflow changes.
