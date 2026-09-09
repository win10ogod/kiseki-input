# Working in this repository

For Kiseki app operation or development, read [skills/kiseki-project/SKILL.md](skills/kiseki-project/SKILL.md). It routes to the relevant operation, precision, architecture, or testing guide; load references needed by the current task.

For recording, inspecting, annotating, or compiling a teaching bundle, read [skills/kiseki-teach-recording/SKILL.md](skills/kiseki-teach-recording/SKILL.md). Inspecting an existing bundle does not start another recording. Use its explicit recorder control helper when stopping a run.

Use the user's established target, mode, and authorization. Operational actions run through the CLI; the WebUI edits configuration and inspects local teaching files. Preserve requested input timing/detail and verify changes in the same target/session where they were made.

Skill helpers use Python 3.9+ and the standard library. Run `python3 -m unittest discover -s test/skills -v` when changing them; CTest also discovers this suite when a suitable Python interpreter is available. For other changes, use the relevant checks in the project skill. Keep generated recordings and desktop evidence under ignored `artifacts/` paths.
