# Optional macOS login agent

First verify the server works in Terminal. Keep the project and virtual environment at a stable location, preferably outside Desktop, Documents, Downloads, or iCloud Drive: background processes may encounter macOS privacy restrictions there. A path such as `~/Applications/MacWire` is suitable. Recreate the virtual environment after moving the project; virtual environments are not portable.

Copy `backend/com.macwire.helper.plist.example` to `~/Library/LaunchAgents/com.macwire.helper.plist`, creating `~/Library/LaunchAgents` if needed. Edit the file and replace every `/ABSOLUTE/PATH/TO/MacWire` with the absolute project path. Replace `/ABSOLUTE/PATH/TO/LOGS` with an existing writable directory, for example `/Users/yourname/Library/Logs/MacWire Helper` (create it first). `~` and shell variables are not expanded in plist strings. XML-escape `&` as `&amp;` in paths.

The template uses the same per-user configuration/cache defaults as the CLI. To keep an existing local configuration, add separate `<string>--config</string>` and `<string>/absolute/path/to/feeds.json</string>` elements to `ProgramArguments`; do the same for `--data-dir` if needed. Stop your Terminal server before loading the agent to avoid a port conflict. Stop any previously installed login agent before loading this one so two helpers do not compete for the same port.

Validate and start:

```sh
plutil -lint "$HOME/Library/LaunchAgents/com.macwire.helper.plist"
launchctl bootstrap "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.macwire.helper.plist"
launchctl print "gui/$(id -u)/com.macwire.helper"
```

The agent starts at login and restarts after an unexpected failure. Logs go to the configured files; they are not automatically rotated. Inspect logs if it repeatedly exits, and remove the agent while fixing configuration errors.

Restart after changing feeds:

```sh
launchctl kickstart -k "gui/$(id -u)/com.macwire.helper"
```

Stop and uninstall:

```sh
launchctl bootout "gui/$(id -u)" "$HOME/Library/LaunchAgents/com.macwire.helper.plist"
rm "$HOME/Library/LaunchAgents/com.macwire.helper.plist"
```

Uninstalling the agent leaves your feeds and cache intact. This runs as your user and requires an active login. It does not install a privileged daemon or prevent sleep.
