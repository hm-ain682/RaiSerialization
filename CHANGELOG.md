# Changelog

## Unreleased
- Added `readFormat` / `writeFormat`-based extension path for custom types.
- Removed `HasReadJson` / `HasWriteJson` compatibility aliases.

### Migration checklist
- [x] Update examples and documents to use `readFormat` / `writeFormat` as primary API.
- [x] Update tests and helper constraints to avoid direct dependency on `HasReadJson` / `HasWriteJson`.
- [x] Remove compatibility aliases from `ObjectConverter.cppm`.
- [x] Remove `readJson` / `writeJson` fallback path in `JsonIO.cppm`.
- [ ] Bump major version and publish migration notes.

## 0.1.0 - 2025-12-13
- Initial public release
- C++23 module-based JSON5 tokenizer, parser, writer
- Declarative JsonField bindings, enum/polymorphic support
- ThreadPool and parallel read helpers
- Added CMake install and package config generation
