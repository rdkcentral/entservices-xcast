## Purpose

Defines the out-of-process `PlaybinPlugin` capability that lets callers configure a media URI and control playback (play/pause/stop) of a single `playbin`-backed pipeline through both COM-RPC and JSON-RPC.

## ADDED Requirements

### Requirement: Configure media without starting playback
The system SHALL accept a media location (local file path, `file://` URI, `http://`/`https://` URL, or any other URI scheme the underlying `playbin` element supports) via a `Configure` operation, normalize a bare local file path into a `file://` URI, store it as the pipeline's target, and SHALL NOT start or change playback state as a result of `Configure` alone.

#### Scenario: Configure with an absolute local file path
- **WHEN** `Configure` is called with an absolute local file system path (no URI scheme)
- **THEN** the system normalizes it to a `file://` URI, stores it as the configured media, returns success, and playback does not start

#### Scenario: Configure with a file:// URI
- **WHEN** `Configure` is called with a `file://` URI
- **THEN** the system stores the URI as-is as the configured media, returns success, and playback does not start

#### Scenario: Configure with an HTTP/HTTPS URL
- **WHEN** `Configure` is called with an `http://` or `https://` URL
- **THEN** the system stores the URL as the configured media, returns success, and playback does not start

#### Scenario: Configure called again while idle
- **WHEN** `Configure` is called a second time with a new media location before `Play` has been invoked
- **THEN** the system replaces the previously configured media with the new one and returns success

#### Scenario: Configure called while a pipeline is playing or paused
- **WHEN** `Configure` is called while a previous `Play` has left the pipeline in the PLAYING or PAUSED state
- **THEN** the system stops and releases the existing pipeline first, then applies the new configuration as if starting from idle, and returns success

#### Scenario: Configure with an empty or malformed media location
- **WHEN** `Configure` is called with an empty string or a location that cannot be resolved to a valid URI
- **THEN** the system returns an error, does not store the invalid location, and any previously configured media (if any) remains unchanged

### Requirement: Play starts or resumes playback of configured media
The system SHALL start playback of the media configured through `Configure` by creating (if not already created) a `playbin`-based pipeline, setting its `uri` property to the configured media, and transitioning it to the PLAYING state. If the pipeline already exists and is PAUSED, `Play` SHALL resume it to PLAYING without recreating the pipeline or re-reading the configured URI.

#### Scenario: Play after Configure
- **WHEN** `Play` is called after a successful `Configure`
- **THEN** the system creates the `playbin` pipeline with the configured URI, transitions it to PLAYING, and returns success

#### Scenario: Play without prior Configure
- **WHEN** `Play` is called and no media has ever been configured
- **THEN** the system returns an invalid-state error and does not create a pipeline

#### Scenario: Play while already playing
- **WHEN** `Play` is called while the pipeline is already in the PLAYING state
- **THEN** the system leaves playback running, returns success, and does not recreate the pipeline

#### Scenario: Resume with Play after Pause
- **WHEN** `Play` is called while the pipeline is in the PAUSED state
- **THEN** the system transitions the existing pipeline to PLAYING without recreating it and returns success

#### Scenario: Play after Stop requires reconfiguration behavior
- **WHEN** `Play` is called after a prior `Stop` has released the pipeline, using the media location that was last configured before `Stop`
- **THEN** the system creates a new `playbin` pipeline using that previously configured media and transitions it to PLAYING, since `Stop` clears pipeline state but not the last configured media location

### Requirement: Pause suspends playback while preserving pipeline state
The system SHALL transition an existing pipeline from PLAYING to PAUSED without destroying it, so that subsequent `Play` calls resume from the same position.

#### Scenario: Pause while playing
- **WHEN** `Pause` is called while the pipeline is in the PLAYING state
- **THEN** the system transitions the pipeline to PAUSED, keeps it alive, and returns success

#### Scenario: Pause without an active pipeline
- **WHEN** `Pause` is called and no pipeline currently exists (no `Play` has been called, or `Stop` has already released it)
- **THEN** the system returns an invalid-state error and takes no pipeline action

#### Scenario: Pause while already paused
- **WHEN** `Pause` is called while the pipeline is already in the PAUSED state
- **THEN** the system leaves the pipeline paused and returns success

### Requirement: Stop releases the playback pipeline
The system SHALL stop playback, transition the pipeline to the NULL state, and release/unref all pipeline resources, leaving the system in an idle state where a subsequent `Play` (using the previously configured media) creates a fresh pipeline.

#### Scenario: Stop while playing or paused
- **WHEN** `Stop` is called while the pipeline is in the PLAYING or PAUSED state
- **THEN** the system transitions the pipeline to NULL, releases all pipeline resources, and returns success

#### Scenario: Stop when nothing is playing
- **WHEN** `Stop` is called and no pipeline currently exists
- **THEN** the system returns an invalid-state error and performs no pipeline action

#### Scenario: Stop called twice in a row
- **WHEN** `Stop` is called immediately after a previous successful `Stop` with no intervening `Play`
- **THEN** the second `Stop` returns an invalid-state error, consistent with "no pipeline currently exists"

### Requirement: Playback lifecycle notifications
The system SHALL notify registered listeners when the pipeline reaches the PLAYING state for the first time after creation and when the pipeline has been stopped and released, mirroring the existing player notification pattern of `OnPlayerInitialized`/`OnPlayerStopped`.

#### Scenario: Notification on successful playback start
- **WHEN** the `playbin` pipeline reports it has reached PLAYING after a `Play` call
- **THEN** the system fires an initialized/started notification to all registered listeners

#### Scenario: Notification on stop
- **WHEN** `Stop` completes releasing the pipeline
- **THEN** the system fires a stopped notification to all registered listeners

### Requirement: Unsupported or invalid media reports a playback error
The system SHALL surface a playback error, rather than silently succeeding, when the configured media cannot be played because no compatible GStreamer source, demuxer, decoder, or sink is available on the target, or the URI cannot be resolved by `playbin`.

#### Scenario: Play with an unsupported or unreachable URI
- **WHEN** `Play` is called with configured media whose scheme or format `playbin` cannot resolve or decode on the running target
- **THEN** the system reports a playback error (via the call's return value and/or an error notification) instead of leaving the caller with no indication of failure
