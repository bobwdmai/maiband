#pragma once

#include "core/AudioEngine.h"
#include "core/Library.h"
#include "core/Timeline.h"
#include "core/Transport.h"
#include "core/Undo.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace bandforge_app {

class TrackListComponent;
class TimelineComponent;
class LibraryPanelComponent;
class EditorPanelComponent;

struct SelectionState {
    bandforge::TrackId selectedTrackId = 0;
    bandforge::ClipId selectedClipId = 0;
};

class MainComponent final : public juce::AudioAppComponent,
                            private juce::Timer,
                            private juce::MidiInputCallback {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void resized() override;
    void paint(juce::Graphics& graphics) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

private:
    void timerCallback() override;
    void refreshViews();
    void setSelectedTrack(bandforge::TrackId trackId);
    void setSelectedClip(bandforge::TrackId trackId, bandforge::ClipId clipId);
    void openProject();
    void saveProject();
    void undo();
    void redo();
    void addMidiTrack();
    void createMidiTrack(bandforge::TrackKind kind);
    void addAudioTrack();
    void exportWav();
    void startRecording();
    void stopRecording();
    void openPluginBrowser();
    void loadPluginOnTrack(bandforge::TrackId trackId, const juce::PluginDescription& desc);
    void openDeviceSettings();
    void recordMusicalTypingMessage(const juce::MidiMessage& msg);
    void releaseActiveMusicalTypingKeys();
    [[nodiscard]] bandforge::TrackId chooseMidiRecordTarget() const;
    [[nodiscard]] bandforge::TrackId chooseMusicalTypingRecordTarget() const;
    bandforge::TrackId ensureMidiRecordTarget(bool preferSelectedTrack);
    void armMidiRecordTarget(bandforge::TrackId trackId, bool remember);
    void closeActiveMidiNotesForRecording();
    [[nodiscard]] bandforge::TrackKind selectedTrackKind() const;

    // juce::MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override;

    bandforge::Project project_;
    bandforge::Transport transport_;
    bandforge::AudioEngine audioEngine_;
    bandforge::SoundLibrary library_;
    bandforge::ProjectHistory history_;
    SelectionState selection_;
    bandforge::GridSettings grid_ { 72.0, 0.25, true };

    // Declared before buttons so it is destroyed after them
    std::unique_ptr<juce::LookAndFeel> toolbarLaf_;
    juce::TooltipWindow tooltipWindow_ { this, 600 };

    juce::TextButton openButton_ { "Open" };
    juce::TextButton saveButton_ { "Save" };
    juce::TextButton undoButton_ { "Undo" };
    juce::TextButton redoButton_ { "Redo" };
    juce::TextButton playButton_ { "Play" };
    juce::TextButton stopButton_ { "Stop" };
    juce::TextButton recordButton_ { "Rec" };
    juce::TextButton addMidiButton_ { "+ MIDI" };
    juce::TextButton addAudioButton_ { "+ Audio" };
    juce::TextButton loopButton_ { "Cycle" };
    juce::TextButton metronomeButton_ { "Metro" };
    juce::TextButton snapButton_ { "Snap" };
    juce::TextButton zoomOutButton_ { "-" };
    juce::TextButton zoomInButton_ { "+" };
    juce::TextButton exportButton_ { "Export" };
    juce::TextButton settingsButton_ { "Devices" };
    juce::Label positionLabel_;
    juce::Label tempoLabel_;
    juce::Label recordingStatusLabel_;

    std::unique_ptr<TrackListComponent> trackList_;
    std::unique_ptr<TimelineComponent> timeline_;
    std::unique_ptr<LibraryPanelComponent> libraryPanel_;
    std::unique_ptr<EditorPanelComponent> editorPanel_;
    std::unique_ptr<juce::FileChooser> projectChooser_;
    std::unique_ptr<juce::FileChooser> exportChooser_;

    std::vector<float> renderScratch_;
    double currentSampleRate_ = 48000.0;
    bool metronomeEnabled_ = false;
    double lastPositionBeat_ = -1.0;

    // ── VU meter levels (updated in timerCallback, read from TrackListComponent) ─
    std::map<bandforge::TrackId, float> trackDisplayLevels_;

    // ── Auto-save ─────────────────────────────────────────────────────────────
    int autoSaveCounterTicks_ = 0;

    int keyboardOctave_ = 4;
    int keyboardVelocity_ = 100;
    mutable std::mutex activeNoteKeysMutex_;
    std::map<int, int> activeNoteKeys_; // keyCode/source id -> MIDI pitch

    // ── Audio recording ───────────────────────────────────────────────────────
    class AudioRecorder;
    std::unique_ptr<AudioRecorder> audioRecorder_;
    juce::File recordingFile_;
    double recordStartBeat_ = 0.0;
    bandforge::TrackId recordTargetTrack_ = 0;

    // ── MIDI recording ────────────────────────────────────────────────────────
    juce::MidiMessageCollector midiCollector_;
    std::mutex midiRecordMutex_;
    struct TimedMidiMsg { juce::MidiMessage msg; double beatPosition = 0.0; };
    std::vector<TimedMidiMsg> recordedMidi_;
    bool midiRecording_ = false;
    bandforge::TrackId midiRecordTargetTrack_ = 0;
    bool musicalTypingRecorded_ = false;

    // ── Plugin hosting ────────────────────────────────────────────────────────
    juce::AudioPluginFormatManager formatManager_;
    juce::KnownPluginList knownPlugins_;
    juce::ThreadPool pluginScanPool_ { 1 };

    struct TrackPlugin {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::AudioBuffer<float> pluginBuffer;
        bool prepared = false;
        int latencySamples = 0;

        // Delay line for compensating this plugin's latency against the dry mix
        std::vector<std::vector<float>> delayBuffers; // one ring buffer per channel
        int delayWritePos = 0;
    };
    std::mutex pluginMutex_;
    std::map<bandforge::TrackId, std::unique_ptr<TrackPlugin>> trackPlugins_;

    juce::TextButton pluginsButton_ { "Plugins" };
    std::unique_ptr<juce::FileChooser> pluginChooser_;
    std::map<bandforge::TrackId, std::unique_ptr<juce::DocumentWindow>> pluginWindows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace bandforge_app
