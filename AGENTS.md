# PvZ AI bridge

This is an independent project. Do not change any other workspace project.
The supported target is the Steam GOTY 1.2.0.1096 x86 installation registered
for Steam app 3590; discover its exact library path through Steam metadata.

Preserve the game executable/resources and back up live saves before experiments.
No screenshot-based control, SendInput, mouse/keyboard simulation, or window
input messages. Gameplay must obey normal resource, cooldown and unlock rules.
Do not enable trainer cheats or report unsupported operations as successful.
Verify version/signatures before native calls. Gameplay calls run on the game
thread. DLL initialization must not block inside DllMain.

Use plain language in user documentation. Distinguish interface implementation,
live action verification, and actual wins. Do not claim AI has learned a video
without extracting and validating a strategy.
