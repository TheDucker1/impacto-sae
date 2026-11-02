# Development instructions

Preserve the runtime behavior while reusing existing Impacto primitives and services.
Understand whole-function control/data flow and owner lifecycle before changing
related consumers together. Use source review for clear assignments and established
composition; select small checks only for consequential unresolved behavior. Avoid
fragment-by-fragment edits, automatic test ladders and repeated passed campaigns.

Keep game assets, extracted data, native snapshots/disassembly, research notes,
evidence, session histories and local verification tools outside this repository.
They must not be staged or published. Public documentation should explain the
implementation's behavior and build/use contracts without embedding native data.
The sibling ../saekano-local/impacto directory is the current optional local
research workspace; its private docs retain planning and development context.
Do not recreate those records in this repository. Respect current user scope
and paused/closed goals; old research cursors do not select new work.

Build the actual impacto product; do not add a separate headless product or
require private tests/data to configure or build it. Never package original game
files, personal saves or research material. Preserve unrelated user changes.
