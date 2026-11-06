# Commit the repo cleanup (approved 2026-11-05)

The files are already changed (dead upstream layouts and fonts gone, README
pictures in `img/`, previews and renders no longer tracked, `todo` ignored).
Untracking is a git step, so it's yours:

```bash
cd /srv/mergerfs/pool/VARIOUS/keyboard/CYBER60
git rm -r --cached pi/draw draw/shoko case/3d/renders case/deck/renders todo
git add -u
git add .gitignore img/
git status
```

`git status` should show `img/` as new and nothing from `pi/draw/`,
`draw/shoko/`, the renders, `todo`, `firmware/` or `config/secrets/`. Note that
`git add -u` also stages the other work in progress on tracked files; new files
(`pi/tools/dev.sh`, `pi/kbd/`, `pi/PHONE-DESIGN.md`, `pi/app/src/tone.c`,
weather/calendar/keyboard-link files) need their own `git add` when they're
ready. Deleted files stay in git history; shrinking that needs a history
rewrite and a force-push (your call).

# Before the Pi goes to work (~mid-December 2026): remove the build-phase access

Added 2026-11-02 so Claude can reach the Pi from OMV while it sits at home
(root, OMV's key only, no password).

- [ ] Put `ssh=0` in `deck.conf` and power-cycle (takes the key off the Pi).
- [ ] Delete `pi/image/files/deck-ssh.sh`, `deck-ssh.service`, `ssh/`, and
      their lines in `pi/image/build-app.sh` and `pi/image/build-image.sh`
      (marked "BUILD PHASE ONLY"), and `ssh=` in `files/deck.conf`.
- [ ] Delete the Spotify API probe in `pi/app/src/spotify.c` (marked "BUILD
      PHASE ONLY").
- [ ] `pi/tools/dev.sh` (the live loop over SSH) stops working: delete it. On
      the Pi, `/opt/deck.release` (the copy it kept) can go.
- [ ] On OMV: `Host deck` in `~/.ssh/config`, `~/.ssh/id_ed25519_deck*`,
      `~/.ssh/known_hosts_deck`.
- [ ] Build the final SD image (Actions → Pi screen → Run workflow → image).
