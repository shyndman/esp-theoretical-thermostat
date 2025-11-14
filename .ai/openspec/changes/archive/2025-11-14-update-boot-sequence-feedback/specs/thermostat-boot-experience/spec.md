# thermostat-boot-experience Delta

## ADDED Requirements
### Requirement: Splash precedes service bring-up
The firmware SHALL bring up the LCD panel, LVGL adapter, and backlight before starting any transport or network stack, then load a minimalist splash screen that remains active until all services succeed.

#### Scenario: Splash-first boot
- **WHEN** `app_main` starts
- **THEN** it initializes BSP display handles, registers the LVGL display + touch, starts the adapter task, and brings up the backlight as the first major block
- **AND** it locks LVGL long enough to create a dedicated splash screen (solid background + centered label) showing initial status text
- **AND** it keeps that splash screen visible until Wi-Fi, SNTP, MQTT manager, and MQTT dataplane finish starting, after which it tears down the splash and loads `thermostat_ui_attach()`.

### Requirement: Stage status text updates
The splash label SHALL update before each boot stage using the pattern "<Verb> <stage description>..." so observers know what is executing.

#### Scenario: Stage-by-stage messaging
- **GIVEN** the splash screen is active
- **WHEN** the firmware prepares each subsystem (speaker, esp-hosted link, Wi-Fi, SNTP wait, MQTT client, MQTT dataplane)
- **THEN** it updates the splash text to verbs such as "Initializing speaker...", "Starting esp-hosted link...", "Syncing time...", etc. immediately before calling the corresponding init routine
- **AND** the label keeps the verb phrase visible until the next stage updates it or a failure occurs.

### Requirement: Failure messaging + halt
If any boot stage fails, the firmware SHALL replace the splash text with a failure message describing the stage and error, trigger the audio failure cue (subject to quiet hours), and halt boot so the message stays on screen.

#### Scenario: esp-hosted link fails
- **WHEN** `esp_hosted_link_start()` returns an error
- **THEN** the splash text updates to "Failed to start esp-hosted link: <err_name>"
- **AND** the system attempts to play the failure tone, logging WARN if quiet hours suppress playback or the codec is unavailable
- **AND** it stops further initialization (no thermostat UI attach, no MQTT start) while idling so technicians can read the screen.

#### Scenario: Success path resumes UI
- **WHEN** every boot stage completes successfully
- **THEN** the firmware hides/destroys the splash, loads the main thermostat UI, signals `backlight_manager_on_ui_ready()`, and continues the normal boot sequence.
