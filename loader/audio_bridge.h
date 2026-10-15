#ifndef SF4CE_AUDIO_BRIDGE_H
#define SF4CE_AUDIO_BRIDGE_H
struct so_module;
bool InstallAudioBridge(so_module *module, void *java_vm);
bool InitAudioBridge();
bool PauseAudioBridgeForMovie();
void ResumeAudioBridgeAfterMovie(bool was_running);
#endif
